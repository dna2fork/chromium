// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_COMMON_PASSWORD_GENERATION_UTIL_H_
#define COMPONENTS_AUTOFILL_CORE_COMMON_PASSWORD_GENERATION_UTIL_H_

#include "components/autofill/core/common/password_form.h"
#include "ui/gfx/geometry/rect_f.h"

namespace autofill {

namespace password_generation {

// Enumerates various events related to the password generation process.
// Do not remove items from this enum as they are used for UMA stats logging.
enum PasswordGenerationEvent {
  // No Account creation form is detected.
  NO_SIGN_UP_DETECTED,

  // Account creation form is detected.
  SIGN_UP_DETECTED,

  // DEPRECATED: Password generation icon shown (old UI).
  DEPRECATED_ICON_SHOWN,

  // DEPRECATED: Password generation bubble shown (old UI).
  DEPRECATED_BUBBLE_SHOWN,

  // Password generation could be triggered if the user selects the appropriate
  // element.
  GENERATION_AVAILABLE,

  // Password generation popup is shown after user focuses the appropriate
  // password field.
  // DEPRECATED: These reports were triggered when the popup could have shown
  // not when it did show so they paint an unreliable picture. Newer stats
  // are only incremented per page, which is more useful to judge the
  // effectiveness of the UI.
  DEPRECATED_GENERATION_POPUP_SHOWN,

  // Generated password was accepted by the user.
  PASSWORD_ACCEPTED,

  // User focused the password field containing the generated password.
  // DEPRECATED: These reports were triggered when the popup could have shown
  // not when it did show so they paint an unreliable picture. Newer stats
  // are only incremented per page, which is more useful to judge the
  // effectiveness of the UI.
  DEPRECATED_EDITING_POPUP_SHOWN,

  // Password was changed after generation.
  PASSWORD_EDITED,

  // Generated password was deleted by the user
  PASSWORD_DELETED,

  // Password generation popup is shown after user focuses the appropriate
  // password field.
  GENERATION_POPUP_SHOWN,

  // User focused the password field containing the generated password.
  EDITING_POPUP_SHOWN,

  // Generation enabled because autocomplete attributes for username and
  // new-password are set.
  AUTOCOMPLETE_ATTRIBUTES_ENABLED_GENERATION,

  // Generation is triggered by the user from the context menu.
  PASSWORD_GENERATION_CONTEXT_MENU_PRESSED,

  // Context menu with generation item was shown.
  PASSWORD_GENERATION_CONTEXT_MENU_SHOWN,

  // Number of enum entries, used for UMA histogram reporting macros.
  EVENT_ENUM_COUNT
};

// Wrapper to store the user interactions with the password generation bubble.
struct PasswordGenerationActions {
  // Whether the user has clicked on the learn more link.
  bool learn_more_visited;

  // Whether the user has accepted the generated password.
  bool password_accepted;

  // Whether the user has manually edited password entry.
  bool password_edited;

  // Whether the user has clicked on the regenerate button.
  bool password_regenerated;

  PasswordGenerationActions();
  ~PasswordGenerationActions();
};

struct PasswordGenerationUIData {
  PasswordGenerationUIData(const gfx::RectF& bounds,
                           int max_length,
                           const base::string16& generation_element,
                           const autofill::PasswordForm& password_form);
  PasswordGenerationUIData();
  ~PasswordGenerationUIData();

  // Location at which to display a popup if needed. This location is specified
  // in the renderer's coordinate system. The popup will be anchored at
  // |bounds|.
  gfx::RectF bounds;

  // Maximum length of the generated password.
  int max_length;

  // Name of the password field to which the generation popup is attached.
  base::string16 generation_element;

  // The form associated with the password field.
  autofill::PasswordForm password_form;
};

void LogUserActions(PasswordGenerationActions actions);

void LogPasswordGenerationEvent(PasswordGenerationEvent event);

// Enumerates user actions after password generation bubble is shown.
// These are visible for testing purposes.
enum UserAction {
  // User closes the bubble without any meaningful actions (e.g. use backspace
  // key, close the bubble, click outside the bubble, etc).
  IGNORE_FEATURE,

  // User navigates to the learn more page. Note that in the current
  // implementation this will result in closing the bubble so this action
  // doesn't overlap with the following two actions.
  LEARN_MORE,

  // User accepts the generated password without manually editing it (but
  // including changing it through the regenerate button).
  ACCEPT_ORIGINAL_PASSWORD,

  // User accepts the gererated password after manually editing it.
  ACCEPT_AFTER_EDITING,

  // Number of enum entries, used for UMA histogram reporting macros.
  ACTION_ENUM_COUNT
};

// Returns true if Password Generation is enabled according to the field
// trial result and the flags.
bool IsPasswordGenerationEnabled();

}  // namespace password_generation
}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_COMMON_PASSWORD_GENERATION_UTIL_H_
