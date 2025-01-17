// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/display/manager/display_util.h"

#include <vector>

#include "base/macros.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/display/manager/managed_display_info.h"
#include "ui/gfx/geometry/size.h"

namespace display {
namespace test {

namespace {
constexpr std::size_t kNumOfZoomFactors = 9;
using ZoomListBucket = std::pair<int, std::array<float, kNumOfZoomFactors>>;

}  // namespace
using DisplayUtilTest = testing::Test;

TEST_F(DisplayUtilTest, DisplayZooms) {
  // The expected zoom list for the width given by |first| of the pair should be
  //  equal to the |second| of the pair.
  constexpr std::array<ZoomListBucket, 4> kTestData{{
      {240, {0.60f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.f}},
      {720, {0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.f, 1.05f, 1.10f}},
      {1024, {0.90f, 0.95f, 1.f, 1.05f, 1.10f, 1.15f, 1.20f, 1.25f, 1.30f}},
      {2400, {1.f, 1.10f, 1.15f, 1.20f, 1.30f, 1.40f, 1.50f, 1.75f, 2.00f}},
  }};
  for (const auto& data : kTestData) {
    ManagedDisplayMode mode(gfx::Size(data.first, data.first), 60, false, true,
                            1.f, 1.f);
    const std::vector<float> zoom_values = GetDisplayZoomFactors(mode);
    for (std::size_t j = 0; j < kNumOfZoomFactors; j++)
      EXPECT_FLOAT_EQ(zoom_values[j], data.second[j]);
  }
}

TEST_F(DisplayUtilTest, DisplayZoomsWithInternalDsf) {
  const std::vector<std::pair<float, std::vector<float>>> expected_zoom_values{
      {1.25f, {1.f / 1.25f, 0.85f, 0.9f, 0.95f, 1.f, 1.05f, 1.1f, 1.15f, 1.2f}},
      {1.5f, {1.f / 1.5f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.f, 1.05f}},
      {1.6f, {1.f / 1.6f, 0.65f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.f}},
      {1.8f, {1.f / 1.8f, 0.6f, 0.7f, 0.8f, 0.9f, 1.f, 1.1f, 1.2f, 1.3f}},
      {2.f, {1.f / 2.f, 0.6f, 0.7f, 0.8f, 0.9f, 1.f, 1.1f, 1.2f, 1.3f}},
      {2.25f, {1.f / 2.25f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.f, 1.1f, 1.2f}}};

  for (const auto& pair : expected_zoom_values) {
    const std::vector<float> zoom_values =
        GetDisplayZoomFactorForDsf(pair.first);
    for (std::size_t j = 0; j < kNumOfZoomFactors; j++)
      EXPECT_FLOAT_EQ(zoom_values[j], pair.second[j]);
  }
}

TEST_F(DisplayUtilTest, InsertDsfIntoListLessThanUnity) {
  // list[0] -> actual
  // list[1] -> expected
  std::vector<float> list[2];
  float dsf;

  dsf = 0.6f;
  list[0] = {0.6f, 0.65f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.f};
  list[1] = {dsf, 0.65f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.f};
  InsertDsfIntoList(&list[0], dsf);
  EXPECT_EQ(list[1].size(), kNumOfZoomFactors);
  EXPECT_EQ(list[0], list[1]);

  dsf = 0.6f;
  list[0] = {0.65f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.f, 1.05f};
  list[1] = {dsf, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.f, 1.05f};
  InsertDsfIntoList(&list[0], dsf);
  EXPECT_EQ(list[1].size(), kNumOfZoomFactors);
  EXPECT_EQ(list[0], list[1]);

  dsf = 0.67f;
  list[0] = {0.6f, 0.7f, 0.8f, 0.9f, 1.f, 1.1f, 1.2f, 1.3f, 1.4f};
  list[1] = {0.6f, dsf, 0.8f, 0.9f, 1.f, 1.1f, 1.2f, 1.3f, 1.4f};
  InsertDsfIntoList(&list[0], dsf);
  EXPECT_EQ(list[1].size(), kNumOfZoomFactors);
  EXPECT_EQ(list[0], list[1]);

  dsf = 0.9f;
  list[0] = {0.6f, 0.7f, 0.8f, 0.9f, 1.f, 1.1f, 1.2f, 1.3f, 1.4f};
  list[1] = {0.6f, 0.7f, 0.8f, dsf, 1.f, 1.1f, 1.2f, 1.3f, 1.4f};
  InsertDsfIntoList(&list[0], dsf);
  EXPECT_EQ(list[1].size(), kNumOfZoomFactors);
  EXPECT_EQ(list[0], list[1]);

  dsf = 0.99f;
  list[0] = {0.6f, 0.7f, 0.8f, 0.9f, 1.f, 1.1f, 1.2f, 1.3f, 1.4f};
  list[1] = {0.6f, 0.7f, 0.8f, dsf, 1.f, 1.1f, 1.2f, 1.3f, 1.4f};
  InsertDsfIntoList(&list[0], dsf);
  EXPECT_EQ(list[1].size(), kNumOfZoomFactors);
  EXPECT_EQ(list[0], list[1]);

  dsf = 0.99f;
  list[0] = {0.8f, 1.f, 1.2f, 1.4f, 1.6f, 1.8f, 2.f, 2.2f, 2.4f};
  list[1] = {dsf, 1.f, 1.2f, 1.4f, 1.6f, 1.8f, 2.f, 2.2f, 2.4f};
  InsertDsfIntoList(&list[0], dsf);
  EXPECT_EQ(list[1].size(), kNumOfZoomFactors);
  EXPECT_EQ(list[0], list[1]);

  dsf = 0.85f;
  list[0] = {1.f, 1.25f, 1.5f, 1.75f, 2.f, 2.25f, 2.5f, 2.75f, 3.f};
  list[1] = {dsf, 1.f, 1.25f, 1.5f, 1.75f, 2.f, 2.25f, 2.5f, 2.75f};
  InsertDsfIntoList(&list[0], dsf);
  EXPECT_EQ(list[1].size(), kNumOfZoomFactors);
  EXPECT_EQ(list[0], list[1]);
}

TEST_F(DisplayUtilTest, InsertDsfIntoListGreaterThanUnity) {
  // list[0] -> actual
  // list[1] -> expected
  std::vector<float> list[2];
  float dsf;

  dsf = 1.f;
  list[0] = {0.6f, 0.65f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.f};
  list[1] = {0.6f, 0.65f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.f};
  InsertDsfIntoList(&list[0], dsf);
  EXPECT_EQ(list[1].size(), kNumOfZoomFactors);
  EXPECT_EQ(list[0], list[1]);

  dsf = 1.1f;
  list[0] = {0.65f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.f, 1.05f};
  list[1] = {0.65f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.f, dsf};
  InsertDsfIntoList(&list[0], dsf);
  EXPECT_EQ(list[1].size(), kNumOfZoomFactors);
  EXPECT_EQ(list[0], list[1]);

  dsf = 1.1f;
  list[0] = {0.6f, 0.65f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.f};
  list[1] = {0.65f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.f, dsf};
  InsertDsfIntoList(&list[0], dsf);
  EXPECT_EQ(list[1].size(), kNumOfZoomFactors);
  EXPECT_EQ(list[0], list[1]);

  dsf = 1.01f;
  list[0] = {0.6f, 0.7f, 0.8f, 0.9f, 1.f, 1.1f, 1.2f, 1.3f, 1.4f};
  list[1] = {0.6f, 0.7f, 0.8f, 0.9f, 1.f, dsf, 1.2f, 1.3f, 1.4f};
  InsertDsfIntoList(&list[0], dsf);
  EXPECT_EQ(list[1].size(), kNumOfZoomFactors);
  EXPECT_EQ(list[0], list[1]);

  dsf = 1.1f;
  list[0] = {0.6f, 0.7f, 0.8f, 0.9f, 1.f, 1.1f, 1.2f, 1.3f, 1.4f};
  list[1] = {0.6f, 0.7f, 0.8f, 0.9f, 1.f, dsf, 1.2f, 1.3f, 1.4f};
  InsertDsfIntoList(&list[0], dsf);
  EXPECT_EQ(list[1].size(), kNumOfZoomFactors);
  EXPECT_EQ(list[0], list[1]);

  dsf = 1.13f;
  list[0] = {0.6f, 0.7f, 0.8f, 0.9f, 1.f, 1.1f, 1.2f, 1.3f, 1.4f};
  list[1] = {0.6f, 0.7f, 0.8f, 0.9f, 1.f, dsf, 1.2f, 1.3f, 1.4f};
  InsertDsfIntoList(&list[0], dsf);
  EXPECT_EQ(list[1].size(), kNumOfZoomFactors);
  EXPECT_EQ(list[0], list[1]);

  dsf = 1.17f;
  list[0] = {0.6f, 0.7f, 0.8f, 0.9f, 1.f, 1.1f, 1.2f, 1.3f, 1.4f};
  list[1] = {0.6f, 0.7f, 0.8f, 0.9f, 1.f, 1.1f, dsf, 1.3f, 1.4f};
  InsertDsfIntoList(&list[0], dsf);
  EXPECT_EQ(list[1].size(), kNumOfZoomFactors);
  EXPECT_EQ(list[0], list[1]);

  dsf = 1.1f;
  list[0] = {1.f, 1.25f, 1.5f, 1.75f, 2.f, 2.25f, 2.5f, 2.75f, 3.f};
  list[1] = {1.f, dsf, 1.5f, 1.75f, 2.f, 2.25f, 2.5f, 2.75f, 3.f};
  InsertDsfIntoList(&list[0], dsf);
  EXPECT_EQ(list[1].size(), kNumOfZoomFactors);
  EXPECT_EQ(list[0], list[1]);
}

}  // namespace test
}  // namespace display
