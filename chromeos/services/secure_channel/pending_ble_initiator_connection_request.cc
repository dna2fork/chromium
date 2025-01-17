// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/secure_channel/pending_ble_initiator_connection_request.h"

#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "chromeos/components/proximity_auth/logging/logging.h"

namespace chromeos {

namespace secure_channel {

namespace {
const char kBleInitiatorReadableRequestTypeForLogging[] = "BLE Initiator";
}  // namespace

// The number of times to attempt to connect to a device without receiving any
// response before giving up. When a connection to a device is attempted, a
// BLE discovery session listens for advertisements from the remote device as
// the first step of the connection; if no advertisement is picked up, it is
// likely that the remote device is not nearby or is not currently responding
// to connection requests.
const size_t PendingBleInitiatorConnectionRequest::kMaxEmptyScansPerDevice = 3u;

// The number of times to attempt a GATT connection to a device after a BLE
// discovery session has already detected a nearby device. GATT connections
// may fail for a variety of reasons, but most failures are ephemeral. Thus,
// more connection attempts are allowed in such cases since it is likely that
// a subsequent attempt will succeed. See https://crbug.com/805218.
const size_t
    PendingBleInitiatorConnectionRequest::kMaxGattConnectionAttemptsPerDevice =
        6u;

// static
PendingBleInitiatorConnectionRequest::Factory*
    PendingBleInitiatorConnectionRequest::Factory::test_factory_ = nullptr;

// static
PendingBleInitiatorConnectionRequest::Factory*
PendingBleInitiatorConnectionRequest::Factory::Get() {
  if (test_factory_)
    return test_factory_;

  static base::NoDestructor<PendingBleInitiatorConnectionRequest::Factory>
      factory;
  return factory.get();
}

// static
void PendingBleInitiatorConnectionRequest::Factory::SetFactoryForTesting(
    Factory* test_factory) {
  test_factory_ = test_factory;
}

PendingBleInitiatorConnectionRequest::Factory::~Factory() = default;

std::unique_ptr<PendingConnectionRequest<BleInitiatorFailureType>>
PendingBleInitiatorConnectionRequest::Factory::BuildInstance(
    std::unique_ptr<ClientConnectionParameters> client_connection_parameters,
    PendingConnectionRequestDelegate* delegate) {
  return base::WrapUnique(new PendingBleInitiatorConnectionRequest(
      std::move(client_connection_parameters), delegate));
}

PendingBleInitiatorConnectionRequest::PendingBleInitiatorConnectionRequest(
    std::unique_ptr<ClientConnectionParameters> client_connection_parameters,
    PendingConnectionRequestDelegate* delegate)
    : PendingConnectionRequestBase<BleInitiatorFailureType>(
          std::move(client_connection_parameters),
          kBleInitiatorReadableRequestTypeForLogging,
          delegate) {}

PendingBleInitiatorConnectionRequest::~PendingBleInitiatorConnectionRequest() =
    default;

void PendingBleInitiatorConnectionRequest::HandleConnectionFailure(
    BleInitiatorFailureType failure_detail) {
  switch (failure_detail) {
    case BleInitiatorFailureType::kAuthenticationError:
      // Authentication errors cannot be solved via a retry. This situation
      // likely means that the keys for this device or the remote device are out
      // of sync.
      StopRequestDueToConnectionFailures(
          mojom::ConnectionAttemptFailureReason::AUTHENTICATION_ERROR);
      break;
    case BleInitiatorFailureType::kGattConnectionError:
      ++num_gatt_failures_;
      if (num_gatt_failures_ == kMaxGattConnectionAttemptsPerDevice) {
        StopRequestDueToConnectionFailures(
            mojom::ConnectionAttemptFailureReason::GATT_CONNECTION_ERROR);
      }
      break;
    case BleInitiatorFailureType::kInterruptedByHigherPriorityConnectionAttempt:
      // This failure was not due to an actual failure to connect, so there is
      // nothing extra to do.
      break;
    case BleInitiatorFailureType::kTimeoutContactingRemoteDevice:
      ++num_empty_scan_failures_;
      if (num_empty_scan_failures_ == kMaxEmptyScansPerDevice) {
        StopRequestDueToConnectionFailures(
            mojom::ConnectionAttemptFailureReason::TIMEOUT_FINDING_DEVICE);
      }
      break;
    case BleInitiatorFailureType::kCouldNotGenerateAdvertisement:
      // Valid BeaconSeeds are required for generating BLE advertisements and
      // scan filters.
      StopRequestDueToConnectionFailures(mojom::ConnectionAttemptFailureReason::
                                             COULD_NOT_GENERATE_ADVERTISEMENT);
      break;
  }
}

}  // namespace secure_channel

}  // namespace chromeos
