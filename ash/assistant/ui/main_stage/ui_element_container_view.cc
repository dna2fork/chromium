// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/assistant/ui/main_stage/ui_element_container_view.h"

#include "ash/assistant/assistant_controller.h"
#include "ash/assistant/model/assistant_ui_element.h"
#include "ash/assistant/ui/assistant_ui_constants.h"
#include "ash/assistant/ui/main_stage/assistant_query_view.h"
#include "ash/assistant/ui/main_stage/assistant_text_element_view.h"
#include "ash/public/cpp/app_list/answer_card_contents_registry.h"
#include "base/callback.h"
#include "base/unguessable_token.h"
#include "ui/views/layout/box_layout.h"

namespace ash {

UiElementContainerView::UiElementContainerView(
    AssistantController* assistant_controller)
    : assistant_controller_(assistant_controller),
      assistant_query_view_(
          std::make_unique<AssistantQueryView>(assistant_controller)),
      render_request_weak_factory_(this) {
  InitLayout();

  // The Assistant controller indirectly owns the view hierarchy to which
  // UiElementContainerView belongs so is guaranteed to outlive it.
  assistant_controller_->AddInteractionModelObserver(this);
}

UiElementContainerView::~UiElementContainerView() {
  assistant_controller_->RemoveInteractionModelObserver(this);
  ReleaseAllCards();
}

void UiElementContainerView::ChildPreferredSizeChanged(views::View* child) {
  PreferredSizeChanged();
}

void UiElementContainerView::InitLayout() {
  views::BoxLayout* layout_manager =
      SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(0, kPaddingDip),
          kSpacingDip));

  layout_manager->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::CROSS_AXIS_ALIGNMENT_START);

  // Query.
  assistant_query_view_->set_owned_by_client();
  AddChildView(assistant_query_view_.get());
}

void UiElementContainerView::OnUiElementAdded(
    const AssistantUiElement* ui_element) {
  // If we are processing a UI element we need to pend the incoming element
  // instead of handling it immediately.
  if (is_processing_ui_element_) {
    pending_ui_element_list_.push_back(ui_element);
    return;
  }

  switch (ui_element->GetType()) {
    case AssistantUiElementType::kCard:
      OnCardElementAdded(static_cast<const AssistantCardElement*>(ui_element));
      break;
    case AssistantUiElementType::kText:
      OnTextElementAdded(static_cast<const AssistantTextElement*>(ui_element));
      break;
  }
}

void UiElementContainerView::OnUiElementsCleared() {
  // Prevent any in-flight card rendering requests from returning.
  render_request_weak_factory_.InvalidateWeakPtrs();

  RemoveAllChildViews(/*delete_children=*/true);
  AddChildView(assistant_query_view_.get());

  PreferredSizeChanged();

  ReleaseAllCards();

  // We can clear any pending UI elements as they are no longer relevant.
  pending_ui_element_list_.clear();
  SetProcessingUiElement(false);
}

void UiElementContainerView::OnCardElementAdded(
    const AssistantCardElement* card_element) {
  DCHECK(!is_processing_ui_element_);

  // We need to pend any further UI elements until the card has been rendered.
  // This insures that views will be added to the view hierarchy in the order in
  // which they were received.
  SetProcessingUiElement(true);

  // Generate a unique identifier for the card. This will be used to clean up
  // card resources when it is no longer needed.
  base::UnguessableToken id_token = base::UnguessableToken::Create();

  // Configure parameters for the card.
  ash::mojom::AssistantCardParamsPtr params(
      ash::mojom::AssistantCardParams::New());
  params->html = card_element->html();
  params->min_width_dip = kPreferredWidthDip - 2 * kPaddingDip;
  params->max_width_dip = kPreferredWidthDip - 2 * kPaddingDip;

  // The card will be rendered by AssistantCardRenderer, running the specified
  // callback when the card is ready for embedding.
  assistant_controller_->RenderCard(
      id_token, std::move(params),
      base::BindOnce(&UiElementContainerView::OnCardReady,
                     render_request_weak_factory_.GetWeakPtr()));

  // Cache the card identifier for freeing up resources when it is no longer
  // needed.
  id_token_list_.push_back(id_token);
}

void UiElementContainerView::OnCardReady(
    const base::UnguessableToken& embed_token) {
  // When the card has been rendered in the same process, its view is
  // available in the AnswerCardContentsRegistry's token-to-view map.
  if (app_list::AnswerCardContentsRegistry::Get()) {
    RemoveChildView(assistant_query_view_.get());
    AddChildView(
        app_list::AnswerCardContentsRegistry::Get()->GetView(embed_token));
  }
  // TODO(dmblack): Handle Mash case.

  PreferredSizeChanged();

  // Once the card has been rendered and embedded, we can resume processing
  // any UI elements that are in the pending queue.
  SetProcessingUiElement(false);
}

void UiElementContainerView::OnTextElementAdded(
    const AssistantTextElement* text_element) {
  DCHECK(!is_processing_ui_element_);

  RemoveChildView(assistant_query_view_.get());
  AddChildView(new AssistantTextElementView(text_element));

  PreferredSizeChanged();
}

void UiElementContainerView::SetProcessingUiElement(bool is_processing) {
  if (is_processing == is_processing_ui_element_)
    return;

  is_processing_ui_element_ = is_processing;

  // If we are no longer processing a UI element, we need to handle anything
  // that was put in the pending queue. Note that the elements left in the
  // pending queue may themselves require processing that again pends the queue.
  if (!is_processing_ui_element_)
    ProcessPendingUiElements();
}

void UiElementContainerView::ProcessPendingUiElements() {
  while (!is_processing_ui_element_ && !pending_ui_element_list_.empty()) {
    const AssistantUiElement* ui_element = pending_ui_element_list_.front();
    pending_ui_element_list_.pop_front();
    OnUiElementAdded(ui_element);
  }
}

void UiElementContainerView::ReleaseAllCards() {
  if (id_token_list_.empty())
    return;

  // Release any resources associated with the cards identified in
  // |id_token_list_| owned by AssistantCardRenderer.
  assistant_controller_->ReleaseCards(id_token_list_);
  id_token_list_.clear();
}

}  // namespace ash
