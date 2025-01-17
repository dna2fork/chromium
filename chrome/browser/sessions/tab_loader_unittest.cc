// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sessions/tab_loader.h"

#include <vector>

#include "base/bind.h"
#include "base/memory/memory_coordinator_client_registry.h"
#include "base/run_loop.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/time/time.h"
#include "chrome/browser/resource_coordinator/tab_helper.h"
#include "chrome/browser/resource_coordinator/tab_manager_features.h"
#include "chrome/browser/sessions/tab_loader_tester.h"
#include "chrome/test/base/testing_profile.h"
#include "components/variations/variations_params_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/public/test/test_web_contents_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

using resource_coordinator::TabLoadTracker;
using resource_coordinator::ResourceCoordinatorTabHelper;

class TabLoaderTest : public testing::Test {
 protected:
  using RestoredTab = SessionRestoreDelegate::RestoredTab;

  TabLoaderTest() : max_simultaneous_loads_(1) {}

  void OnTabLoaderCreated(TabLoader* tab_loader) {
    tab_loader_.SetTabLoader(tab_loader);
    tab_loader_.SetTickClockForTesting(&clock_);
    if (max_simultaneous_loads_ != 0)
      tab_loader_.SetMaxSimultaneousLoadsForTesting(max_simultaneous_loads_);
  }

  // testing::Test:
  void SetUp() override {
    construction_callback_ = base::BindRepeating(
        &TabLoaderTest::OnTabLoaderCreated, base::Unretained(this));
    TabLoaderTester::SetConstructionCallbackForTesting(&construction_callback_);
    test_web_contents_factory_.reset(new content::TestWebContentsFactory);
  }

  void TearDown() override {
    if (TabLoaderTester::shared_tab_loader() != nullptr) {
      // Expect the TabLoader to detach after all tabs have loaded.
      SimulateLoadedAll();
      EXPECT_TRUE(TabLoaderTester::shared_tab_loader() == nullptr);
    }

    TabLoaderTester::SetConstructionCallbackForTesting(nullptr);
    test_web_contents_factory_.reset();
    thread_bundle_.RunUntilIdle();
  }

  void SimulateLoadTimeout() {
    // Unfortunately there's no mock time in BrowserThreadBundle. Fast-forward
    // things and simulate firing the timer.
    EXPECT_TRUE(tab_loader_.force_load_timer().IsRunning());
    clock_.SetNowTicks(tab_loader_.force_load_time());
    tab_loader_.force_load_timer().Stop();
    tab_loader_.ForceLoadTimerFired();
  }

  void SimulateLoaded(size_t tab_index) {
    TabLoadTracker::Get()->TransitionStateForTesting(
        restored_tabs_[tab_index].contents(), TabLoadTracker::LOADED);
  }

  void SimulateLoadedAll() {
    for (size_t i = 0; i < restored_tabs_.size(); ++i)
      SimulateLoaded(i);
  }

  content::WebContents* CreateRestoredWebContents(bool is_active) {
    content::WebContents* test_contents =
        test_web_contents_factory_->CreateWebContents(&testing_profile_);
    std::vector<std::unique_ptr<content::NavigationEntry>> entries;
    entries.push_back(content::NavigationEntry::Create());
    test_contents->GetController().Restore(
        0, content::RestoreType::LAST_SESSION_EXITED_CLEANLY, &entries);
    // TabLoadTracker needs the resource_coordinator WebContentsData to be
    // initialized.
    ResourceCoordinatorTabHelper::CreateForWebContents(test_contents);

    restored_tabs_.push_back(
        RestoredTab(test_contents, is_active /* is_active */,
                    false /* is_app */, false /* is_pinned */));

    // If the tab is active start "loading" it right away for consistency with
    // session restore code.
    if (is_active)
      test_contents->GetController().LoadIfNecessary();

    return test_contents;
  }

  void CreateMultipleRestoredWebContents(size_t num_active,
                                         size_t num_inactive) {
    for (size_t i = 0; i < num_active; ++i)
      CreateRestoredWebContents(true);
    for (size_t i = 0; i < num_inactive; ++i)
      CreateRestoredWebContents(false);
  }

  // The number of loading slots to use. This needs to be set before the
  // TabLoader is created in order to be picked up by it.
  size_t max_simultaneous_loads_;

  // Set of restored tabs that is populated by calls to
  // CreateRestoredWebContents.
  std::vector<RestoredTab> restored_tabs_;

  // Automatically attaches to the tab loader that is created by the test.
  TabLoaderTester tab_loader_;

  // The tick clock that is injected into the tab loader.
  base::SimpleTestTickClock clock_;

  // The post-construction testing seam that is invoked by TabLoader.
  base::RepeatingCallback<void(TabLoader*)> construction_callback_;

  std::unique_ptr<content::TestWebContentsFactory> test_web_contents_factory_;
  content::TestBrowserThreadBundle thread_bundle_;
  TestingProfile testing_profile_;

 private:
  DISALLOW_COPY_AND_ASSIGN(TabLoaderTest);
};

TEST_F(TabLoaderTest, AllLoadingSlotsUsed) {
  // Create 2 active tabs and 4 inactive tabs.
  CreateMultipleRestoredWebContents(2, 4);

  // Use 4 loading slots. The active tabs will only use 2 which means 2 of the
  // inactive tabs should immediately be scheduled to load as well.
  max_simultaneous_loads_ = 4;

  // Create the tab loader.
  TabLoader::RestoreTabs(restored_tabs_, clock_.NowTicks());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());

  // The loader should be enabled, with 2 tabs loading and 4 tabs left to go.
  // The initial load should exclusively allow active tabs time to load, and
  // fill up the rest of the loading slots.
  EXPECT_TRUE(tab_loader_.is_loading_enabled());
  EXPECT_EQ(4u, tab_loader_.tabs_to_load().size());
  EXPECT_EQ(2u, tab_loader_.scheduled_to_load_count());
  EXPECT_EQ(2u, TabLoadTracker::Get()->GetLoadingTabCount());

  // Trying to load another tab should do nothing as no tab has yet finished
  // loading.
  tab_loader_.MaybeLoadSomeTabsForTesting();
  EXPECT_EQ(4u, tab_loader_.tabs_to_load().size());
  EXPECT_EQ(2u, tab_loader_.scheduled_to_load_count());
  EXPECT_EQ(2u, TabLoadTracker::Get()->GetLoadingTabCount());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());

  // Mark an active tab as having finished loading. This marks the end of the
  // exclusive loading period and all slots should be full now.
  SimulateLoaded(0);
  EXPECT_EQ(1u, tab_loader_.tabs_to_load().size());
  EXPECT_EQ(5u, tab_loader_.scheduled_to_load_count());
  EXPECT_EQ(4u, TabLoadTracker::Get()->GetLoadingTabCount());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());

  // Trying to load more tabs should still do nothing.
  tab_loader_.MaybeLoadSomeTabsForTesting();
  EXPECT_EQ(1u, tab_loader_.tabs_to_load().size());
  EXPECT_EQ(5u, tab_loader_.scheduled_to_load_count());
  EXPECT_EQ(4u, TabLoadTracker::Get()->GetLoadingTabCount());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());
}

TEST_F(TabLoaderTest, ForceLoadTimer) {
  // Create 1 active tab and 1 inactive tab with 1 loading slot.
  CreateMultipleRestoredWebContents(1, 1);
  max_simultaneous_loads_ = 1;

  // Create the tab loader.
  TabLoader::RestoreTabs(restored_tabs_, clock_.NowTicks());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());

  // The loader should be enabled, with 1 tab loading and 1 tab left to go.
  EXPECT_TRUE(tab_loader_.is_loading_enabled());
  EXPECT_EQ(1u, tab_loader_.tabs_to_load().size());
  EXPECT_EQ(1u, tab_loader_.scheduled_to_load_count());
  EXPECT_EQ(1u, TabLoadTracker::Get()->GetLoadingTabCount());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());

  SimulateLoadTimeout();
  EXPECT_FALSE(tab_loader_.HasTimedOutLoads());

  // Expect all tabs to be loading. Note that this also validates that
  // force-loads can exceed the number of loadingslots.
  EXPECT_TRUE(tab_loader_.is_loading_enabled());
  EXPECT_TRUE(tab_loader_.tabs_to_load().empty());
  EXPECT_EQ(2u, tab_loader_.scheduled_to_load_count());
  EXPECT_EQ(2u, TabLoadTracker::Get()->GetLoadingTabCount());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());
}

TEST_F(TabLoaderTest, LoadsAreStaggered) {
  // Create 1 active tab and 1 inactive tab with 1 loading slot.
  CreateMultipleRestoredWebContents(1, 1);
  max_simultaneous_loads_ = 1;

  // Create the tab loader.
  TabLoader::RestoreTabs(restored_tabs_, clock_.NowTicks());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());

  // The loader should be enabled, with 1 tab loading and 1 tab left to go.
  EXPECT_TRUE(tab_loader_.is_loading_enabled());
  EXPECT_EQ(1u, tab_loader_.tabs_to_load().size());
  EXPECT_EQ(1u, tab_loader_.scheduled_to_load_count());
  EXPECT_EQ(1u, TabLoadTracker::Get()->GetLoadingTabCount());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());

  // Simulate the first tab finishing loading.
  SimulateLoaded(0);

  // Expect all tabs to be loaded/loading.
  EXPECT_TRUE(tab_loader_.is_loading_enabled());
  EXPECT_TRUE(tab_loader_.tabs_to_load().empty());
  EXPECT_EQ(2u, tab_loader_.scheduled_to_load_count());
  EXPECT_EQ(1u, TabLoadTracker::Get()->GetLoadedTabCount());
  EXPECT_EQ(1u, TabLoadTracker::Get()->GetLoadingTabCount());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());
}

TEST_F(TabLoaderTest, OnMemoryStateChange) {
  // Multiple contents are necessary to make sure that the tab loader
  // doesn't immediately kick off loading of all tabs and detach.
  CreateMultipleRestoredWebContents(0, 2);

  // Create the tab loader.
  TabLoader::RestoreTabs(restored_tabs_, clock_.NowTicks());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());
  EXPECT_EQ(1u, tab_loader_.scheduled_to_load_count());

  // Simulate memory pressure and expect the tab loader to disable loading and
  // to have initiated a self-destroy.
  EXPECT_TRUE(tab_loader_.is_loading_enabled());
  tab_loader_.OnMemoryStateChange(base::MemoryState::THROTTLED);
  EXPECT_TRUE(TabLoaderTester::shared_tab_loader() == nullptr);
}

TEST_F(TabLoaderTest, OnMemoryPressure) {
  // Multiple contents are necessary to make sure that the tab loader
  // doesn't immediately kick off loading of all tabs and detach.
  CreateMultipleRestoredWebContents(0, 2);

  // Create the tab loader.
  TabLoader::RestoreTabs(restored_tabs_, clock_.NowTicks());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());
  EXPECT_EQ(1u, tab_loader_.scheduled_to_load_count());

  // Simulate memory pressure and expect the tab loader to disable loading and
  // detach from being the shared tab loader.
  EXPECT_TRUE(tab_loader_.is_loading_enabled());
  tab_loader_.OnMemoryPressure(
      base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE);
  EXPECT_TRUE(TabLoaderTester::shared_tab_loader() == nullptr);
}

TEST_F(TabLoaderTest, TimeoutCanExceedLoadingSlots) {
  CreateMultipleRestoredWebContents(1, 4);

  // Create the tab loader with 2 loading slots. This should initially start
  // loading 1 tab, due to exclusive initial loading of active tabs.
  max_simultaneous_loads_ = 2;
  TabLoader::RestoreTabs(restored_tabs_, clock_.NowTicks());
  EXPECT_EQ(4u, tab_loader_.tabs_to_load().size());
  EXPECT_EQ(1u, tab_loader_.scheduled_to_load_count());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());

  // Simulate a timeout and expect there to be 2 loading tabs and 3 left to
  // load.
  SimulateLoadTimeout();
  EXPECT_FALSE(tab_loader_.HasTimedOutLoads());
  EXPECT_EQ(3u, tab_loader_.tabs_to_load().size());
  EXPECT_EQ(2u, tab_loader_.scheduled_to_load_count());
  EXPECT_EQ(2u, tab_loader_.force_load_delay_multiplier());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());

  // Do it again and expect 3 tabs to be loading.
  SimulateLoadTimeout();
  EXPECT_FALSE(tab_loader_.HasTimedOutLoads());
  EXPECT_EQ(2u, tab_loader_.tabs_to_load().size());
  EXPECT_EQ(3u, tab_loader_.scheduled_to_load_count());
  EXPECT_EQ(4u, tab_loader_.force_load_delay_multiplier());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());

  // Do it again and expect 4 tabs to be loading.
  SimulateLoadTimeout();
  EXPECT_FALSE(tab_loader_.HasTimedOutLoads());
  EXPECT_EQ(1u, tab_loader_.tabs_to_load().size());
  EXPECT_EQ(4u, tab_loader_.scheduled_to_load_count());
  EXPECT_EQ(8u, tab_loader_.force_load_delay_multiplier());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());

  // Simulate the first tab finishing loading and don't expect more tabs to
  // start loading.
  SimulateLoaded(0);
  EXPECT_EQ(1u, tab_loader_.tabs_to_load().size());
  EXPECT_EQ(4u, tab_loader_.scheduled_to_load_count());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());

  // Simulate the second tab finishing loading and don't expect more tabs to
  // start loading.
  SimulateLoaded(1);
  EXPECT_EQ(1u, tab_loader_.tabs_to_load().size());
  EXPECT_EQ(4u, tab_loader_.scheduled_to_load_count());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());

  // Simulate the third tab finishing loading and this time expect the last tab
  // load to be initiated. There are no tabs left so the TabLoader should also
  // have initiated a self-destroy.
  SimulateLoaded(2);
  EXPECT_TRUE(tab_loader_.tabs_to_load().empty());
  EXPECT_EQ(5u, tab_loader_.scheduled_to_load_count());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());
}

TEST_F(TabLoaderTest, DelegatePolicyIsApplied) {
  namespace rc = resource_coordinator;

  std::set<std::string> features;
  features.insert(features::kInfiniteSessionRestore.name);

  // Configure the policy engine via its experimental feature. This configures
  // it such that there are 2 max simultaneous tab loads, and 3 maximum tabs to
  // restore.
  std::map<std::string, std::string> params;
  params[rc::kInfiniteSessionRestore_MinSimultaneousTabLoads] = "2";
  params[rc::kInfiniteSessionRestore_MaxSimultaneousTabLoads] = "2";
  params[rc::kInfiniteSessionRestore_CoresPerSimultaneousTabLoad] = "0";
  params[rc::kInfiniteSessionRestore_MinTabsToRestore] = "1";
  params[rc::kInfiniteSessionRestore_MaxTabsToRestore] = "3";

  // Disable these policy features.
  params[rc::kInfiniteSessionRestore_MbFreeMemoryPerTabToRestore] = "0";
  params[rc::kInfiniteSessionRestore_MaxTimeSinceLastUseToRestore] = "0";
  params[rc::kInfiniteSessionRestore_MinSiteEngagementToRestore] = "0";

  variations::testing::VariationParamsManager variations_manager;
  variations_manager.SetVariationParamsWithFeatureAssociations(
      "DummyTrial", params, features);

  // Don't directly configure the max simultaneous loads, but rather let it be
  // configured via the policy engine.
  max_simultaneous_loads_ = 0;

  // Create 5 tabs to restore, 1 foreground and 4 background.
  CreateMultipleRestoredWebContents(1, 4);

  // Create the tab loader. This should initially start loading 1 tab, due to
  // exclusive initial loading of active tabs.
  TabLoader::RestoreTabs(restored_tabs_, clock_.NowTicks());
  EXPECT_EQ(4u, tab_loader_.tabs_to_load().size());
  EXPECT_EQ(1u, tab_loader_.scheduled_to_load_count());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());

  // Simulate the first tab as having loaded. Another 2 should start loading.
  SimulateLoaded(0);
  EXPECT_EQ(2u, tab_loader_.tabs_to_load().size());
  EXPECT_EQ(3u, tab_loader_.scheduled_to_load_count());
  EXPECT_TRUE(tab_loader_.IsSharedTabLoader());

  // Simulate another tab as having loaded. The last 2 tabs should be deferred
  // (still need reloads) and the tab loader should detach.
  SimulateLoaded(1);
  EXPECT_TRUE(restored_tabs_[3].contents()->GetController().NeedsReload());
  EXPECT_TRUE(restored_tabs_[4].contents()->GetController().NeedsReload());
  EXPECT_TRUE(TabLoaderTester::shared_tab_loader() == nullptr);
}
