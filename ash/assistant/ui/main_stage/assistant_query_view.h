// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_ASSISTANT_UI_MAIN_STAGE_ASSISTANT_QUERY_VIEW_H_
#define ASH_ASSISTANT_UI_MAIN_STAGE_ASSISTANT_QUERY_VIEW_H_

#include "ash/assistant/model/assistant_interaction_model_observer.h"
#include "base/macros.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/view.h"

namespace ash {

class AssistantController;

// AssistantQueryView is the visual representation of an AssistantQuery. It is a
// child view of UiElementContainerView.
class AssistantQueryView : public views::View,
                           public AssistantInteractionModelObserver {
 public:
  explicit AssistantQueryView(AssistantController* assistant_controller);
  ~AssistantQueryView() override;

  // views::View:
  gfx::Size CalculatePreferredSize() const override;
  void ChildPreferredSizeChanged(views::View* child) override;
  void OnBoundsChanged(const gfx::Rect& prev_bounds) override;

  // AssistantInteractionModelObserver:
  void OnQueryChanged(const AssistantQuery& query) override;
  void OnQueryCleared() override;

 private:
  void InitLayout();
  void SetText(const std::string& high_confidence_text,
               const std::string& low_confidence_text = std::string());
  views::StyledLabel::RangeStyleInfo CreateStyleInfo(SkColor color) const;

  AssistantController* const assistant_controller_;  // Owned by Shell.
  views::StyledLabel* label_;                        // Owned by view hierarchy.

  DISALLOW_COPY_AND_ASSIGN(AssistantQueryView);
};

}  // namespace ash

#endif  // ASH_ASSISTANT_UI_MAIN_STAGE_ASSISTANT_QUERY_VIEW_H_
