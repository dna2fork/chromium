// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/macros.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/fullscreen_keyboard_browsertest_base.h"
#include "chrome/browser/ui/views_mode_controller.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "ui/events/keycodes/keyboard_codes.h"

#if defined(OS_MACOSX)
#include "base/mac/mac_util.h"
#endif

using BrowserCommandControllerInteractiveTest =
    FullscreenKeyboardBrowserTestBase;

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerInteractiveTest,
                       ShortcutsShouldTakeEffectInWindowMode) {
  ASSERT_EQ(1, GetTabCount());
  ASSERT_NO_FATAL_FAILURE(SendShortcut(ui::VKEY_T));
  ASSERT_EQ(2, GetTabCount());
  ASSERT_NO_FATAL_FAILURE(SendShortcut(ui::VKEY_T));
  ASSERT_EQ(3, GetTabCount());
  ASSERT_NO_FATAL_FAILURE(SendShortcut(ui::VKEY_W));
  ASSERT_EQ(2, GetTabCount());
  ASSERT_NO_FATAL_FAILURE(SendShortcut(ui::VKEY_W));
  ASSERT_EQ(1, GetTabCount());
  ASSERT_NO_FATAL_FAILURE(SendFullscreenShortcutAndWait());
  ASSERT_TRUE(IsInBrowserFullscreen());
  ASSERT_FALSE(IsActiveTabFullscreen());
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerInteractiveTest,
                       UnpreservedShortcutsShouldBePreventable) {
  ASSERT_NO_FATAL_FAILURE(StartFullscreenLockPage());

  // The browser print function should be blocked by the web page.
  ASSERT_NO_FATAL_FAILURE(SendShortcut(ui::VKEY_P));
  // The system print function should be blocked by the web page.
  ASSERT_NO_FATAL_FAILURE(SendShiftShortcut(ui::VKEY_P));
  ASSERT_NO_FATAL_FAILURE(FinishTestAndVerifyResult());
}

#if defined(OS_MACOSX)
// TODO(zijiehe): Figure out why this test crashes on Mac OSX. The suspicious
// command is "SendFullscreenShortcutAndWait()". See, http://crbug.com/738949.
#define MAYBE_KeyEventsShouldBeConsumedByWebPageInBrowserFullscreen \
  DISABLED_KeyEventsShouldBeConsumedByWebPageInBrowserFullscreen
#else
#define MAYBE_KeyEventsShouldBeConsumedByWebPageInBrowserFullscreen \
  KeyEventsShouldBeConsumedByWebPageInBrowserFullscreen
#endif
IN_PROC_BROWSER_TEST_F(
    BrowserCommandControllerInteractiveTest,
    MAYBE_KeyEventsShouldBeConsumedByWebPageInBrowserFullscreen) {
  ASSERT_NO_FATAL_FAILURE(StartFullscreenLockPage());

  ASSERT_NO_FATAL_FAILURE(SendFullscreenShortcutAndWait());
  ASSERT_FALSE(IsActiveTabFullscreen());
  ASSERT_TRUE(IsInBrowserFullscreen());
  ASSERT_NO_FATAL_FAILURE(SendShortcutsAndExpectPrevented());
  // Current page should not exit browser fullscreen mode.
  ASSERT_NO_FATAL_FAILURE(SendEscape());

  ASSERT_NO_FATAL_FAILURE(FinishTestAndVerifyResult());

  ASSERT_NO_FATAL_FAILURE(SendFullscreenShortcutAndWait());
  ASSERT_FALSE(IsActiveTabFullscreen());
  ASSERT_FALSE(IsInBrowserFullscreen());
}

IN_PROC_BROWSER_TEST_F(
    BrowserCommandControllerInteractiveTest,
    KeyEventsShouldBeConsumedByWebPageInJsFullscreenExceptForEsc) {
#if defined(OS_MACOSX)
  // https://crbug.com/850594
  if (!views_mode_controller::IsViewsBrowserCocoa())
    return;
#endif
  ASSERT_NO_FATAL_FAILURE(StartFullscreenLockPage());

  ASSERT_NO_FATAL_FAILURE(SendJsFullscreenShortcutAndWait());
  ASSERT_NO_FATAL_FAILURE(SendShortcutsAndExpectPrevented());
  // Current page should exit HTML fullscreen mode.
  ASSERT_NO_FATAL_FAILURE(SendEscapeAndWaitForExitingFullscreen());

  ASSERT_NO_FATAL_FAILURE(FinishTestAndVerifyResult());
}

#if defined(OS_MACOSX)
// Triggers a DCHECK in MacViews: http://crbug.com/823478
#define MAYBE_KeyEventsShouldBeConsumedByWebPageInJsFullscreenExceptForF11 \
    DISABLED_KeyEventsShouldBeConsumedByWebPageInJsFullscreenExceptForF11
#else
#define MAYBE_KeyEventsShouldBeConsumedByWebPageInJsFullscreenExceptForF11 \
    KeyEventsShouldBeConsumedByWebPageInJsFullscreenExceptForF11
#endif
IN_PROC_BROWSER_TEST_F(
    BrowserCommandControllerInteractiveTest,
    MAYBE_KeyEventsShouldBeConsumedByWebPageInJsFullscreenExceptForF11) {
  ASSERT_NO_FATAL_FAILURE(StartFullscreenLockPage());

  ASSERT_NO_FATAL_FAILURE(SendJsFullscreenShortcutAndWait());
  ASSERT_NO_FATAL_FAILURE(SendShortcutsAndExpectPrevented());
#if defined(OS_MACOSX)
  // On 10.9 or earlier, sending the exit fullscreen shortcut will crash the
  // binary. See http://crbug.com/740250.
  if (base::mac::IsAtLeastOS10_10()) {
    // Current page should exit browser fullscreen mode.
    ASSERT_NO_FATAL_FAILURE(SendFullscreenShortcutAndWait());
    ASSERT_FALSE(IsActiveTabFullscreen());
    ASSERT_FALSE(IsInBrowserFullscreen());
  }
#else
  // Current page should exit browser fullscreen mode.
  ASSERT_NO_FATAL_FAILURE(SendFullscreenShortcutAndWait());
  ASSERT_FALSE(IsActiveTabFullscreen());
  ASSERT_FALSE(IsInBrowserFullscreen());
#endif

  ASSERT_NO_FATAL_FAILURE(FinishTestAndVerifyResult());
}

#if defined(OS_MACOSX)
// TODO(zijiehe): Figure out why this test crashes on Mac OSX. The suspicious
// command is "SendFullscreenShortcutAndWait()". See, http://crbug.com/738949.
#define MAYBE_ShortcutsShouldTakeEffectInBrowserFullscreen \
        DISABLED_ShortcutsShouldTakeEffectInBrowserFullscreen
#else
#define MAYBE_ShortcutsShouldTakeEffectInBrowserFullscreen \
        ShortcutsShouldTakeEffectInBrowserFullscreen
#endif
IN_PROC_BROWSER_TEST_F(BrowserCommandControllerInteractiveTest,
                       MAYBE_ShortcutsShouldTakeEffectInBrowserFullscreen) {
#if defined(OS_MACOSX)
  // On 10.9 or earlier, sending the exit fullscreen shortcut will crash the
  // binary. See http://crbug.com/740250.
  if (base::mac::IsAtMostOS10_9())
    return;
#endif
  ASSERT_NO_FATAL_FAILURE(SendShortcutsAndExpectNotPrevented(false));
}

#if !defined(OS_MACOSX)
// HTML fullscreen is automatically exited after some commands are executed,
// such as Ctrl + T (new tab). But some commands won't have this effect, such as
// Ctrl + N (new window).
// On Mac OSX, AppKit implementation is used for HTML fullscreen mode. Entering
// and exiting AppKit fullscreen mode triggers an animation. A
// FullscreenChangeObserver is needed to ensure the animation is finished. But
// the FullscreenChangeObserver won't finish if the command actually won't cause
// the page to exit fullscreen mode. So we need to maintain a list of exiting /
// non-exiting commands, which is not the goal of this test.

#if defined(OS_CHROMEOS)
// This test is flaky on ChromeOS, see http://crbug.com/754878.
#define MAYBE_ShortcutsShouldTakeEffectInJsFullscreen \
        DISABLED_ShortcutsShouldTakeEffectInJsFullscreen
#else
#define MAYBE_ShortcutsShouldTakeEffectInJsFullscreen \
        ShortcutsShouldTakeEffectInJsFullscreen
#endif
IN_PROC_BROWSER_TEST_F(BrowserCommandControllerInteractiveTest,
                       MAYBE_ShortcutsShouldTakeEffectInJsFullscreen) {
// This test is flaky. See http://crbug.com/759704.
// TODO(zijiehe): Find out the root cause.
#if defined(OS_LINUX)
  return;
#endif
  ASSERT_NO_FATAL_FAILURE(SendShortcutsAndExpectNotPrevented(true));
}

#endif
