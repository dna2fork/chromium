// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/secure_channel/public/cpp/client/secure_channel_client_impl.h"

#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/optional.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/test/null_task_runner.h"
#include "base/test/scoped_task_environment.h"
#include "base/test/test_simple_task_runner.h"
#include "chromeos/services/secure_channel/fake_channel.h"
#include "chromeos/services/secure_channel/fake_secure_channel.h"
#include "chromeos/services/secure_channel/public/cpp/client/client_channel_impl.h"
#include "chromeos/services/secure_channel/public/cpp/client/connection_attempt.h"
#include "chromeos/services/secure_channel/public/cpp/client/connection_attempt_impl.h"
#include "chromeos/services/secure_channel/public/cpp/client/fake_client_channel.h"
#include "chromeos/services/secure_channel/public/cpp/client/fake_connection_attempt.h"
#include "chromeos/services/secure_channel/public/mojom/constants.mojom.h"
#include "chromeos/services/secure_channel/public/mojom/secure_channel.mojom.h"
#include "chromeos/services/secure_channel/secure_channel_impl.h"
#include "chromeos/services/secure_channel/secure_channel_service.h"
#include "components/cryptauth/remote_device_test_util.h"
#include "services/service_manager/public/cpp/test/test_connector_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {

namespace secure_channel {

namespace {

const size_t kNumTestDevices = 5u;

class FakeSecureChannelImplFactory : public SecureChannelImpl::Factory {
 public:
  explicit FakeSecureChannelImplFactory(
      std::unique_ptr<FakeSecureChannel> fake_secure_channel)
      : fake_secure_channel_(std::move(fake_secure_channel)) {}

  ~FakeSecureChannelImplFactory() override = default;

  // SecureChannelImpl::Factory:
  std::unique_ptr<SecureChannelBase> BuildInstance() override {
    EXPECT_TRUE(fake_secure_channel_);
    return std::move(fake_secure_channel_);
  }

 private:
  std::unique_ptr<FakeSecureChannel> fake_secure_channel_;
};

class FakeConnectionAttemptFactory : public ConnectionAttemptImpl::Factory {
 public:
  FakeConnectionAttemptFactory() = default;
  ~FakeConnectionAttemptFactory() override = default;

  // ConnectionAttemptImpl::Factory:
  std::unique_ptr<ConnectionAttemptImpl> BuildInstance() override {
    return std::make_unique<FakeConnectionAttempt>();
  }
};

class FakeClientChannelImplFactory : public ClientChannelImpl::Factory {
 public:
  FakeClientChannelImplFactory() = default;
  ~FakeClientChannelImplFactory() override = default;

  ClientChannel* last_client_channel_created() {
    return last_client_channel_created_;
  }

  // ClientChannelImpl::Factory:
  std::unique_ptr<ClientChannel> BuildInstance(
      mojom::ChannelPtr channel,
      mojom::MessageReceiverRequest message_receiver_request) override {
    auto client_channel = std::make_unique<FakeClientChannel>();
    last_client_channel_created_ = client_channel.get();
    return client_channel;
  }

 private:
  ClientChannel* last_client_channel_created_;
};

class TestConnectionAttemptDelegate : public ConnectionAttempt::Delegate {
 public:
  void OnConnectionAttemptFailure(
      mojom::ConnectionAttemptFailureReason reason) override {
    last_connection_attempt_failure_reason_ = reason;
  }

  void OnConnection(std::unique_ptr<ClientChannel> channel) override {
    last_client_channel_ = std::move(channel);
  }

  base::Optional<mojom::ConnectionAttemptFailureReason>
  last_connection_attempt_failure_reason() {
    return last_connection_attempt_failure_reason_;
  }

  std::unique_ptr<ClientChannel> last_client_channel() {
    return std::move(last_client_channel_);
  }

 private:
  base::Optional<mojom::ConnectionAttemptFailureReason>
      last_connection_attempt_failure_reason_;
  std::unique_ptr<ClientChannel> last_client_channel_;
};

}  // namespace

class SecureChannelClientImplTest : public testing::Test {
 protected:
  SecureChannelClientImplTest()
      : test_remote_device_list_(
            cryptauth::CreateRemoteDeviceListForTest(kNumTestDevices)),
        test_remote_device_ref_list_(
            cryptauth::CreateRemoteDeviceRefListForTest(kNumTestDevices)) {}

  // testing::Test:
  void SetUp() override {
    auto fake_secure_channel = std::make_unique<FakeSecureChannel>();
    fake_secure_channel_ = fake_secure_channel.get();
    fake_secure_channel_impl_factory_ =
        std::make_unique<FakeSecureChannelImplFactory>(
            std::move(fake_secure_channel));
    SecureChannelImpl::Factory::SetFactoryForTesting(
        fake_secure_channel_impl_factory_.get());

    fake_connection_attempt_factory_ =
        std::make_unique<FakeConnectionAttemptFactory>();
    ConnectionAttemptImpl::Factory::SetFactoryForTesting(
        fake_connection_attempt_factory_.get());

    fake_client_channel_impl_factory_ =
        std::make_unique<FakeClientChannelImplFactory>();
    ClientChannelImpl::Factory::SetFactoryForTesting(
        fake_client_channel_impl_factory_.get());

    test_connection_attempt_delegate_ =
        std::make_unique<TestConnectionAttemptDelegate>();

    auto secure_channel_service = std::make_unique<SecureChannelService>();
    connector_factory_ =
        service_manager::TestConnectorFactory::CreateForUniqueService(
            std::move(secure_channel_service));

    connector_ = connector_factory_->CreateConnector();
    test_task_runner_ = base::MakeRefCounted<base::TestSimpleTaskRunner>();

    client_ = SecureChannelClientImpl::Factory::Get()->BuildInstance(
        connector_.get(), test_task_runner_);
  }

  void TearDown() override {
    SecureChannelImpl::Factory::SetFactoryForTesting(nullptr);
  }

  std::unique_ptr<FakeConnectionAttempt> CallListenForConnectionFromDevice(
      cryptauth::RemoteDeviceRef device_to_connect,
      cryptauth::RemoteDeviceRef local_device,
      const std::string& feature,
      ConnectionPriority connection_priority) {
    auto connection_attempt = client_->ListenForConnectionFromDevice(
        device_to_connect, local_device, feature, connection_priority);
    auto fake_connection_attempt = base::WrapUnique(
        static_cast<FakeConnectionAttempt*>(connection_attempt.release()));
    fake_connection_attempt->SetDelegate(
        test_connection_attempt_delegate_.get());

    test_task_runner_->RunUntilIdle();

    SendPendingMojoMessages();

    return fake_connection_attempt;
  }

  std::unique_ptr<FakeConnectionAttempt> CallInitiateConnectionToDevice(
      cryptauth::RemoteDeviceRef device_to_connect,
      cryptauth::RemoteDeviceRef local_device,
      const std::string& feature,
      ConnectionPriority connection_priority) {
    auto connection_attempt = client_->InitiateConnectionToDevice(
        device_to_connect, local_device, feature, connection_priority);
    auto fake_connection_attempt = base::WrapUnique(
        static_cast<FakeConnectionAttempt*>(connection_attempt.release()));
    fake_connection_attempt->SetDelegate(
        test_connection_attempt_delegate_.get());

    test_task_runner_->RunUntilIdle();

    SendPendingMojoMessages();

    return fake_connection_attempt;
  }

  void SendPendingMojoMessages() {
    static_cast<SecureChannelClientImpl*>(client_.get())->FlushForTesting();
  }

  const base::test::ScopedTaskEnvironment scoped_task_environment_;

  FakeSecureChannel* fake_secure_channel_;
  std::unique_ptr<FakeSecureChannelImplFactory>
      fake_secure_channel_impl_factory_;
  std::unique_ptr<FakeConnectionAttemptFactory>
      fake_connection_attempt_factory_;
  std::unique_ptr<FakeClientChannelImplFactory>
      fake_client_channel_impl_factory_;
  std::unique_ptr<TestConnectionAttemptDelegate>
      test_connection_attempt_delegate_;
  std::unique_ptr<service_manager::TestConnectorFactory> connector_factory_;
  std::unique_ptr<service_manager::Connector> connector_;
  scoped_refptr<base::TestSimpleTaskRunner> test_task_runner_;

  std::unique_ptr<SecureChannelClient> client_;

  const cryptauth::RemoteDeviceList test_remote_device_list_;
  const cryptauth::RemoteDeviceRefList test_remote_device_ref_list_;

 private:
  DISALLOW_COPY_AND_ASSIGN(SecureChannelClientImplTest);
};

TEST_F(SecureChannelClientImplTest, TestInitiateConnectionToDevice) {
  auto fake_connection_attempt = CallInitiateConnectionToDevice(
      test_remote_device_ref_list_[1], test_remote_device_ref_list_[0],
      "feature", ConnectionPriority::kLow);

  base::RunLoop run_loop;

  fake_connection_attempt->set_on_connection_callback(run_loop.QuitClosure());

  auto fake_channel = std::make_unique<FakeChannel>();
  mojom::MessageReceiverPtr message_receiver_ptr;

  fake_secure_channel_->delegate_from_last_initiate_call()->OnConnection(
      fake_channel->GenerateInterfacePtr(),
      mojo::MakeRequest(&message_receiver_ptr));

  run_loop.Run();

  EXPECT_EQ(fake_client_channel_impl_factory_->last_client_channel_created(),
            test_connection_attempt_delegate_->last_client_channel().get());
}

TEST_F(SecureChannelClientImplTest, TestInitiateConnectionToDevice_Failure) {
  auto fake_connection_attempt = CallInitiateConnectionToDevice(
      test_remote_device_ref_list_[1], test_remote_device_ref_list_[0],
      "feature", ConnectionPriority::kLow);

  base::RunLoop run_loop;

  fake_connection_attempt->set_on_connection_attempt_failure_callback(
      run_loop.QuitClosure());

  fake_secure_channel_->delegate_from_last_initiate_call()
      ->OnConnectionAttemptFailure(
          mojom::ConnectionAttemptFailureReason::AUTHENTICATION_ERROR);

  run_loop.Run();

  EXPECT_EQ(mojom::ConnectionAttemptFailureReason::AUTHENTICATION_ERROR,
            test_connection_attempt_delegate_
                ->last_connection_attempt_failure_reason());
}

TEST_F(SecureChannelClientImplTest, TestListenForConnectionFromDevice) {
  auto fake_connection_attempt = CallListenForConnectionFromDevice(
      test_remote_device_ref_list_[1], test_remote_device_ref_list_[0],
      "feature", ConnectionPriority::kLow);

  base::RunLoop run_loop;

  fake_connection_attempt->set_on_connection_callback(run_loop.QuitClosure());

  auto fake_channel = std::make_unique<FakeChannel>();
  mojom::MessageReceiverPtr message_receiver_ptr;

  fake_secure_channel_->delegate_from_last_listen_call()->OnConnection(
      fake_channel->GenerateInterfacePtr(),
      mojo::MakeRequest(&message_receiver_ptr));

  run_loop.Run();

  EXPECT_EQ(fake_client_channel_impl_factory_->last_client_channel_created(),
            test_connection_attempt_delegate_->last_client_channel().get());
}

TEST_F(SecureChannelClientImplTest, TestListenForConnectionFromDevice_Failure) {
  auto fake_connection_attempt = CallListenForConnectionFromDevice(
      test_remote_device_ref_list_[1], test_remote_device_ref_list_[0],
      "feature", ConnectionPriority::kLow);

  base::RunLoop run_loop;

  fake_connection_attempt->set_on_connection_attempt_failure_callback(
      run_loop.QuitClosure());

  fake_secure_channel_->delegate_from_last_listen_call()
      ->OnConnectionAttemptFailure(
          mojom::ConnectionAttemptFailureReason::AUTHENTICATION_ERROR);

  run_loop.Run();

  EXPECT_EQ(mojom::ConnectionAttemptFailureReason::AUTHENTICATION_ERROR,
            test_connection_attempt_delegate_
                ->last_connection_attempt_failure_reason());
}

TEST_F(SecureChannelClientImplTest, TestMultipleConnections) {
  auto fake_connection_attempt_1 = CallInitiateConnectionToDevice(
      test_remote_device_ref_list_[1], test_remote_device_ref_list_[0],
      "feature", ConnectionPriority::kLow);
  base::RunLoop run_loop_1;
  fake_connection_attempt_1->set_on_connection_callback(
      run_loop_1.QuitClosure());
  auto fake_channel_1 = std::make_unique<FakeChannel>();
  mojom::MessageReceiverPtr message_receiver_ptr_1;
  fake_secure_channel_->delegate_from_last_initiate_call()->OnConnection(
      fake_channel_1->GenerateInterfacePtr(),
      mojo::MakeRequest(&message_receiver_ptr_1));
  run_loop_1.Run();

  ClientChannel* client_channel_1 =
      test_connection_attempt_delegate_->last_client_channel().get();
  EXPECT_EQ(fake_client_channel_impl_factory_->last_client_channel_created(),
            client_channel_1);

  auto fake_connection_attempt_2 = CallListenForConnectionFromDevice(
      test_remote_device_ref_list_[2], test_remote_device_ref_list_[0],
      "feature", ConnectionPriority::kLow);
  base::RunLoop run_loop_2;
  fake_connection_attempt_2->set_on_connection_callback(
      run_loop_2.QuitClosure());
  auto fake_channel_2 = std::make_unique<FakeChannel>();
  mojom::MessageReceiverPtr message_receiver_ptr_2;
  fake_secure_channel_->delegate_from_last_listen_call()->OnConnection(
      fake_channel_2->GenerateInterfacePtr(),
      mojo::MakeRequest(&message_receiver_ptr_2));
  run_loop_2.Run();

  ClientChannel* client_channel_2 =
      test_connection_attempt_delegate_->last_client_channel().get();
  EXPECT_EQ(fake_client_channel_impl_factory_->last_client_channel_created(),
            client_channel_2);

  EXPECT_NE(client_channel_1, client_channel_2);
}

}  // namespace secure_channel

}  // namespace chromeos
