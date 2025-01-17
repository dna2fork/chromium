// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_TRAY_DETAILED_VIEW_DELEGATE_H_
#define ASH_SYSTEM_TRAY_DETAILED_VIEW_DELEGATE_H_

#include "third_party/skia/include/core/SkColor.h"

namespace ui {
class NativeTheme;
}  // namespace ui

namespace views {
class Button;
class ButtonListener;
class View;
}  // namespace views

namespace ash {

class TriView;

// A delegate of TrayDetailedView that handles bubble related actions e.g.
// transition to the main view, closing the bubble, etc.
class DetailedViewDelegate {
 public:
  virtual ~DetailedViewDelegate() {}

  // Transition to the main view from the detailed view. |restore_focus| is true
  // if the title row has keyboard focus before transition. If so, the main view
  // should focus on the corresponding element of the detailed view.
  virtual void TransitionToMainView(bool restore_focus) = 0;

  // Close the bubble that contains the detailed view.
  virtual void CloseBubble() = 0;

  // Get the background color of the detailed view.
  virtual SkColor GetBackgroundColor(ui::NativeTheme* native_theme) = 0;

  // Return true if overflow indicator of ScrollView is enabled.
  virtual bool IsOverflowIndicatorEnabled() const = 0;

  // Return TriView used for the title row. It should have title label of
  // |string_id| in CENTER. TrayDetailedView will calls CreateBackButton() and
  // adds the returned view to START.
  virtual TriView* CreateTitleRow(int string_id) = 0;

  // Return the separator used between the title row and the contents. Caller
  // takes ownership of the returned view.
  virtual views::View* CreateTitleSeparator() = 0;

  // Return the back button used in the title row. Caller takes ownership of the
  // returned view.
  virtual views::Button* CreateBackButton(views::ButtonListener* listener) = 0;

  // Return the info button used in the title row. Caller takes ownership of the
  // returned view.
  virtual views::Button* CreateInfoButton(views::ButtonListener* listener,
                                          int info_accessible_name_id) = 0;

  // Return the settings button used in the title row. Caller takes ownership of
  // the returned view.
  virtual views::Button* CreateSettingsButton(
      views::ButtonListener* listener,
      int setting_accessible_name_id) = 0;

  // Return the help button used in the title row. Caller takes ownership of the
  // returned view.
  virtual views::Button* CreateHelpButton(views::ButtonListener* listener) = 0;
};

}  // namespace ash

#endif  // ASH_SYSTEM_TRAY_DETAILED_VIEW_DELEGATE_H_
