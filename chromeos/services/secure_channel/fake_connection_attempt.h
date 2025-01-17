// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_SECURE_CHANNEL_FAKE_CONNECTION_ATTEMPT_H_
#define CHROMEOS_SERVICES_SECURE_CHANNEL_FAKE_CONNECTION_ATTEMPT_H_

#include <unordered_map>

#include "base/macros.h"
#include "base/unguessable_token.h"
#include "chromeos/services/secure_channel/client_connection_parameters.h"
#include "chromeos/services/secure_channel/connection_attempt.h"
#include "chromeos/services/secure_channel/connection_attempt_details.h"
#include "chromeos/services/secure_channel/pending_connection_request.h"

namespace chromeos {

namespace secure_channel {

class ConnectionAttemptDelegate;

// Fake ConnectionAttempt implementation.
template <typename FailureDetailType>
class FakeConnectionAttempt : public ConnectionAttempt<FailureDetailType> {
 public:
  FakeConnectionAttempt(
      ConnectionAttemptDelegate* delegate,
      const ConnectionAttemptDetails& connection_attempt_details);
  ~FakeConnectionAttempt() override;

  using IdToRequestMap = std::unordered_map<
      base::UnguessableToken,
      std::unique_ptr<PendingConnectionRequest<FailureDetailType>>,
      base::UnguessableTokenHash>;
  const IdToRequestMap& id_to_request_map() const { return id_to_request_map_; }

  void set_client_data_for_extraction(
      std::vector<std::unique_ptr<ClientConnectionParameters>>
          client_data_for_extraction) {
    client_data_for_extraction_ = std::move(client_data_for_extraction);
  }

  // Make OnConnectionAttempt{Succeeded|FinishedWithoutConnection}() public for
  // testing.
  using ConnectionAttempt<FailureDetailType>::OnConnectionAttemptSucceeded;
  using ConnectionAttempt<
      FailureDetailType>::OnConnectionAttemptFinishedWithoutConnection;

 private:
  // ConnectionAttempt<FailureDetailType>:
  void ProcessAddingNewConnectionRequest(
      std::unique_ptr<PendingConnectionRequest<FailureDetailType>> request)
      override {
    DCHECK(request);
    DCHECK(!base::ContainsKey(id_to_request_map_, request->GetRequestId()));

    id_to_request_map_[request->GetRequestId()] = std::move(request);
  }

  std::vector<std::unique_ptr<ClientConnectionParameters>>
  ExtractClientConnectionParameters() override {
    return std::move(client_data_for_extraction_);
  }

  IdToRequestMap id_to_request_map_;

  std::vector<std::unique_ptr<ClientConnectionParameters>>
      client_data_for_extraction_;

  DISALLOW_COPY_AND_ASSIGN(FakeConnectionAttempt);
};

}  // namespace secure_channel

}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_SECURE_CHANNEL_FAKE_CONNECTION_ATTEMPT_H_
