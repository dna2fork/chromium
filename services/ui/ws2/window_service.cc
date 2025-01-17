// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/ws2/window_service.h"

#include "base/bind.h"
#include "base/single_thread_task_runner.h"
#include "services/ui/ws2/gpu_support.h"
#include "services/ui/ws2/screen_provider.h"
#include "services/ui/ws2/server_window.h"
#include "services/ui/ws2/window_service_delegate.h"
#include "services/ui/ws2/window_tree.h"
#include "services/ui/ws2/window_tree_factory.h"
#include "ui/aura/env.h"
#include "ui/base/mojo/clipboard_host.h"

namespace ui {
namespace ws2 {

WindowService::WindowService(WindowServiceDelegate* delegate,
                             std::unique_ptr<GpuSupport> gpu_support,
                             aura::client::FocusClient* focus_client)
    : delegate_(delegate),
      gpu_support_(std::move(gpu_support)),
      screen_provider_(std::make_unique<ScreenProvider>()),
      focus_client_(focus_client),
      ime_registrar_(&ime_driver_) {
  DCHECK(focus_client);  // A |focus_client| must be provided.
  // MouseLocationManager is necessary for providing the shared memory with the
  // location of the mouse to clients.
  aura::Env::GetInstance()->CreateMouseLocationManager();

  input_device_server_.RegisterAsObserver();
}

WindowService::~WindowService() = default;

ServerWindow* WindowService::GetServerWindowForWindowCreateIfNecessary(
    aura::Window* window) {
  ServerWindow* server_window = ServerWindow::GetMayBeNull(window);
  if (server_window)
    return server_window;

  const viz::FrameSinkId frame_sink_id =
      ClientWindowId(kWindowServerClientId, next_window_id_++);
  CHECK_NE(0u, next_window_id_);
  const bool is_top_level = false;
  return ServerWindow::Create(window, nullptr, frame_sink_id, is_top_level);
}

std::unique_ptr<WindowTree> WindowService::CreateWindowTree(
    mojom::WindowTreeClient* window_tree_client) {
  const ClientSpecificId client_id = next_client_id_++;
  CHECK_NE(0u, next_client_id_);
  return std::make_unique<WindowTree>(this, client_id, window_tree_client);
}

void WindowService::SetFrameDecorationValues(
    const gfx::Insets& client_area_insets,
    int max_title_bar_button_width) {
  screen_provider_->SetFrameDecorationValues(client_area_insets,
                                             max_title_bar_button_width);
}

// static
bool WindowService::HasRemoteClient(aura::Window* window) {
  return ServerWindow::GetMayBeNull(window);
}

void WindowService::RequestClose(aura::Window* window) {
  ServerWindow* server_window = ServerWindow::GetMayBeNull(window);
  DCHECK(window && server_window->IsTopLevel());
  server_window->owning_window_tree()->RequestClose(server_window);
}

void WindowService::OnStart() {
  window_tree_factory_ = std::make_unique<WindowTreeFactory>(this);

  registry_.AddInterface(base::BindRepeating(
      &WindowService::BindClipboardHostRequest, base::Unretained(this)));
  registry_.AddInterface(base::BindRepeating(
      &WindowService::BindScreenProviderRequest, base::Unretained(this)));
  registry_.AddInterface(base::BindRepeating(
      &WindowService::BindImeRegistrarRequest, base::Unretained(this)));
  registry_.AddInterface(base::BindRepeating(
      &WindowService::BindImeDriverRequest, base::Unretained(this)));
  registry_.AddInterface(base::BindRepeating(
      &WindowService::BindInputDeviceServerRequest, base::Unretained(this)));
  registry_.AddInterface(base::BindRepeating(
      &WindowService::BindWindowTreeFactoryRequest, base::Unretained(this)));

  // |gpu_support_| may be null in tests.
  if (gpu_support_) {
    registry_.AddInterface(
        base::BindRepeating(
            &GpuSupport::BindDiscardableSharedMemoryManagerOnGpuTaskRunner,
            base::Unretained(gpu_support_.get())),
        gpu_support_->GetGpuTaskRunner());
    registry_.AddInterface(
        base::BindRepeating(&GpuSupport::BindGpuRequestOnGpuTaskRunner,
                            base::Unretained(gpu_support_.get())),
        gpu_support_->GetGpuTaskRunner());
  }
}

void WindowService::OnBindInterface(
    const service_manager::BindSourceInfo& remote_info,
    const std::string& interface_name,
    mojo::ScopedMessagePipeHandle handle) {
  registry_.BindInterface(interface_name, std::move(handle));
}

void WindowService::BindClipboardHostRequest(
    mojom::ClipboardHostRequest request) {
  if (!clipboard_host_)
    clipboard_host_ = std::make_unique<ClipboardHost>();
  clipboard_host_->AddBinding(std::move(request));
}

void WindowService::BindScreenProviderRequest(
    mojom::ScreenProviderRequest request) {
  screen_provider_->AddBinding(std::move(request));
}

void WindowService::BindImeRegistrarRequest(
    mojom::IMERegistrarRequest request) {
  ime_registrar_.AddBinding(std::move(request));
}

void WindowService::BindImeDriverRequest(mojom::IMEDriverRequest request) {
  ime_driver_.AddBinding(std::move(request));
}

void WindowService::BindInputDeviceServerRequest(
    mojom::InputDeviceServerRequest request) {
  input_device_server_.AddBinding(std::move(request));
}

void WindowService::BindWindowTreeFactoryRequest(
    ui::mojom::WindowTreeFactoryRequest request) {
  window_tree_factory_->AddBinding(std::move(request));
}

}  // namespace ws2
}  // namespace ui
