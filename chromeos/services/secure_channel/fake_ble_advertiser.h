// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_SECURE_CHANNEL_FAKE_BLE_ADVERTISER_H_
#define CHROMEOS_SERVICES_SECURE_CHANNEL_FAKE_BLE_ADVERTISER_H_

#include <unordered_map>

#include "base/macros.h"
#include "base/optional.h"
#include "chromeos/services/secure_channel/ble_advertiser.h"
#include "chromeos/services/secure_channel/device_id_pair.h"
#include "chromeos/services/secure_channel/public/cpp/shared/connection_priority.h"
#include "chromeos/services/secure_channel/shared_resource_scheduler.h"

namespace chromeos {

namespace secure_channel {

// Test BleAdvertisementScheduler implementation, which internally uses a
// SharedResourceScheduler to store the provided requests.
class FakeBleAdvertiser : public BleAdvertiser {
 public:
  FakeBleAdvertiser(Delegate* delegate);
  ~FakeBleAdvertiser() override;

  const std::list<DeviceIdPair>& GetRequestsForPriority(
      ConnectionPriority connection_priority);

  base::Optional<ConnectionPriority> GetPriorityForRequest(
      const DeviceIdPair& request) const;

  void NotifyAdvertisingSlotEnded(
      const DeviceIdPair& device_id_pair,
      bool replaced_by_higher_priority_advertisement);

 private:
  // BleAdvertiser:
  void AddAdvertisementRequest(const DeviceIdPair& request,
                               ConnectionPriority connection_priority) override;
  void UpdateAdvertisementRequestPriority(
      const DeviceIdPair& request,
      ConnectionPriority connection_priority) override;
  void RemoveAdvertisementRequest(const DeviceIdPair& request) override;

  base::flat_map<ConnectionPriority, std::list<DeviceIdPair>>&
  priority_to_queued_requests_map() const {
    return scheduler_->priority_to_queued_requests_map_;
  }

  const base::flat_map<DeviceIdPair, ConnectionPriority>&
  request_to_priority_map() const {
    return scheduler_->request_to_priority_map_;
  }

  std::unique_ptr<SharedResourceScheduler> scheduler_;

  DISALLOW_COPY_AND_ASSIGN(FakeBleAdvertiser);
};

// Test BleAdvertiser::Delegate implementation.
class FakeBleAdvertiserDelegate : public BleAdvertiser::Delegate {
 public:
  FakeBleAdvertiserDelegate();
  ~FakeBleAdvertiserDelegate() override;

  using EndedAdvertisement = std::pair<DeviceIdPair, bool>;

  const std::vector<EndedAdvertisement>& ended_advertisements() const {
    return ended_advertisements_;
  }

 private:
  // BleAdvertiser::Delegate:
  void OnAdvertisingSlotEnded(
      const DeviceIdPair& device_id_pair,
      bool replaced_by_higher_priority_advertisement) override;

  std::vector<EndedAdvertisement> ended_advertisements_;

  DISALLOW_COPY_AND_ASSIGN(FakeBleAdvertiserDelegate);
};

}  // namespace secure_channel

}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_SECURE_CHANNEL_FAKE_BLE_ADVERTISER_H_
