// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/html/lazy_load_frame_observer.h"

#include <limits>

#include "base/metrics/histogram_macros.h"
#include "third_party/blink/public/platform/web_effective_connection_type.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/frame.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_client.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/geometry/dom_rect_read_only.h"
#include "third_party/blink/renderer/core/html/html_frame_owner_element.h"
#include "third_party/blink/renderer/core/intersection_observer/intersection_observer.h"
#include "third_party/blink/renderer/core/intersection_observer/intersection_observer_entry.h"
#include "third_party/blink/renderer/core/loader/frame_load_request.h"
#include "third_party/blink/renderer/core/loader/frame_loader.h"
#include "third_party/blink/renderer/platform/heap/visitor.h"
#include "third_party/blink/renderer/platform/length.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_request.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin.h"
#include "third_party/blink/renderer/platform/wtf/assertions.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

namespace {

// Determine if the |bounding_client_rect| for a frame indicates that the frame
// is probably hidden according to some experimental heuristics. Since hidden
// frames are often used for analytics or communication, and lazily loading them
// could break their functionality, so these heuristics are used to recognize
// likely hidden frames and immediately load them so that they can function
// properly.
bool IsFrameProbablyHidden(const DOMRectReadOnly& bounding_client_rect) {
  // Tiny frames that are 4x4 or smaller are likely not intended to be seen by
  // the user. Note that this condition includes frames marked as
  // "display:none", since those frames would have dimensions of 0x0.
  if (bounding_client_rect.width() < 4.1 || bounding_client_rect.height() < 4.1)
    return true;

  // Frames that are positioned completely off the page above or to the left are
  // likely never intended to be visible to the user.
  if (bounding_client_rect.right() < 0.0 || bounding_client_rect.bottom() < 0.0)
    return true;

  return false;
}

int GetLazyFrameLoadingViewportDistanceThresholdPx(const Document& document) {
  const Settings* settings = document.GetSettings();
  if (!settings)
    return 0;

  DCHECK(document.GetFrame() && document.GetFrame()->Client());
  switch (document.GetFrame()->Client()->GetEffectiveConnectionType()) {
    case WebEffectiveConnectionType::kTypeUnknown:
      return settings->GetLazyFrameLoadingDistanceThresholdPxUnknown();
    case WebEffectiveConnectionType::kTypeOffline:
      return settings->GetLazyFrameLoadingDistanceThresholdPxOffline();
    case WebEffectiveConnectionType::kTypeSlow2G:
      return settings->GetLazyFrameLoadingDistanceThresholdPxSlow2G();
    case WebEffectiveConnectionType::kType2G:
      return settings->GetLazyFrameLoadingDistanceThresholdPx2G();
    case WebEffectiveConnectionType::kType3G:
      return settings->GetLazyFrameLoadingDistanceThresholdPx3G();
    case WebEffectiveConnectionType::kType4G:
      return settings->GetLazyFrameLoadingDistanceThresholdPx4G();
  }
  NOTREACHED();
  return 0;
}

}  // namespace

LazyLoadFrameObserver::LazyLoadFrameObserver(HTMLFrameOwnerElement& element)
    : element_(&element) {}

void LazyLoadFrameObserver::DeferLoadUntilNearViewport(
    const ResourceRequest& resource_request,
    WebFrameLoadType frame_load_type) {
  DCHECK(!lazy_load_intersection_observer_);

  lazy_load_intersection_observer_ = IntersectionObserver::Create(
      {Length(GetLazyFrameLoadingViewportDistanceThresholdPx(
                  element_->GetDocument()),
              kFixed)},
      {std::numeric_limits<float>::min()}, &element_->GetDocument(),
      WTF::BindRepeating(&LazyLoadFrameObserver::LoadIfHiddenOrNearViewport,
                         WrapWeakPersistent(this), resource_request,
                         frame_load_type));

  lazy_load_intersection_observer_->observe(element_);
}

void LazyLoadFrameObserver::CancelPendingLazyLoad() {
  if (!lazy_load_intersection_observer_)
    return;
  lazy_load_intersection_observer_->disconnect();
  lazy_load_intersection_observer_.Clear();
}

void LazyLoadFrameObserver::LoadIfHiddenOrNearViewport(
    const ResourceRequest& resource_request,
    WebFrameLoadType frame_load_type,
    const HeapVector<Member<IntersectionObserverEntry>>& entries) {
  DCHECK(!entries.IsEmpty());
  DCHECK_EQ(element_, entries.back()->target());

  if (!entries.back()->isIntersecting() &&
      !IsFrameProbablyHidden(*entries.back()->boundingClientRect())) {
    return;
  }

  // The content frame of the element should not have changed, since any
  // pending lazy load should have been already been cancelled in
  // DisconnectContentFrame() if the content frame changes.
  DCHECK(element_->ContentFrame());

  // Note that calling FrameLoader::Load() causes the
  // |lazy_load_intersection_observer| to be disconnected.
  ToLocalFrame(element_->ContentFrame())
      ->Loader()
      .StartNavigation(
          FrameLoadRequest(&element_->GetDocument(), resource_request),
          frame_load_type);
}

void LazyLoadFrameObserver::StartTrackingVisibilityMetrics() {
  DCHECK(time_when_first_visible_.is_null());
  DCHECK(!visibility_metrics_observer_);

  visibility_metrics_observer_ = IntersectionObserver::Create(
      {}, {std::numeric_limits<float>::min()}, &element_->GetDocument(),
      WTF::BindRepeating(
          &LazyLoadFrameObserver::RecordMetricsOnVisibilityChanged,
          WrapWeakPersistent(this)));

  visibility_metrics_observer_->observe(element_);
}

void LazyLoadFrameObserver::RecordMetricsOnVisibilityChanged(
    const HeapVector<Member<IntersectionObserverEntry>>& entries) {
  DCHECK(!entries.IsEmpty());
  DCHECK_EQ(element_, entries.back()->target());

  if (IsFrameProbablyHidden(*entries.back()->boundingClientRect())) {
    visibility_metrics_observer_->disconnect();
    visibility_metrics_observer_.Clear();
    return;
  }

  if (!has_above_the_fold_been_set_) {
    is_initially_above_the_fold_ = entries.back()->isIntersecting();
    has_above_the_fold_been_set_ = true;
  }

  if (!entries.back()->isIntersecting())
    return;

  DCHECK(time_when_first_visible_.is_null());
  time_when_first_visible_ = CurrentTimeTicks();
  RecordVisibilityMetricsIfLoadedAndVisible();

  // On slow networks, iframes might not finish loading by the time the user
  // leaves the page, so the visible load time metrics samples won't represent
  // the slowest frames. To remedy this, record how often below the fold
  // lazyload-eligible frames become visible before they've finished loading.
  // This isn't recorded for above the fold frames since basically every above
  // the fold frame would be visible before they finish loading.
  if (time_when_first_load_finished_.is_null() &&
      !is_initially_above_the_fold_ && element_->GetDocument().GetFrame()) {
    // Note: If the WebEffectiveConnectionType enum ever gets out of sync with
    // net::EffectiveConnectionType, then this will have to be updated to record
    // the sample in terms of net::EffectiveConnectionType instead of
    // WebEffectiveConnectionType.
    UMA_HISTOGRAM_ENUMERATION(
        "Blink.VisibleBeforeLoaded.LazyLoadEligibleFrames.BelowTheFold",
        element_->GetDocument()
            .GetFrame()
            ->Client()
            ->GetEffectiveConnectionType());
  }

  visibility_metrics_observer_->disconnect();
  visibility_metrics_observer_.Clear();
}

void LazyLoadFrameObserver::RecordMetricsOnLoadFinished() {
  if (!time_when_first_load_finished_.is_null())
    return;
  time_when_first_load_finished_ = CurrentTimeTicks();
  RecordVisibilityMetricsIfLoadedAndVisible();
}

void LazyLoadFrameObserver::RecordVisibilityMetricsIfLoadedAndVisible() {
  if (time_when_first_load_finished_.is_null() ||
      time_when_first_visible_.is_null() ||
      !element_->GetDocument().GetFrame()) {
    return;
  }

  DCHECK(has_above_the_fold_been_set_);

  TimeDelta visible_load_delay =
      time_when_first_load_finished_ - time_when_first_visible_;
  if (visible_load_delay < TimeDelta())
    visible_load_delay = TimeDelta();

  switch (element_->GetDocument()
              .GetFrame()
              ->Client()
              ->GetEffectiveConnectionType()) {
    case WebEffectiveConnectionType::kTypeSlow2G:
      if (is_initially_above_the_fold_) {
        UMA_HISTOGRAM_MEDIUM_TIMES(
            "Blink.VisibleLoadTime.LazyLoadEligibleFrames.AboveTheFold.Slow2G",
            visible_load_delay);
      } else {
        UMA_HISTOGRAM_MEDIUM_TIMES(
            "Blink.VisibleLoadTime.LazyLoadEligibleFrames.BelowTheFold.Slow2G",
            visible_load_delay);
      }
      break;

    case WebEffectiveConnectionType::kType2G:
      if (is_initially_above_the_fold_) {
        UMA_HISTOGRAM_MEDIUM_TIMES(
            "Blink.VisibleLoadTime.LazyLoadEligibleFrames.AboveTheFold.2G",
            visible_load_delay);
      } else {
        UMA_HISTOGRAM_MEDIUM_TIMES(
            "Blink.VisibleLoadTime.LazyLoadEligibleFrames.BelowTheFold.2G",
            visible_load_delay);
      }
      break;

    case WebEffectiveConnectionType::kType3G:
      if (is_initially_above_the_fold_) {
        UMA_HISTOGRAM_MEDIUM_TIMES(
            "Blink.VisibleLoadTime.LazyLoadEligibleFrames.AboveTheFold.3G",
            visible_load_delay);
      } else {
        UMA_HISTOGRAM_MEDIUM_TIMES(
            "Blink.VisibleLoadTime.LazyLoadEligibleFrames.BelowTheFold.3G",
            visible_load_delay);
      }
      break;

    case WebEffectiveConnectionType::kType4G:
      if (is_initially_above_the_fold_) {
        UMA_HISTOGRAM_MEDIUM_TIMES(
            "Blink.VisibleLoadTime.LazyLoadEligibleFrames.AboveTheFold.4G",
            visible_load_delay);
      } else {
        UMA_HISTOGRAM_MEDIUM_TIMES(
            "Blink.VisibleLoadTime.LazyLoadEligibleFrames.BelowTheFold.4G",
            visible_load_delay);
      }
      break;

    case WebEffectiveConnectionType::kTypeUnknown:
    case WebEffectiveConnectionType::kTypeOffline:
      // No VisibleLoadTime histograms are recorded for these effective
      // connection types.
      break;
  }
}

void LazyLoadFrameObserver::Trace(blink::Visitor* visitor) {
  visitor->Trace(element_);
  visitor->Trace(lazy_load_intersection_observer_);
  visitor->Trace(visibility_metrics_observer_);
}

}  // namespace blink
