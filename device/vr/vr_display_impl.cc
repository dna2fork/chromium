// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/vr/vr_display_impl.h"

#include <utility>

#include "base/bind.h"
#include "device/vr/vr_device_base.h"

namespace {
constexpr int kMaxImageHeightOrWidth = 8000;
}  // namespace

namespace device {

VRDisplayImpl::VRDisplayImpl(VRDevice* device,
                             mojom::VRServiceClient* service_client,
                             mojom::VRDisplayInfoPtr display_info,
                             mojom::VRDisplayHostPtr display_host,
                             mojom::VRDisplayClientRequest client_request,
                             int render_process_id,
                             int render_frame_id)
    : binding_(this),
      device_(static_cast<VRDeviceBase*>(device)),
      render_process_id_(render_process_id),
      render_frame_id_(render_frame_id) {
  mojom::VRMagicWindowProviderPtr magic_window_provider;
  binding_.Bind(mojo::MakeRequest(&magic_window_provider));
  service_client->OnDisplayConnected(
      std::move(magic_window_provider), std::move(display_host),
      std::move(client_request), std::move(display_info));
}

VRDisplayImpl::~VRDisplayImpl() = default;

void VRDisplayImpl::RequestSession(
    bool has_user_activation,
    mojom::VRDisplayHost::RequestSessionCallback callback) {
  device_->RequestSession(render_process_id_, render_frame_id_,
                          has_user_activation, std::move(callback));
}

// Gets a pose for magic window sessions.
void VRDisplayImpl::GetPose(GetPoseCallback callback) {
  if (device_->HasExclusiveSession() || restrict_frame_data_) {
    std::move(callback).Run(nullptr);
    return;
  }
  device_->GetMagicWindowPose(std::move(callback));
}

// Gets frame image data for AR magic window sessions.
void VRDisplayImpl::GetFrameData(const gfx::Size& frame_size,
                                 display::Display::Rotation rotation,
                                 GetFrameDataCallback callback) {
  if (device_->HasExclusiveSession() || restrict_frame_data_) {
    std::move(callback).Run(nullptr);
    return;
  }

  // Check for a valid frame size.
  // While Mojo should handle negative values, we also do not want to allow 0.
  // TODO(https://crbug.com/841062): Reconsider how we check the sizes.
  if (frame_size.width() <= 0 || frame_size.height() <= 0 ||
      frame_size.width() > kMaxImageHeightOrWidth ||
      frame_size.height() > kMaxImageHeightOrWidth) {
    DLOG(ERROR) << "Invalid frame size passed to GetFrameData().";
    std::move(callback).Run(nullptr);
    return;
  }

  device_->GetMagicWindowFrameData(frame_size, rotation, std::move(callback));
}

void VRDisplayImpl::RequestHitTest(mojom::XRRayPtr ray,
                                   RequestHitTestCallback callback) {
  if (restrict_frame_data_) {
    std::move(callback).Run(base::nullopt);
    return;
  }
  device_->RequestHitTest(std::move(ray), std::move(callback));
}

// XrSessionController
void VRDisplayImpl::SetFrameDataRestricted(bool frame_data_restricted) {
  restrict_frame_data_ = frame_data_restricted;
  if (device_->ShouldPauseTrackingWhenFrameDataRestricted()) {
    if (restrict_frame_data_) {
      device_->PauseTracking();
    } else {
      device_->ResumeTracking();
    }
  }
}

void VRDisplayImpl::StopSession() {
  binding_.Close();
}

}  // namespace device
