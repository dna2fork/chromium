// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/secure_channel/ble_scanner_impl.h"

#include <iostream>
#include <sstream>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "chromeos/components/proximity_auth/logging/logging.h"
#include "chromeos/services/secure_channel/ble_constants.h"
#include "chromeos/services/secure_channel/ble_service_data_helper.h"
#include "chromeos/services/secure_channel/ble_synchronizer_base.h"
#include "components/cryptauth/proto/cryptauth_api.pb.h"
#include "components/cryptauth/remote_device_ref.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/bluetooth_discovery_session.h"
#include "device/bluetooth/bluetooth_uuid.h"

namespace chromeos {

namespace secure_channel {

namespace {

// TODO(hansberry): Share this constant with BleServiceDataHelper.
const size_t kMinNumBytesInServiceData = 2;

}  // namespace

// static
BleScannerImpl::Factory* BleScannerImpl::Factory::test_factory_ = nullptr;

// static
BleScannerImpl::Factory* BleScannerImpl::Factory::Get() {
  if (test_factory_)
    return test_factory_;

  static base::NoDestructor<Factory> factory;
  return factory.get();
}

// static
void BleScannerImpl::Factory::SetFactoryForTesting(Factory* test_factory) {
  test_factory_ = test_factory;
}

std::unique_ptr<BleScanner> BleScannerImpl::Factory::BuildInstance(
    Delegate* delegate,
    secure_channel::BleServiceDataHelper* service_data_helper,
    BleSynchronizerBase* ble_synchronizer,
    scoped_refptr<device::BluetoothAdapter> adapter) {
  return base::WrapUnique(new BleScannerImpl(delegate, service_data_helper,
                                             ble_synchronizer, adapter));
}

BleScannerImpl::ServiceDataProvider::~ServiceDataProvider() = default;

const std::vector<uint8_t>*
BleScannerImpl::ServiceDataProvider::ExtractProximityAuthServiceData(
    device::BluetoothDevice* bluetooth_device) {
  return bluetooth_device->GetServiceDataForUUID(
      device::BluetoothUUID(kAdvertisingServiceUuid));
}

BleScannerImpl::BleScannerImpl(
    Delegate* delegate,
    secure_channel::BleServiceDataHelper* service_data_helper,
    BleSynchronizerBase* ble_synchronizer,
    scoped_refptr<device::BluetoothAdapter> adapter)
    : BleScanner(delegate),
      service_data_helper_(service_data_helper),
      ble_synchronizer_(ble_synchronizer),
      adapter_(adapter),
      service_data_provider_(std::make_unique<ServiceDataProvider>()),
      weak_ptr_factory_(this) {
  adapter_->AddObserver(this);
}

BleScannerImpl::~BleScannerImpl() {
  adapter_->RemoveObserver(this);
}

void BleScannerImpl::HandleScanFilterChange() {
  UpdateDiscoveryStatus();
}

void BleScannerImpl::DeviceAdded(device::BluetoothAdapter* adapter,
                                 device::BluetoothDevice* bluetooth_device) {
  DCHECK_EQ(adapter_.get(), adapter);
  HandleDeviceUpdated(bluetooth_device);
}

void BleScannerImpl::DeviceChanged(device::BluetoothAdapter* adapter,
                                   device::BluetoothDevice* bluetooth_device) {
  DCHECK_EQ(adapter_.get(), adapter);
  HandleDeviceUpdated(bluetooth_device);
}

void BleScannerImpl::UpdateDiscoveryStatus() {
  if (should_discovery_session_be_active())
    EnsureDiscoverySessionActive();
  else
    EnsureDiscoverySessionNotActive();
}

bool BleScannerImpl::IsDiscoverySessionActive() {
  ResetDiscoverySessionIfNotActive();
  return discovery_session_.get() != nullptr;
}

void BleScannerImpl::ResetDiscoverySessionIfNotActive() {
  if (!discovery_session_ || discovery_session_->IsActive())
    return;

  PA_LOG(ERROR) << "BluetoothDiscoverySession became out of sync. Session is "
                << "no longer active, but it was never stopped successfully. "
                << "Resetting session.";

  // |discovery_session_| should be deleted as part of
  // OnDiscoverySessionStopped() whenever the session is no longer active.
  // However, a Bluetooth issue (https://crbug.com/768521) sometimes causes the
  // session to become inactive without Stop() ever succeeding. If this
  // occurs, reset state accordingly.
  discovery_session_.reset();
  discovery_session_weak_ptr_factory_.reset();
  is_initializing_discovery_session_ = false;
  is_stopping_discovery_session_ = false;
  weak_ptr_factory_.InvalidateWeakPtrs();
}

void BleScannerImpl::EnsureDiscoverySessionActive() {
  if (IsDiscoverySessionActive() || is_initializing_discovery_session_)
    return;

  is_initializing_discovery_session_ = true;

  ble_synchronizer_->StartDiscoverySession(
      base::Bind(&BleScannerImpl::OnDiscoverySessionStarted,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&BleScannerImpl::OnStartDiscoverySessionError,
                 weak_ptr_factory_.GetWeakPtr()));
}

void BleScannerImpl::OnDiscoverySessionStarted(
    std::unique_ptr<device::BluetoothDiscoverySession> discovery_session) {
  PA_LOG(INFO) << "Started discovery session successfully.";
  is_initializing_discovery_session_ = false;

  discovery_session_ = std::move(discovery_session);
  discovery_session_weak_ptr_factory_ =
      std::make_unique<base::WeakPtrFactory<device::BluetoothDiscoverySession>>(
          discovery_session_.get());

  UpdateDiscoveryStatus();
}

void BleScannerImpl::OnStartDiscoverySessionError() {
  is_initializing_discovery_session_ = false;
  PA_LOG(ERROR) << "Error starting discovery session.";
  UpdateDiscoveryStatus();
}

void BleScannerImpl::EnsureDiscoverySessionNotActive() {
  if (!IsDiscoverySessionActive() || is_stopping_discovery_session_)
    return;

  is_stopping_discovery_session_ = true;

  ble_synchronizer_->StopDiscoverySession(
      discovery_session_weak_ptr_factory_->GetWeakPtr(),
      base::Bind(&BleScannerImpl::OnDiscoverySessionStopped,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&BleScannerImpl::OnStopDiscoverySessionError,
                 weak_ptr_factory_.GetWeakPtr()));
}

void BleScannerImpl::OnDiscoverySessionStopped() {
  is_stopping_discovery_session_ = false;
  PA_LOG(INFO) << "Stopped discovery session successfully.";

  discovery_session_.reset();
  discovery_session_weak_ptr_factory_.reset();

  UpdateDiscoveryStatus();
}

void BleScannerImpl::OnStopDiscoverySessionError() {
  is_stopping_discovery_session_ = false;
  PA_LOG(ERROR) << "Error stopping discovery session.";
  UpdateDiscoveryStatus();
}

void BleScannerImpl::HandleDeviceUpdated(
    device::BluetoothDevice* bluetooth_device) {
  DCHECK(bluetooth_device);

  const std::vector<uint8_t>* service_data =
      service_data_provider_->ExtractProximityAuthServiceData(bluetooth_device);
  if (!service_data || service_data->size() < kMinNumBytesInServiceData) {
    // If there is no service data or the service data is of insufficient
    // length, there is not enough information to create a connection.
    return;
  }

  // Convert the service data from a std::vector<uint8_t> to a std::string.
  std::string service_data_str;
  char* string_contents_ptr =
      base::WriteInto(&service_data_str, service_data->size() + 1);
  memcpy(string_contents_ptr, service_data->data(), service_data->size());

  auto potential_result = service_data_helper_->IdentifyRemoteDevice(
      service_data_str, scan_filters());

  // There was service data for the ProximityAuth UUID, but it did not apply to
  // any active scan filters. The advertisement was likely from a nearby device
  // attempting a ProximityAuth connection for another account.
  if (!potential_result)
    return;

  // Prepare a hex string of |service_data_str|.
  std::stringstream ss;
  ss << "0x" << std::hex;
  for (const auto& character : service_data_str)
    ss << static_cast<uint32_t>(character);

  PA_LOG(INFO) << "BleScannerImpl::HandleDeviceUpdated(): Received scan result "
               << "from device with ID \""
               << potential_result->first.GetTruncatedDeviceIdForLogs() << "\""
               << ". Service data: " << ss.str()
               << ", Background advertisement: "
               << (potential_result->second ? "true" : "false");

  NotifyReceivedAdvertisementFromDevice(
      potential_result->first, bluetooth_device, potential_result->second);
}

void BleScannerImpl::SetServiceDataProviderForTesting(
    std::unique_ptr<ServiceDataProvider> service_data_provider) {
  service_data_provider_ = std::move(service_data_provider);
}

}  // namespace secure_channel

}  // namespace chromeos
