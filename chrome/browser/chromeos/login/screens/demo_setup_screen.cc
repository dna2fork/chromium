// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/screens/demo_setup_screen.h"

#include "chrome/browser/chromeos/login/screen_manager.h"
#include "chrome/browser/chromeos/login/screens/base_screen_delegate.h"
#include "chrome/browser/chromeos/policy/enrollment_config.h"

namespace {

constexpr const char kUserActionOnlineSetup[] = "online-setup";
constexpr const char kUserActionOfflineSetup[] = "offline-setup";
constexpr const char kUserActionClose[] = "close-setup";

}  // namespace

namespace chromeos {

DemoSetupScreen::DemoSetupScreen(BaseScreenDelegate* base_screen_delegate,
                                 DemoSetupScreenView* view)
    : BaseScreen(base_screen_delegate, OobeScreen::SCREEN_OOBE_DEMO_SETUP),
      view_(view) {
  DCHECK(view_);
  view_->Bind(this);
  demo_controller_.reset(new DemoSetupController(this));
}

DemoSetupScreen::~DemoSetupScreen() {
  if (view_)
    view_->Bind(nullptr);
}

void DemoSetupScreen::Show() {
  if (view_)
    view_->Show();
}

void DemoSetupScreen::Hide() {
  if (view_)
    view_->Hide();
}

void DemoSetupScreen::OnUserAction(const std::string& action_id) {
  if (action_id == kUserActionOnlineSetup) {
    demo_controller_->EnrollOnline();
  } else if (action_id == kUserActionOfflineSetup) {
    demo_controller_->EnrollOffline();
  } else if (action_id == kUserActionClose) {
    Finish(ScreenExitCode::DEMO_MODE_SETUP_CANCELED);
  } else {
    BaseScreen::OnUserAction(action_id);
  }
}

void DemoSetupScreen::OnSetupError() {
  NOTIMPLEMENTED();
}

void DemoSetupScreen::OnSetupSuccess() {
  Finish(ScreenExitCode::DEMO_MODE_SETUP_FINISHED);
}

void DemoSetupScreen::OnViewDestroyed(DemoSetupScreenView* view) {
  if (view_ == view)
    view_ = nullptr;
  demo_controller_.reset();
}

}  // namespace chromeos
