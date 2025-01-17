// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/resource_coordinator/tab_load_tracker_test_support.h"

#include "base/run_loop.h"

namespace resource_coordinator {

namespace {

enum class WaitForEvent { NO_LONGER_TRACKED };

class WaitForLoadingStateHelper : public TabLoadTracker::Observer {
 public:
  // Configures this helper to wait until the tab reaches the provided loading
  // state.
  WaitForLoadingStateHelper(content::WebContents* waiting_for_contents,
                            LoadingState waiting_for_state)
      : waiting_for_contents_(waiting_for_contents),
        waiting_for_state_(waiting_for_state),
        waiting_for_no_longer_tracked_(false),
        wait_successful_(false) {}

  // Configures this helper to wait until the tab is no longer tracked.
  WaitForLoadingStateHelper(content::WebContents* waiting_for_contents,
                            WaitForEvent no_longer_tracked_unused)
      : waiting_for_contents_(waiting_for_contents),
        waiting_for_state_(TabLoadTracker::LOADING_STATE_MAX),
        waiting_for_no_longer_tracked_(true),
        wait_successful_(false) {}

  ~WaitForLoadingStateHelper() override {}

  bool Wait() {
    wait_successful_ = false;
    auto* tracker = resource_coordinator::TabLoadTracker::Get();

    // Early exit if the contents is already in the desired state.
    if (!waiting_for_no_longer_tracked_ &&
        tracker->GetLoadingState(waiting_for_contents_) == waiting_for_state_) {
      wait_successful_ = true;
      return wait_successful_;
    }

    tracker->AddObserver(this);
    base::RunLoop run_loop;
    run_loop_quit_closure_ = run_loop.QuitClosure();
    run_loop.Run();
    tracker->RemoveObserver(this);

    return wait_successful_;
  }

 protected:
  void OnLoadingStateChange(content::WebContents* web_contents,
                            LoadingState loading_state) override {
    if (waiting_for_no_longer_tracked_)
      return;
    if (waiting_for_contents_ == web_contents &&
        waiting_for_state_ == loading_state) {
      wait_successful_ = true;
      run_loop_quit_closure_.Run();
    }
  }

  void OnStopTracking(content::WebContents* web_contents,
                      LoadingState loading_state) override {
    if (waiting_for_contents_ != web_contents)
      return;
    if (waiting_for_no_longer_tracked_) {
      wait_successful_ = true;
    } else {
      wait_successful_ = (waiting_for_state_ == loading_state);
    }
    run_loop_quit_closure_.Run();
  }

 private:
  // The contents and state that is being waited for.
  content::WebContents* waiting_for_contents_;
  LoadingState waiting_for_state_;
  bool waiting_for_no_longer_tracked_;

  // Returns true if the wait was successful. This can be false if the contents
  // stops being tracked (is destroyed) before encountering the desired state.
  bool wait_successful_;

  base::Closure run_loop_quit_closure_;

  DISALLOW_COPY_AND_ASSIGN(WaitForLoadingStateHelper);
};

}  // namespace

bool WaitForTransitionToLoadingState(
    content::WebContents* contents,
    TabLoadTracker::LoadingState loading_state) {
  WaitForLoadingStateHelper waiter(contents, loading_state);
  return waiter.Wait();
}

bool WaitForTransitionToUnloaded(content::WebContents* contents) {
  return WaitForTransitionToLoadingState(contents, TabLoadTracker::UNLOADED);
}

bool WaitForTransitionToLoading(content::WebContents* contents) {
  return WaitForTransitionToLoadingState(contents, TabLoadTracker::LOADING);
}

bool WaitForTransitionToLoaded(content::WebContents* contents) {
  return WaitForTransitionToLoadingState(contents, TabLoadTracker::LOADED);
}

bool WaitUntilNoLongerTracked(content::WebContents* contents) {
  WaitForLoadingStateHelper waiter(contents, WaitForEvent::NO_LONGER_TRACKED);
  return waiter.Wait();
}

}  // namespace resource_coordinator
