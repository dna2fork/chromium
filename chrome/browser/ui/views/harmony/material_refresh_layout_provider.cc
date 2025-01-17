// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/harmony/material_refresh_layout_provider.h"

#include "ui/base/material_design/material_design_controller.h"
#include "ui/views/layout/layout_provider.h"

int MaterialRefreshLayoutProvider::GetDistanceMetric(int metric) const {
  switch (metric) {
    case views::DistanceMetric::DISTANCE_CONTROL_VERTICAL_TEXT_PADDING:
      return 8;
  }
  return HarmonyLayoutProvider::GetDistanceMetric(metric);
}

gfx::Insets MaterialRefreshLayoutProvider::GetInsetsMetric(int metric) const {
  switch (metric) {
    case INSETS_BOOKMARKS_BAR_BUTTON:
      // The paddings here are chosen so that the resulting inkdrops are
      // circular for favicon-only bookmarks.
      if (ui::MaterialDesignController::IsTouchOptimizedUiEnabled())
        return gfx::Insets(8, 10);
      return gfx::Insets(5, 6);
  }
  return HarmonyLayoutProvider::GetInsetsMetric(metric);
}

int MaterialRefreshLayoutProvider::GetCornerRadiusMetric(
    views::EmphasisMetric emphasis_metric,
    const gfx::Size& size) const {
  switch (emphasis_metric) {
    case views::EMPHASIS_NONE:
      NOTREACHED();
      return 0;
    case views::EMPHASIS_LOW:
    case views::EMPHASIS_MEDIUM:
      return 4;
    case views::EMPHASIS_HIGH:
      return 8;
    case views::EMPHASIS_MAXIMUM:
      return std::min(size.width(), size.height()) / 2;
  }
}

int MaterialRefreshLayoutProvider::GetShadowElevationMetric(
    views::EmphasisMetric emphasis_metric) const {
  switch (emphasis_metric) {
    case views::EMPHASIS_NONE:
      NOTREACHED();
      return 0;
    case views::EMPHASIS_LOW:
      return 1;
    case views::EMPHASIS_MEDIUM:
      return 2;
    case views::EMPHASIS_HIGH:
      return 3;
    case views::EMPHASIS_MAXIMUM:
      return 16;
  }
}

gfx::ShadowValues MaterialRefreshLayoutProvider::MakeShadowValues(
    int elevation) const {
  return gfx::ShadowValue::MakeRefreshShadowValues(elevation);
}
