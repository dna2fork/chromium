// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/secure_channel/fake_ble_advertiser.h"

#include "base/logging.h"
#include "base/stl_util.h"

namespace chromeos {

namespace secure_channel {

FakeBleAdvertiser::FakeBleAdvertiser(Delegate* delegate)
    : BleAdvertiser(delegate),
      scheduler_(std::make_unique<SharedResourceScheduler>()) {}

FakeBleAdvertiser::~FakeBleAdvertiser() = default;

const std::list<DeviceIdPair>& FakeBleAdvertiser::GetRequestsForPriority(
    ConnectionPriority connection_priority) {
  return priority_to_queued_requests_map()[connection_priority];
}

base::Optional<ConnectionPriority> FakeBleAdvertiser::GetPriorityForRequest(
    const DeviceIdPair& request) const {
  for (auto it = request_to_priority_map().begin();
       it != request_to_priority_map().end(); ++it) {
    if (it->first == request)
      return it->second;
  }

  return base::nullopt;
}

void FakeBleAdvertiser::NotifyAdvertisingSlotEnded(
    const DeviceIdPair& device_id_pair,
    bool replaced_by_higher_priority_advertisement) {
  // |device_id_pair| must be scheduled.
  DCHECK(GetPriorityForRequest(device_id_pair));

  BleAdvertiser::NotifyAdvertisingSlotEnded(
      device_id_pair, replaced_by_higher_priority_advertisement);
}

void FakeBleAdvertiser::AddAdvertisementRequest(
    const DeviceIdPair& request,
    ConnectionPriority connection_priority) {
  scheduler_->ScheduleRequest(request, connection_priority);
}

void FakeBleAdvertiser::UpdateAdvertisementRequestPriority(
    const DeviceIdPair& request,
    ConnectionPriority connection_priority) {
  scheduler_->UpdateRequestPriority(request, connection_priority);
}

void FakeBleAdvertiser::RemoveAdvertisementRequest(
    const DeviceIdPair& request) {
  scheduler_->RemoveScheduledRequest(request);
}

FakeBleAdvertiserDelegate::FakeBleAdvertiserDelegate() = default;

FakeBleAdvertiserDelegate::~FakeBleAdvertiserDelegate() = default;

void FakeBleAdvertiserDelegate::OnAdvertisingSlotEnded(
    const DeviceIdPair& device_id_pair,
    bool replaced_by_higher_priority_advertisement) {
  ended_advertisements_.push_back(std::make_pair(
      device_id_pair, replaced_by_higher_priority_advertisement));
}

}  // namespace secure_channel

}  // namespace chromeos
