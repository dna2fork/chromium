// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/animation/worklet_animation.h"

#include "cc/animation/scroll_timeline.h"
#include "cc/trees/animation_options.h"

namespace cc {

WorkletAnimation::WorkletAnimation(
    int id,
    const std::string& name,
    std::unique_ptr<ScrollTimeline> scroll_timeline,
    std::unique_ptr<AnimationOptions> options,
    bool is_controlling_instance)
    : SingleKeyframeEffectAnimation(id),
      name_(name),
      scroll_timeline_(std::move(scroll_timeline)),
      options_(std::move(options)),
      start_time_(base::nullopt),
      last_current_time_(base::nullopt),
      is_impl_instance_(is_controlling_instance) {}

WorkletAnimation::~WorkletAnimation() = default;

scoped_refptr<WorkletAnimation> WorkletAnimation::Create(
    int id,
    const std::string& name,
    std::unique_ptr<ScrollTimeline> scroll_timeline,
    std::unique_ptr<AnimationOptions> options) {
  return WrapRefCounted(new WorkletAnimation(
      id, name, std::move(scroll_timeline), std::move(options), false));
}

scoped_refptr<Animation> WorkletAnimation::CreateImplInstance() const {
  std::unique_ptr<ScrollTimeline> impl_timeline;
  if (scroll_timeline_)
    impl_timeline = scroll_timeline_->CreateImplInstance();

  return WrapRefCounted(new WorkletAnimation(
      id(), name(), std::move(impl_timeline), CloneOptions(), true));
}

void WorkletAnimation::Tick(base::TimeTicks monotonic_time) {
  // Do not tick worklet animations on main thread. This should be removed if we
  // skip ticking all animations on main thread in http://crbug.com/762717.
  if (!is_impl_instance_)
    return;

  // As the output of a WorkletAnimation is driven by a script-provided local
  // time, we don't want the underlying effect to participate in the normal
  // animations lifecycle. To avoid this we pause the underlying keyframe effect
  // at the local time obtained from the user script - essentially turning each
  // call to |WorkletAnimation::Tick| into a seek in the effect.
  keyframe_effect()->Pause(local_time_);
  keyframe_effect()->Tick(monotonic_time);
}

MutatorInputState::AnimationState WorkletAnimation::GetInputState(
    base::TimeTicks monotonic_time,
    const ScrollTree& scroll_tree) {
  // Record the monotonic time to be the start time first time state is
  // generated. This time is used as the origin for computing the current time.
  if (!start_time_.has_value())
    start_time_ = monotonic_time;

  double current_time = CurrentTime(monotonic_time, scroll_tree);
  last_current_time_ = current_time;
  return {id(), name(), current_time, CloneOptions()};
}

void WorkletAnimation::SetOutputState(
    const MutatorOutputState::AnimationState& state) {
  local_time_ = state.local_time;
  SetNeedsPushProperties();
}

// TODO(crbug.com/780151): Multiply the result by the play back rate.
double WorkletAnimation::CurrentTime(base::TimeTicks monotonic_time,
                                     const ScrollTree& scroll_tree) {
  // Note that we have intentionally decided not to offset the scroll timeline
  // by the start time. See: https://github.com/w3c/csswg-drafts/issues/2075
  if (scroll_timeline_)
    return scroll_timeline_->CurrentTime(scroll_tree);
  return (monotonic_time - start_time_.value()).InMillisecondsF();
}

bool WorkletAnimation::NeedsUpdate(base::TimeTicks monotonic_time,
                                   const ScrollTree& scroll_tree) {
  // If we don't have a start time it means that an update was never sent to
  // the worklet therefore we need one.
  if (!scroll_timeline_ && !start_time_.has_value())
    return true;

  double current_time = CurrentTime(monotonic_time, scroll_tree);
  bool needs_update = last_current_time_ != current_time;
  return needs_update;
}

bool WorkletAnimation::IsWorkletAnimation() const {
  return true;
}

}  // namespace cc
