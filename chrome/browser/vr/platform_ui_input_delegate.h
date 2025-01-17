// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VR_PLATFORM_UI_INPUT_DELEGATE_H_
#define CHROME_BROWSER_VR_PLATFORM_UI_INPUT_DELEGATE_H_

#include <memory>
#include <queue>

#include "base/bind.h"
#include "base/callback.h"
#include "base/macros.h"
#include "chrome/browser/vr/macros.h"
#include "chrome/browser/vr/model/text_input_info.h"
#include "chrome/browser/vr/text_edit_action.h"
#include "chrome/browser/vr/vr_export.h"
#include "third_party/blink/public/platform/web_input_event.h"
#include "ui/gfx/geometry/size.h"

namespace blink {
class WebGestureEvent;
class WebMouseEvent;
}  // namespace blink

namespace gfx {
class PointF;
}  // namespace gfx

namespace vr {

class PlatformInputHandler;

// This class is responsible for processing all events and gestures for
// PlatformUiElement.
class VR_EXPORT PlatformUiInputDelegate {
 public:
  PlatformUiInputDelegate();
  explicit PlatformUiInputDelegate(PlatformInputHandler* input_handler);
  virtual ~PlatformUiInputDelegate();

  const gfx::Size& size() const { return size_; }

  // The following functions are virtual so that they may be overridden in the
  // MockContentInputDelegate.
  VIRTUAL_FOR_MOCKS void OnHoverEnter(const gfx::PointF& normalized_hit_point);
  VIRTUAL_FOR_MOCKS void OnHoverLeave();
  VIRTUAL_FOR_MOCKS void OnHoverMove(const gfx::PointF& normalized_hit_point);
  VIRTUAL_FOR_MOCKS void OnButtonDown(const gfx::PointF& normalized_hit_point);
  VIRTUAL_FOR_MOCKS void OnButtonUp(const gfx::PointF& normalized_hit_point);
  VIRTUAL_FOR_MOCKS void OnFlingCancel(
      std::unique_ptr<blink::WebGestureEvent> gesture,
      const gfx::PointF& normalized_hit_point);
  VIRTUAL_FOR_MOCKS void OnScrollBegin(
      std::unique_ptr<blink::WebGestureEvent> gesture,
      const gfx::PointF& normalized_hit_point);
  VIRTUAL_FOR_MOCKS void OnScrollUpdate(
      std::unique_ptr<blink::WebGestureEvent> gesture,
      const gfx::PointF& normalized_hit_point);
  VIRTUAL_FOR_MOCKS void OnScrollEnd(
      std::unique_ptr<blink::WebGestureEvent> gesture,
      const gfx::PointF& normalized_hit_point);

  void SetSize(int width, int height) { size_ = {width, height}; }
  void SetPlatformInputHandlerForTest(PlatformInputHandler* input_handler) {
    input_handler_ = input_handler;
  }

 protected:
  virtual void SendGestureToTarget(std::unique_ptr<blink::WebInputEvent> event);
  virtual std::unique_ptr<blink::WebMouseEvent> MakeMouseEvent(
      blink::WebInputEvent::Type type,
      const gfx::PointF& normalized_web_content_location);
  PlatformInputHandler* input_handler() const { return input_handler_; }

 private:
  void UpdateGesture(const gfx::PointF& normalized_content_hit_point,
                     blink::WebGestureEvent& gesture);
  gfx::Size size_;

  PlatformInputHandler* input_handler_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(PlatformUiInputDelegate);
};

}  // namespace vr

#endif  // CHROME_BROWSER_VR_PLATFORM_UI_INPUT_DELEGATE_H_
