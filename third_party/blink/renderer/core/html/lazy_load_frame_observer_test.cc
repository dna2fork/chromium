// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/html/lazy_load_frame_observer.h"

#include <algorithm>
#include <memory>
#include <tuple>

#include "third_party/blink/public/platform/web_effective_connection_type.h"
#include "third_party/blink/renderer/core/exported/web_view_impl.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/testing/sim/sim_compositor.h"
#include "third_party/blink/renderer/core/testing/sim/sim_request.h"
#include "third_party/blink/renderer/core/testing/sim/sim_test.h"
#include "third_party/blink/renderer/platform/geometry/float_rect.h"
#include "third_party/blink/renderer/platform/testing/histogram_tester.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"
#include "third_party/blink/renderer/platform/testing/unit_test_helpers.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

namespace {

constexpr std::pair<WebEffectiveConnectionType, const char*>
    kVisibleLoadTimeAboveTheFoldHistogramNames[] = {
        {WebEffectiveConnectionType::kTypeSlow2G,
         "Blink.VisibleLoadTime.LazyLoadEligibleFrames.AboveTheFold.Slow2G"},
        {WebEffectiveConnectionType::kType2G,
         "Blink.VisibleLoadTime.LazyLoadEligibleFrames.AboveTheFold.2G"},
        {WebEffectiveConnectionType::kType3G,
         "Blink.VisibleLoadTime.LazyLoadEligibleFrames.AboveTheFold.3G"},
        {WebEffectiveConnectionType::kType4G,
         "Blink.VisibleLoadTime.LazyLoadEligibleFrames.AboveTheFold.4G"},
};

constexpr std::pair<WebEffectiveConnectionType, const char*>
    kVisibleLoadTimeBelowTheFoldHistogramNames[] = {
        {WebEffectiveConnectionType::kTypeSlow2G,
         "Blink.VisibleLoadTime.LazyLoadEligibleFrames.BelowTheFold.Slow2G"},
        {WebEffectiveConnectionType::kType2G,
         "Blink.VisibleLoadTime.LazyLoadEligibleFrames.BelowTheFold.2G"},
        {WebEffectiveConnectionType::kType3G,
         "Blink.VisibleLoadTime.LazyLoadEligibleFrames.BelowTheFold.3G"},
        {WebEffectiveConnectionType::kType4G,
         "Blink.VisibleLoadTime.LazyLoadEligibleFrames.BelowTheFold.4G"},
};

// Convenience enums to make it easy to access the appropriate value of the
// tuple parameters in the parameterized tests below, e.g. so that
// std::get<LazyFrameLoadingFeatureStatus>(GetParam()) can be used instead of
// std::get<0>(GetParam()) if they were just booleans.
enum class LazyFrameLoadingFeatureStatus { kDisabled, kEnabled };
enum class LazyFrameVisibleLoadTimeFeatureStatus { kDisabled, kEnabled };

class LazyLoadFramesTest : public SimTest,
                           public ::testing::WithParamInterface<
                               std::tuple<LazyFrameLoadingFeatureStatus,
                                          LazyFrameVisibleLoadTimeFeatureStatus,
                                          WebEffectiveConnectionType>> {
 public:
  static constexpr int kViewportWidth = 800;
  static constexpr int kViewportHeight = 600;

  LazyLoadFramesTest()
      : scoped_lazy_frame_loading_for_test_(
            std::get<LazyFrameLoadingFeatureStatus>(GetParam()) ==
            LazyFrameLoadingFeatureStatus::kEnabled),
        scoped_lazy_frame_visible_load_time_metrics_for_test_(
            std::get<LazyFrameVisibleLoadTimeFeatureStatus>(GetParam()) ==
            LazyFrameVisibleLoadTimeFeatureStatus::kEnabled) {}

  void SetUp() override {
    SetEffectiveConnectionTypeForTesting(
        std::get<WebEffectiveConnectionType>(GetParam()));

    SimTest::SetUp();
    WebView().Resize(WebSize(kViewportWidth, kViewportHeight));

    Settings& settings = WebView().GetPage()->GetSettings();

    // These should match the values that would be returned by
    // GetLoadingDistanceThreshold().
    settings.SetLazyFrameLoadingDistanceThresholdPxUnknown(200);
    settings.SetLazyFrameLoadingDistanceThresholdPxOffline(300);
    settings.SetLazyFrameLoadingDistanceThresholdPxSlow2G(400);
    settings.SetLazyFrameLoadingDistanceThresholdPx2G(500);
    settings.SetLazyFrameLoadingDistanceThresholdPx3G(600);
    settings.SetLazyFrameLoadingDistanceThresholdPx4G(700);
  }

  void ExpectVisibleLoadTimeHistogramSamplesIfApplicable(
      int expected_above_the_fold_count,
      int expected_below_the_fold_count) {
    if (std::get<LazyFrameVisibleLoadTimeFeatureStatus>(GetParam()) !=
        LazyFrameVisibleLoadTimeFeatureStatus::kEnabled) {
      // Expect zero samples if the visible load time metrics feature is
      // disabled.
      expected_above_the_fold_count = 0;
      expected_below_the_fold_count = 0;
    }

    for (const auto& pair : kVisibleLoadTimeAboveTheFoldHistogramNames) {
      histogram_tester()->ExpectTotalCount(
          pair.second,
          std::get<WebEffectiveConnectionType>(GetParam()) == pair.first
              ? expected_above_the_fold_count
              : 0);
    }
    for (const auto& pair : kVisibleLoadTimeBelowTheFoldHistogramNames) {
      histogram_tester()->ExpectTotalCount(
          pair.second,
          std::get<WebEffectiveConnectionType>(GetParam()) == pair.first
              ? expected_below_the_fold_count
              : 0);
    }
  }

  HistogramTester* histogram_tester() { return &histogram_tester_; }

  int GetLoadingDistanceThreshold() const {
    static constexpr int kDistanceThresholdByEffectiveConnectionType[] = {
        200, 300, 400, 500, 600, 700};
    return kDistanceThresholdByEffectiveConnectionType[static_cast<int>(
        std::get<WebEffectiveConnectionType>(GetParam()))];
  }

  // Convenience function to load a page with a cross origin frame far down the
  // page such that it's not near the viewport.
  std::unique_ptr<SimRequest> LoadPageWithCrossOriginFrameFarFromViewport() {
    SimRequest main_resource("https://example.com/", "text/html");
    std::unique_ptr<SimRequest> child_frame_resource;

    if (!RuntimeEnabledFeatures::LazyFrameLoadingEnabled()) {
      // This SimRequest needs to be created now if the frame won't actually be
      // lazily loaded. Otherwise, it'll be defined later to ensure that the
      // subframe resource isn't requested until the page is scrolled down.
      child_frame_resource.reset(
          new SimRequest("https://crossorigin.com/subframe.html", "text/html"));
    }

    LoadURL("https://example.com/");

    main_resource.Complete(String::Format(
        R"HTML(
          <body onload='console.log("main body onload");'>
          <div style='height: %dpx;'></div>
          <iframe src='https://crossorigin.com/subframe.html'
               style='width: 400px; height: 400px;'
               onload='console.log("child frame element onload");'></iframe>
          </body>)HTML",
        kViewportHeight + GetLoadingDistanceThreshold() + 100));

    Compositor().BeginFrame();
    test::RunPendingTasks();

    // If the child frame is being lazy loaded, then the body's load event
    // should have already fired.
    EXPECT_EQ(RuntimeEnabledFeatures::LazyFrameLoadingEnabled(),
              ConsoleMessages().Contains("main body onload"));
    EXPECT_FALSE(ConsoleMessages().Contains("child frame element onload"));

    ExpectVisibleLoadTimeHistogramSamplesIfApplicable(0, 0);

    if (!child_frame_resource) {
      child_frame_resource.reset(
          new SimRequest("https://crossorigin.com/subframe.html", "text/html"));
    }

    return child_frame_resource;
  }

 private:
  ScopedLazyFrameLoadingForTest scoped_lazy_frame_loading_for_test_;
  ScopedLazyFrameVisibleLoadTimeMetricsForTest
      scoped_lazy_frame_visible_load_time_metrics_for_test_;

  HistogramTester histogram_tester_;
};

TEST_P(LazyLoadFramesTest, SameOriginFrame) {
  SimRequest main_resource("https://example.com/", "text/html");
  SimRequest child_frame_resource("https://example.com/subframe.html",
                                  "text/html");

  LoadURL("https://example.com/");

  main_resource.Complete(String::Format(
      R"HTML(
        <body onload='console.log("main body onload");'>
        <div style='height: %dpx;'></div>
        <iframe src='https://example.com/subframe.html'
             style='width: 200px; height: 200px;'
             onload='console.log("child frame element onload");'></iframe>
        </body>)HTML",
      kViewportHeight + GetLoadingDistanceThreshold() + 100));

  Compositor().BeginFrame();
  test::RunPendingTasks();

  child_frame_resource.Complete("");

  Compositor().BeginFrame();
  test::RunPendingTasks();

  EXPECT_TRUE(ConsoleMessages().Contains("main body onload"));
  EXPECT_TRUE(ConsoleMessages().Contains("child frame element onload"));

  ExpectVisibleLoadTimeHistogramSamplesIfApplicable(0, 0);
  histogram_tester()->ExpectTotalCount(
      "Blink.VisibleBeforeLoaded.LazyLoadEligibleFrames.BelowTheFold", 0);
}

TEST_P(LazyLoadFramesTest, AboveTheFoldFrame) {
  SimRequest main_resource("https://example.com/", "text/html");
  SimRequest child_frame_resource("https://crossorigin.com/subframe.html",
                                  "text/html");

  LoadURL("https://example.com/");

  main_resource.Complete(String::Format(
      R"HTML(
        <body onload='console.log("main body onload");'>
        <div style='height: %dpx;'></div>
        <iframe src='https://crossorigin.com/subframe.html'
             style='width: 200px; height: 200px;'
             onload='console.log("child frame element onload");'></iframe>
        </body>)HTML",
      kViewportHeight - 100));

  Compositor().BeginFrame();
  test::RunPendingTasks();

  // The child frame is visible, but hasn't finished loading yet, so no visible
  // load time samples should have been recorded yet.
  ExpectVisibleLoadTimeHistogramSamplesIfApplicable(0, 0);

  child_frame_resource.Complete("");
  Compositor().BeginFrame();
  test::RunPendingTasks();

  EXPECT_TRUE(ConsoleMessages().Contains("main body onload"));
  EXPECT_TRUE(ConsoleMessages().Contains("child frame element onload"));

  ExpectVisibleLoadTimeHistogramSamplesIfApplicable(1, 0);
  histogram_tester()->ExpectTotalCount(
      "Blink.VisibleBeforeLoaded.LazyLoadEligibleFrames.BelowTheFold", 0);
}

TEST_P(LazyLoadFramesTest, BelowTheFoldButNearViewportFrame) {
  SimRequest main_resource("https://example.com/", "text/html");
  SimRequest child_frame_resource("https://crossorigin.com/subframe.html",
                                  "text/html");

  LoadURL("https://example.com/");

  main_resource.Complete(String::Format(
      R"HTML(
        <body onload='console.log("main body onload");'>
        <div style='height: %dpx;'></div>
        <iframe src='https://crossorigin.com/subframe.html'
             style='width: 200px; height: 200px;'
             onload='console.log("child frame element onload");'></iframe>
        </body>)HTML",
      kViewportHeight + 100));

  Compositor().BeginFrame();
  test::RunPendingTasks();

  child_frame_resource.Complete("");

  Compositor().BeginFrame();
  test::RunPendingTasks();

  EXPECT_TRUE(ConsoleMessages().Contains("main body onload"));
  EXPECT_TRUE(ConsoleMessages().Contains("child frame element onload"));

  // The frame is below the fold, but hasn't been scrolled down to yet, so there
  // should be no samples in any of the below the fold visible load time
  // histograms yet.
  ExpectVisibleLoadTimeHistogramSamplesIfApplicable(0, 0);

  // Scroll down until the child frame is visible.
  GetDocument().View()->LayoutViewportScrollableArea()->SetScrollOffset(
      ScrollOffset(0, 150), kProgrammaticScroll);

  Compositor().BeginFrame();
  test::RunPendingTasks();

  ExpectVisibleLoadTimeHistogramSamplesIfApplicable(0, 1);

  // The frame finished loading before it became visible, so there should be no
  // samples in the VisibleBeforeLoaded histogram.
  histogram_tester()->ExpectTotalCount(
      "Blink.VisibleBeforeLoaded.LazyLoadEligibleFrames.BelowTheFold", 0);
}

TEST_P(LazyLoadFramesTest, HiddenAndTinyFrames) {
  SimRequest main_resource("https://example.com/", "text/html");

  SimRequest display_none_frame_resource(
      "https://crossorigin.com/display_none.html", "text/html");
  SimRequest tiny_frame_resource("https://crossorigin.com/tiny.html",
                                 "text/html");
  SimRequest tiny_width_frame_resource(
      "https://crossorigin.com/tiny_width.html", "text/html");
  SimRequest tiny_height_frame_resource(
      "https://crossorigin.com/tiny_height.html", "text/html");
  SimRequest off_screen_left_frame_resource(
      "https://crossorigin.com/off_screen_left.html", "text/html");
  SimRequest off_screen_top_frame_resource(
      "https://crossorigin.com/off_screen_top.html", "text/html");

  LoadURL("https://example.com/");

  main_resource.Complete(String::Format(
      R"HTML(
        <head><style>
          /* Chrome by default sets borders for iframes, so explicitly specify
           * no borders, padding, or margins here so that the dimensions of the
           * tiny frames aren't artifically inflated past the dimensions that
           * the lazy loading logic considers "tiny". */
          iframe { border-style: none; padding: 0px; margin: 0px; }
        </style></head>

        <body onload='console.log("main body onload");'>
        <div style='height: %dpx'></div>
        <iframe src='https://crossorigin.com/display_none.html'
             style='display: none;'
             onload='console.log("display none element onload");'></iframe>
        <iframe src='https://crossorigin.com/tiny.html'
             style='width: 4px; height: 4px;'
             onload='console.log("tiny element onload");'></iframe>
        <iframe src='https://crossorigin.com/tiny_width.html'
             style='width: 0px; height: 50px;'
             onload='console.log("tiny width element onload");'></iframe>
        <iframe src='https://crossorigin.com/tiny_height.html'
             style='width: 50px; height: 0px;'
             onload='console.log("tiny height element onload");'></iframe>
        <iframe src='https://crossorigin.com/off_screen_left.html'
             style='position:relative;right:9000px;width:50px;height:50px;'
             onload='console.log("off screen left element onload");'></iframe>
        <iframe src='https://crossorigin.com/off_screen_top.html'
             style='position:relative;bottom:9000px;width:50px;height:50px;'
             onload='console.log("off screen top element onload");'></iframe>
        </body>
      )HTML",
      kViewportHeight + GetLoadingDistanceThreshold() + 100));

  Compositor().BeginFrame();
  test::RunPendingTasks();

  display_none_frame_resource.Complete("");
  tiny_frame_resource.Complete("");
  tiny_width_frame_resource.Complete("");
  tiny_height_frame_resource.Complete("");
  off_screen_left_frame_resource.Complete("");
  off_screen_top_frame_resource.Complete("");

  Compositor().BeginFrame();
  test::RunPendingTasks();

  EXPECT_TRUE(ConsoleMessages().Contains("main body onload"));
  EXPECT_TRUE(ConsoleMessages().Contains("display none element onload"));
  EXPECT_TRUE(ConsoleMessages().Contains("tiny element onload"));
  EXPECT_TRUE(ConsoleMessages().Contains("tiny width element onload"));
  EXPECT_TRUE(ConsoleMessages().Contains("tiny height element onload"));
  EXPECT_TRUE(ConsoleMessages().Contains("off screen left element onload"));
  EXPECT_TRUE(ConsoleMessages().Contains("off screen top element onload"));

  ExpectVisibleLoadTimeHistogramSamplesIfApplicable(0, 0);
  histogram_tester()->ExpectTotalCount(
      "Blink.VisibleBeforeLoaded.LazyLoadEligibleFrames.BelowTheFold", 0);

  // Scroll down to where the hidden frames are.
  GetDocument().View()->LayoutViewportScrollableArea()->SetScrollOffset(
      ScrollOffset(0, kViewportHeight + GetLoadingDistanceThreshold()),
      kProgrammaticScroll);

  // All of the frames on the page are hidden or tiny, so no visible load time
  // samples should have been recorded for them.
  ExpectVisibleLoadTimeHistogramSamplesIfApplicable(0, 0);
  histogram_tester()->ExpectTotalCount(
      "Blink.VisibleBeforeLoaded.LazyLoadEligibleFrames.BelowTheFold", 0);
}

TEST_P(LazyLoadFramesTest, LoadCrossOriginFrameFarFromViewport) {
  std::unique_ptr<SimRequest> child_frame_resource =
      LoadPageWithCrossOriginFrameFarFromViewport();

  if (RuntimeEnabledFeatures::LazyFrameLoadingEnabled()) {
    // If LazyFrameLoading is enabled, then scroll down near the child frame to
    // cause the child frame to start loading.
    GetDocument().View()->LayoutViewportScrollableArea()->SetScrollOffset(
        ScrollOffset(0, 150), kProgrammaticScroll);

    Compositor().BeginFrame();
    test::RunPendingTasks();
  }

  EXPECT_FALSE(ConsoleMessages().Contains("child frame element onload"));
  ExpectVisibleLoadTimeHistogramSamplesIfApplicable(0, 0);

  child_frame_resource->Complete("");

  Compositor().BeginFrame();
  test::RunPendingTasks();

  EXPECT_TRUE(ConsoleMessages().Contains("main body onload"));
  EXPECT_TRUE(ConsoleMessages().Contains("child frame element onload"));
  ExpectVisibleLoadTimeHistogramSamplesIfApplicable(0, 0);

  // Scroll down so that the child frame is visible.
  GetDocument().View()->LayoutViewportScrollableArea()->SetScrollOffset(
      ScrollOffset(0, GetLoadingDistanceThreshold() + 150),
      kProgrammaticScroll);

  Compositor().BeginFrame();
  test::RunPendingTasks();

  EXPECT_TRUE(ConsoleMessages().Contains("main body onload"));
  EXPECT_TRUE(ConsoleMessages().Contains("child frame element onload"));
  ExpectVisibleLoadTimeHistogramSamplesIfApplicable(0, 1);

  histogram_tester()->ExpectTotalCount(
      "Blink.VisibleBeforeLoaded.LazyLoadEligibleFrames.BelowTheFold", 0);
}

TEST_P(LazyLoadFramesTest,
       CrossOriginFrameFarFromViewportBecomesVisibleBeforeFinishedLoading) {
  std::unique_ptr<SimRequest> child_frame_resource =
      LoadPageWithCrossOriginFrameFarFromViewport();

  // Scroll down so that the child frame is visible.
  GetDocument().View()->LayoutViewportScrollableArea()->SetScrollOffset(
      ScrollOffset(0, GetLoadingDistanceThreshold() + 150),
      kProgrammaticScroll);

  Compositor().BeginFrame();
  test::RunPendingTasks();

  EXPECT_FALSE(ConsoleMessages().Contains("child frame element onload"));
  ExpectVisibleLoadTimeHistogramSamplesIfApplicable(0, 0);

  if (RuntimeEnabledFeatures::LazyFrameVisibleLoadTimeMetricsEnabled()) {
    // Even though the child frame hasn't loaded yet, a sample should still have
    // been recorded for VisibleBeforeLoaded.
    histogram_tester()->ExpectUniqueSample(
        "Blink.VisibleBeforeLoaded.LazyLoadEligibleFrames.BelowTheFold",
        static_cast<int>(std::get<WebEffectiveConnectionType>(GetParam())), 1);
  } else {
    histogram_tester()->ExpectTotalCount(
        "Blink.VisibleBeforeLoaded.LazyLoadEligibleFrames.BelowTheFold", 0);
  }

  child_frame_resource->Complete("");

  Compositor().BeginFrame();
  test::RunPendingTasks();

  EXPECT_TRUE(ConsoleMessages().Contains("main body onload"));
  EXPECT_TRUE(ConsoleMessages().Contains("child frame element onload"));
  ExpectVisibleLoadTimeHistogramSamplesIfApplicable(0, 1);

  // The samples recorded for VisibleBeforeLoaded should be unchanged.
  histogram_tester()->ExpectTotalCount(
      "Blink.VisibleBeforeLoaded.LazyLoadEligibleFrames.BelowTheFold",
      RuntimeEnabledFeatures::LazyFrameVisibleLoadTimeMetricsEnabled() ? 1 : 0);
}

TEST_P(LazyLoadFramesTest, NestedFrameInCrossOriginFrameFarFromViewport) {
  std::unique_ptr<SimRequest> child_frame_resource =
      LoadPageWithCrossOriginFrameFarFromViewport();

  if (RuntimeEnabledFeatures::LazyFrameLoadingEnabled()) {
    // If LazyFrameLoading is enabled, then scroll down near the child frame to
    // cause the child frame to start loading.
    GetDocument().View()->LayoutViewportScrollableArea()->SetScrollOffset(
        ScrollOffset(0, 150), kProgrammaticScroll);

    Compositor().BeginFrame();
    test::RunPendingTasks();
  }

  // There's another nested cross origin iframe inside the first child frame,
  // even further down such that it's not near the viewport. It should start
  // loading immediately, even if LazyFrameLoading is enabled, since it's nested
  // inside a frame that was previously deferred.
  SimRequest nested_frame_resource("https://test.com/", "text/html");
  child_frame_resource->Complete(String::Format(
      "<div style='height: %dpx;'></div>"
      "<iframe src='https://test.com/' style='width: 200px; height: 200px;'>"
      "</iframe>",
      kViewportHeight + GetLoadingDistanceThreshold() + 100));

  nested_frame_resource.Complete("");

  Compositor().BeginFrame();
  test::RunPendingTasks();

  EXPECT_TRUE(ConsoleMessages().Contains("main body onload"));
  EXPECT_TRUE(ConsoleMessages().Contains("child frame element onload"));

  // The child frame isn't visible, so no visible load time samples should have
  // been recorded.
  ExpectVisibleLoadTimeHistogramSamplesIfApplicable(0, 0);
  histogram_tester()->ExpectTotalCount(
      "Blink.VisibleBeforeLoaded.LazyLoadEligibleFrames.BelowTheFold", 0);
}

TEST_P(LazyLoadFramesTest, AboutBlankChildFrameNavigation) {
  SimRequest main_resource("https://example.com/", "text/html");
  SimRequest child_frame_resource("https://crossorigin.com/subframe.html",
                                  "text/html");

  LoadURL("https://example.com/");

  main_resource.Complete(String::Format(
      R"HTML(
        <body onload='BodyOnload()'>
        <script>
          function BodyOnload() {
            console.log('main body onload');
            document.getElementsByTagName('iframe')[0].src =
                'https://crossorigin.com/subframe.html';
          }
        </script>

        <div style='height: %dpx;'></div>
        <iframe
             style='width: 200px; height: 200px;'
             onload='console.log("child frame element onload");'></iframe>
        </body>)HTML",
      kViewportHeight + GetLoadingDistanceThreshold() + 100));

  Compositor().BeginFrame();
  test::RunPendingTasks();

  EXPECT_TRUE(ConsoleMessages().Contains("main body onload"));
  EXPECT_EQ(1, static_cast<int>(std::count(ConsoleMessages().begin(),
                                           ConsoleMessages().end(),
                                           "child frame element onload")));

  child_frame_resource.Complete("");

  Compositor().BeginFrame();
  test::RunPendingTasks();

  EXPECT_EQ(2, static_cast<int>(std::count(ConsoleMessages().begin(),
                                           ConsoleMessages().end(),
                                           "child frame element onload")));

  ExpectVisibleLoadTimeHistogramSamplesIfApplicable(0, 0);
  histogram_tester()->ExpectTotalCount(
      "Blink.VisibleBeforeLoaded.LazyLoadEligibleFrames.BelowTheFold", 0);
}

TEST_P(LazyLoadFramesTest, JavascriptStringFrameUrl) {
  SimRequest main_resource("https://example.com/", "text/html");
  LoadURL("https://example.com/");

  main_resource.Complete(String::Format(
      R"HTML(
        <body onload='console.log("main body onload");'>
        <div style='height: %dpx;'></div>
        <iframe src='javascript:"Hello World!";'
             style='width: 200px; height: 200px;'
             onload='console.log("child frame element onload");'></iframe>
        </body>)HTML",
      kViewportHeight + GetLoadingDistanceThreshold() + 100));

  EXPECT_TRUE(ConsoleMessages().Contains("main body onload"));
  EXPECT_TRUE(ConsoleMessages().Contains("child frame element onload"));

  Compositor().BeginFrame();
  test::RunPendingTasks();

  ExpectVisibleLoadTimeHistogramSamplesIfApplicable(0, 0);
  histogram_tester()->ExpectTotalCount(
      "Blink.VisibleBeforeLoaded.LazyLoadEligibleFrames.BelowTheFold", 0);
}

INSTANTIATE_TEST_CASE_P(
    LazyFrameLoading,
    LazyLoadFramesTest,
    ::testing::Combine(
        ::testing::Values(LazyFrameLoadingFeatureStatus::kDisabled,
                          LazyFrameLoadingFeatureStatus::kEnabled),
        ::testing::Values(LazyFrameVisibleLoadTimeFeatureStatus::kDisabled,
                          LazyFrameVisibleLoadTimeFeatureStatus::kEnabled),
        ::testing::Values(WebEffectiveConnectionType::kTypeUnknown,
                          WebEffectiveConnectionType::kTypeOffline,
                          WebEffectiveConnectionType::kTypeSlow2G,
                          WebEffectiveConnectionType::kType2G,
                          WebEffectiveConnectionType::kType3G,
                          WebEffectiveConnectionType::kType4G)));

}  // namespace

}  // namespace blink
