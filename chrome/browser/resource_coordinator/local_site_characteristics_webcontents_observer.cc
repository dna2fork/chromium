// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/resource_coordinator/local_site_characteristics_webcontents_observer.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/resource_coordinator/local_site_characteristics_data_store_factory.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"

namespace resource_coordinator {

namespace {

TabVisibility ContentVisibilityToRCVisibility(content::Visibility visibility) {
  if (visibility == content::Visibility::VISIBLE)
    return TabVisibility::kForeground;
  return TabVisibility::kBackground;
}

bool g_skip_observer_registration_for_testing = false;

}  // namespace

// static
void LocalSiteCharacteristicsWebContentsObserver::
    SkipObserverRegistrationForTesting() {
  g_skip_observer_registration_for_testing = true;
}

LocalSiteCharacteristicsWebContentsObserver::
    LocalSiteCharacteristicsWebContentsObserver(
        content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents) {
  if (!g_skip_observer_registration_for_testing) {
    // The PageSignalReceiver has to be enabled in order to properly track the
    // non-persistent notification events.
    DCHECK(PageSignalReceiver::IsEnabled());

    TabLoadTracker::Get()->AddObserver(this);
    PageSignalReceiver::GetInstance()->AddObserver(this);
  }
}

LocalSiteCharacteristicsWebContentsObserver::
    ~LocalSiteCharacteristicsWebContentsObserver() {
  DCHECK(!writer_);
}

void LocalSiteCharacteristicsWebContentsObserver::OnVisibilityChanged(
    content::Visibility visibility) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!writer_)
    return;

  auto rc_visibility = ContentVisibilityToRCVisibility(visibility);
  writer_->NotifySiteVisibilityChanged(rc_visibility);
}

void LocalSiteCharacteristicsWebContentsObserver::WebContentsDestroyed() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!g_skip_observer_registration_for_testing) {
    TabLoadTracker::Get()->RemoveObserver(this);
    PageSignalReceiver::GetInstance()->RemoveObserver(this);
  }
  writer_.reset();
}

void LocalSiteCharacteristicsWebContentsObserver::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(navigation_handle);

  first_time_title_set_ = false;
  first_time_favicon_set_ = false;

  if (!navigation_handle->HasCommitted())
    return;

  const url::Origin new_origin =
      url::Origin::Create(navigation_handle->GetURL());

  if (writer_ && new_origin == writer_origin_)
    return;

  writer_.reset();
  writer_origin_ = url::Origin();

  // Only store information for the HTTP(S) sites for now.
  if (!navigation_handle->GetURL().SchemeIsHTTPOrHTTPS())
    return;

  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  DCHECK(profile);
  SiteCharacteristicsDataStore* data_store =
      LocalSiteCharacteristicsDataStoreFactory::GetForProfile(profile);
  DCHECK(data_store);
  writer_ = data_store->GetWriterForOrigin(
      new_origin,
      ContentVisibilityToRCVisibility(web_contents()->GetVisibility()));

  writer_origin_ = new_origin;
}

void LocalSiteCharacteristicsWebContentsObserver::TitleWasSet(
    content::NavigationEntry* entry) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // TODO(sebmarchand): Check if the title is always set at least once before
  // loading completes, in which case this check could be removed.
  if (!first_time_title_set_) {
    first_time_title_set_ = true;
    return;
  }

  MaybeNotifyBackgroundFeatureUsage(
      &SiteCharacteristicsDataWriter::NotifyUpdatesTitleInBackground);
}

void LocalSiteCharacteristicsWebContentsObserver::DidUpdateFaviconURL(
    const std::vector<content::FaviconURL>& candidates) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!first_time_favicon_set_) {
    first_time_favicon_set_ = true;
    return;
  }

  MaybeNotifyBackgroundFeatureUsage(
      &SiteCharacteristicsDataWriter::NotifyUpdatesFaviconInBackground);
}

void LocalSiteCharacteristicsWebContentsObserver::OnAudioStateChanged(
    bool audible) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!audible)
    return;

  MaybeNotifyBackgroundFeatureUsage(
      &SiteCharacteristicsDataWriter::NotifyUsesAudioInBackground);
}

void LocalSiteCharacteristicsWebContentsObserver::OnLoadingStateChange(
    content::WebContents* contents,
    LoadingState loading_state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (web_contents() != contents)
    return;

  if (!writer_)
    return;

  if (loading_state == TabLoadTracker::LoadingState::LOADED) {
    writer_->NotifySiteLoaded();
  } else {
    writer_->NotifySiteUnloaded();
  }
}

void LocalSiteCharacteristicsWebContentsObserver::
    OnNonPersistentNotificationCreated(content::WebContents* contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (web_contents() != contents)
    return;

  MaybeNotifyBackgroundFeatureUsage(
      &SiteCharacteristicsDataWriter::NotifyUsesNotificationsInBackground);
}

bool LocalSiteCharacteristicsWebContentsObserver::
    ShouldIgnoreFeatureUsageEvent() {
  if (!writer_)
    return true;

  if (TabLoadTracker::Get()->GetLoadingState(web_contents()) !=
      TabLoadTracker::LOADED) {
    return true;
  }

  return false;
}

void LocalSiteCharacteristicsWebContentsObserver::
    MaybeNotifyBackgroundFeatureUsage(
        void (SiteCharacteristicsDataWriter::*method)()) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (ShouldIgnoreFeatureUsageEvent())
    return;

  if (ContentVisibilityToRCVisibility(web_contents()->GetVisibility()) ==
      TabVisibility::kBackground) {
    (writer_.get()->*method)();
  }
}

}  // namespace resource_coordinator
