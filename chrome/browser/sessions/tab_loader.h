// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SESSIONS_TAB_LOADER_H_
#define CHROME_BROWSER_SESSIONS_TAB_LOADER_H_

#include <vector>

#include "base/callback.h"
#include "base/containers/flat_set.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/memory_coordinator_client.h"
#include "base/memory/memory_pressure_listener.h"
#include "base/time/tick_clock.h"
#include "base/timer/timer.h"
#include "chrome/browser/resource_coordinator/tab_load_tracker.h"
#include "chrome/browser/sessions/session_restore_delegate.h"
#include "chrome/browser/sessions/tab_loader_delegate.h"

class SessionRestoreStatsCollector;
class TabLoaderTester;

// TabLoader is responsible for loading tabs after session restore has finished
// creating all the tabs. Tabs are loaded after a previously started tab
// finishes loading or a timeout is reached. If the timeout is reached before a
// tab finishes loading the timeout delay is doubled.
//
// TabLoader keeps a reference to itself when it's loading. When it has finished
// loading, it drops the reference. If another profile is restored while the
// TabLoader is loading, it will schedule its tabs to get loaded by the same
// TabLoader. When doing the scheduling, it holds a reference to the TabLoader.
// This is not part of SessionRestoreImpl so that synchronous destruction
// of SessionRestoreImpl doesn't have timing problems.
//
// TabLoader is effectively a state machine that guides session/tab restored
// tabs through being unloaded, to loading and finally to their loaded state. It
// does this while respecting memory pressure, a maximum simultaneous number of
// tabs loading in parallel, and a maximum tab load timeouts. At most one
// TabLoader exists at a moment; it owns itself and destroys itself once all
// tabs posted to it have been loaded.
//
// Beyond requesting tabs to load TabLoader maintains the following invariant:
//
// - If loads are ongoing and there are future tabs to load, then a timeout
//   timer is running.
//
// The general principle is that before returning control to the caller,
// the invariant is maintained. Extra care is taken in functions that may
// can cause reentrancy as they need to ensure the invariant is satisfied before
// passing control to the external code.
//
// Since the conditions for self-destroying can occur while deeply nested in our
// own code an entrance count is maintained to ensure it only happens on the way
// out of the outermost function.
class TabLoader : public base::RefCounted<TabLoader>,
                  public TabLoaderCallback,
                  public base::MemoryCoordinatorClient,
                  public resource_coordinator::TabLoadTracker::Observer {
 public:
  // Helper class used for tracking reentrancy and performing lifetime
  // management. See implementation for full details.
  class ReentrancyHelper;

  using RestoredTab = SessionRestoreDelegate::RestoredTab;

  // Called to start restoring tabs.
  static void RestoreTabs(const std::vector<RestoredTab>& tabs,
                          const base::TimeTicks& restore_started);

 private:
  friend class base::RefCounted<TabLoader>;
  friend class ReentrancyHelper;

  // Allows access from various unittests.
  friend class TabLoaderTester;

  // Used for storing tabs under our control that have started loading. The
  // set of these is sorted by |loading_start_time| and used to manage the
  // loading timeout timer.
  struct LoadingTab {
    base::TimeTicks loading_start_time;
    content::WebContents* contents;

    // For use with sorted STL containers.
    bool operator<(const LoadingTab& rhs) const {
      return std::tie(loading_start_time, contents) <
             std::tie(rhs.loading_start_time, contents);
    }
  };

  using LoadingTabSet = base::flat_set<LoadingTab>;
  using TabSet = base::flat_set<content::WebContents*>;
  using TabVector = std::vector<content::WebContents*>;

  explicit TabLoader(base::TimeTicks restore_started);
  ~TabLoader() override;

  // TabLoaderCallback:
  void SetTabLoadingEnabled(bool loading_enabled) override;

  // This is invoked once by RestoreTabs to start loading.
  void StartLoading(const std::vector<RestoredTab>& tabs);

  // resource_coordinator::TabLoadTracker::Observer implementation:
  void OnLoadingStateChange(content::WebContents* contents,
                            LoadingState loading_state) override;
  void OnStopTracking(content::WebContents* contents,
                      LoadingState loading_state) override;

  // React to memory pressure by stopping to load any more tabs.
  void OnMemoryPressure(
      base::MemoryPressureListener::MemoryPressureLevel memory_pressure_level);

  // base::MemoryCoordinatorClient implementation:
  void OnMemoryStateChange(base::MemoryState state) override;

  // Determines whether or not tab loading should stop early due to external
  // factors.
  bool ShouldStopLoadingTabs() const;

  // Determines the number of tab loads that can safely be started at the
  // moment.
  size_t GetMaxNewTabLoads() const;

  // Adds a tab that we are responsible for to one of the |tabs_*| containers.
  // Can invalidate self-destroy and timer invariants.
  void AddTab(content::WebContents* contents, bool loading_initiated);

  // Removes the tab from the set of tabs to load and list of tabs we're waiting
  // to get a load from. Can invalidate self-destroy and timer invariants.
  void RemoveTab(content::WebContents* contents);

  // Moves the tab from |tabs_to_load_| to |tabs_load_initiated_|. Can
  // invalidate self-destroy and timer invariants.
  void MarkTabAsLoadInitiated(content::WebContents* contents);

  // Moves the tab from |tabs_to_load_| or |tabs_load_initiated_| to
  // |tabs_loading_|. Can invalidate self-destroy and timer invariants.
  void MarkTabAsLoading(content::WebContents* contents);

  // Stops tracking the tab, marking its load as deferred. This will remove it
  // from all tab tracking containers and notify the stats delegate of the
  // deferred load.
  void MarkTabAsDeferred(content::WebContents* contents);

  // Maybes loads one of more tabs. This will cause one or more tabs (up to the
  // number of open loading slots) to load, while respecting the loading slot
  // cap.
  void MaybeLoadSomeTabs();

  // Invoked from |force_load_timer_|. Doubles |force_load_delay_multiplier_|
  // and invokes |LoadNextTab| to load the next tab
  void ForceLoadTimerFired();

  // Stops loading tabs.
  void StopLoadingTabs();

  // Gets the next tab to load, returning nullptr if there are none. Note that
  // this can cause |tabs_to_load_| to be drained due to policy decision made by
  // the TabLoaderDelegate.
  content::WebContents* GetNextTabToLoad();

  // Loads the next tab and restores invariants. This should only be called if
  // there is a next tab to load. This will always start loading a next tab even
  // if the number of simultaneously loading tabs is exceeded.
  void LoadNextTab(bool due_to_timeout);

  // Returns the current load timeout period.
  base::TimeDelta GetLoadTimeoutPeriod() const;

  // Can do nothing, start a timer, or cancel a previously started timer
  // depending on whether or not one needs to be running.
  void StartTimerIfNeeded();

  // Limit the number of loaded tabs.
  // Value of 0 restores default behavior. In test mode command line flags and
  // free memory size are not taken into account.
  static void SetMaxLoadedTabCountForTesting(size_t value);

  // Sets an on construction callback for testing.
  static void SetConstructionCallbackForTesting(
      base::RepeatingCallback<void(TabLoader*)>* callback);

  // Sets the number of simultaneous loads for testing.
  void SetMaxSimultaneousLoadsForTesting(size_t loading_slots);

  // Sets the tick clock.
  void SetTickClockForTesting(base::TickClock* tick_clock);

  // Calls MaybeLoadSomeTabs, but wrapped with entry count management.
  void MaybeLoadSomeTabsForTesting();

  // The OS specific delegate of the TabLoader.
  std::unique_ptr<TabLoaderDelegate> delegate_;

  // Listens for system under memory pressure notifications and stops loading
  // of tabs when we start running out of memory.
  base::MemoryPressureListener memory_pressure_listener_;

  // Used for selecting which timeout to use, and to prevent additional
  // non-active tabs from being scheduled to load initially.
  bool did_one_tab_load_ = false;

  // The number of tabs to load simultaneously. This is a soft cap in that it
  // can be exceeded by tabs that timeout, visible tabs, and user interactions
  // forcing a tab load. However, normal session restore tab loads will not kick
  // off a new load unless there is room below this cap. This is initialized via
  // the delegate. The initial value of 0 is used to indicate "uninitialized".
  size_t max_simultaneous_loads_ = 0;

  // The delay timer multiplier. See class description for details.
  size_t force_load_delay_multiplier_ = 1;

  // True if tab loading is currently enabled. The delegate can cause this to
  // toggle.
  bool is_loading_enabled_ = true;

  // The following 3 containers are mutually exclusive. A tab will be in at most
  // one of them at any moment.

  // The tabs that have been restored for which we need to schedule loads. This
  // does not include "active" tabs. Tabs transition from this container to
  // |tabs_load_initiated_|, or are removed from this container.
  TabVector tabs_to_load_;

  // The set of tabs that we have initiated loading, but for which we're
  // waiting for TabLoadTracker to tell us has actually commenced (network
  // activity). This is used to ensure we don't start loading too many tabs.
  // Tabs are removed from this container in two ways: if they were observed to
  // start loading they transition to |tabs_loading_|. Otherwise (closed before
  // loading starts) they stop being tracked by this TabLoader.
  TabSet tabs_load_initiated_;

  // The set of tabs that we have started loading, along with the times at which
  // their loads started. This is used to drive load timeout logic. Tabs
  // eventually transition out of this container. When the 3 tab containers are
  // empty the TabLoader detaches from being the shared TabLoader and destroys
  // itself.
  LoadingTabSet tabs_loading_;

  // The number of tabs that were passed into this TabLoader that have been
  // observed starting to load, or for which we explicitly initiated the load.
  // This is monotonically increasing, and can never exceed the combined number
  // of tabs passed into this TabLoader via StartLoading(). This is only used in
  // order to support a combined maximum total number of tab loads for testing.
  size_t scheduled_to_load_count_ = 0;

  // Timer used to force progress despite tabs that take too long to load.
  base::OneShotTimer force_load_timer_;

  // The time at which the timer is scheduled to fire. Used to minimize
  // restarts of the timer. This should be default initialized when the timer is
  // not running.
  base::TimeTicks force_load_time_;

  // For keeping TabLoader alive while it's loading even if no
  // SessionRestoreImpls reference it.
  scoped_refptr<TabLoader> this_retainer_;

  // The SessionRestoreStatsCollector associated with this TabLoader. This is
  // explicitly referenced so that it can be notified of deferred tab loads due
  // to memory pressure.
  scoped_refptr<SessionRestoreStatsCollector> stats_collector_;

  // The tick clock used by this class. This is used as a testing seam. If not
  // overridden it defaults to a base::DefaultTickClock.
  const base::TickClock* clock_;

  // Holds a pointer to the active tab loader, if one exists. Overlapping
  // session restores will be handled by the same tab loader.
  static TabLoader* shared_tab_loader_;

  // Used to prevent self-destroys while in nested calls, and to initiate
  // self-destroying from the outermost scope only. This is managed by the
  // ReentrancyHelper, and indicates the number of times that the current object
  // has been reentered. Only functions that are directly invoked by external
  // callers are counted.
  size_t reentry_depth_ = 0;

  DISALLOW_COPY_AND_ASSIGN(TabLoader);
};

#endif  // CHROME_BROWSER_SESSIONS_TAB_LOADER_H_
