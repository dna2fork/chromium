// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/test/multiprocess_test_helper.h"

#include <functional>
#include <set>
#include <utility>

#include "base/base_paths.h"
#include "base/base_switches.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/path_service.h"
#include "base/process/kill.h"
#include "base/process/process_handle.h"
#include "base/rand_util.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/embedder/named_platform_handle.h"
#include "mojo/edk/embedder/named_platform_handle_utils.h"
#include "mojo/edk/embedder/peer_connection.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "mojo/edk/embedder/platform_handle_utils.h"
#include "mojo/public/cpp/platform/named_platform_channel.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include "mojo/public/cpp/platform/platform_channel_endpoint.h"
#include "mojo/public/cpp/platform/platform_channel_server_endpoint.h"
#include "mojo/public/cpp/system/invitation.h"
#include "mojo/public/cpp/system/platform_handle.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_MACOSX) && !defined(OS_IOS)
#include "base/mac/mach_port_broker.h"
#endif

namespace mojo {
namespace edk {
namespace test {

namespace {

const char kNamedPipeName[] = "named-pipe-name";
const char kRunAsBrokerClient[] = "run-as-broker-client";

const char kTestChildMessagePipeName[] = "test_pipe";

// For use (and only valid) in a test child process:
base::LazyInstance<PeerConnection>::Leaky g_child_peer_connection;

template <typename Func>
int RunClientFunction(Func handler, bool pass_pipe_ownership_to_main) {
  CHECK(MultiprocessTestHelper::primordial_pipe.is_valid());
  ScopedMessagePipeHandle pipe =
      std::move(MultiprocessTestHelper::primordial_pipe);
  MessagePipeHandle pipe_handle =
      pass_pipe_ownership_to_main ? pipe.release() : pipe.get();
  return handler(pipe_handle.value());
}

// TODO(https://844763): Clean this up. Some test code still relies on old EDK
// APIs and thus internal platform handle types. We try to use the new public
// types as much as possible, so this does a reliable conversion from the new
// type to the old type.
ScopedInternalPlatformHandle PlatformHandleToScopedInternalPlatformHandle(
    PlatformHandle handle) {
  MojoPlatformHandle platform_handle;
  PlatformHandleToMojoPlatformHandle(std::move(handle), &platform_handle);
  ScopedInternalPlatformHandle edk_handle;
  MojoPlatformHandleToScopedInternalPlatformHandle(&platform_handle,
                                                   &edk_handle);
  return edk_handle;
}

}  // namespace

MultiprocessTestHelper::MultiprocessTestHelper() {}

MultiprocessTestHelper::~MultiprocessTestHelper() {
  CHECK(!test_child_.IsValid());
}

ScopedMessagePipeHandle MultiprocessTestHelper::StartChild(
    const std::string& test_child_name,
    LaunchType launch_type) {
  return StartChildWithExtraSwitch(test_child_name, std::string(),
                                   std::string(), launch_type);
}

ScopedMessagePipeHandle MultiprocessTestHelper::StartChildWithExtraSwitch(
    const std::string& test_child_name,
    const std::string& switch_string,
    const std::string& switch_value,
    LaunchType launch_type) {
  CHECK(!test_child_name.empty());
  CHECK(!test_child_.IsValid());

  std::string test_child_main = test_child_name + "TestChildMain";

  // Manually construct the new child's commandline to avoid copying unwanted
  // values.
  base::CommandLine command_line(
      base::GetMultiProcessTestChildBaseCommandLine().GetProgram());

  std::set<std::string> uninherited_args;
  uninherited_args.insert("mojo-platform-channel-handle");
  uninherited_args.insert(switches::kTestChildProcess);

  // Copy commandline switches from the parent process, except for the
  // multiprocess client name and mojo message pipe handle; this allows test
  // clients to spawn other test clients.
  for (const auto& entry :
       base::CommandLine::ForCurrentProcess()->GetSwitches()) {
    if (uninherited_args.find(entry.first) == uninherited_args.end())
      command_line.AppendSwitchNative(entry.first, entry.second);
  }

  mojo::PlatformChannel channel;
  mojo::NamedPlatformChannel::ServerName server_name;
  base::LaunchOptions options;
  if (launch_type == LaunchType::CHILD || launch_type == LaunchType::PEER) {
#if defined(OS_FUCHSIA)
    channel.PrepareToPassRemoteEndpoint(&options.handles_to_transfer,
                                        &command_line);
#elif defined(OS_POSIX)
    channel.PrepareToPassRemoteEndpoint(&options.fds_to_remap, &command_line);
#elif defined(OS_WIN)
    channel.PrepareToPassRemoteEndpoint(&options.handles_to_inherit,
                                        &command_line);
#else
#error "Platform not yet supported."
#endif
  } else if (launch_type == LaunchType::NAMED_CHILD ||
             launch_type == LaunchType::NAMED_PEER) {
#if defined(OS_FUCHSIA)
    // TODO(fuchsia): Implement named channels. See crbug.com/754038.
    NOTREACHED();
#elif defined(OS_POSIX)
    base::FilePath temp_dir;
    CHECK(base::PathService::Get(base::DIR_TEMP, &temp_dir));
    server_name =
        temp_dir.AppendASCII(base::NumberToString(base::RandUint64())).value();
#elif defined(OS_WIN)
    server_name = base::NumberToString16(base::RandUint64());
#else
#error "Platform not yet supported."
#endif
    command_line.AppendSwitchNative(kNamedPipeName, server_name);
  }

  if (!switch_string.empty()) {
    CHECK(!command_line.HasSwitch(switch_string));
    if (!switch_value.empty())
      command_line.AppendSwitchASCII(switch_string, switch_value);
    else
      command_line.AppendSwitch(switch_string);
  }

#if defined(OS_WIN)
  options.start_hidden = true;
#endif

  // NOTE: In the case of named pipes, it's important that the server handle be
  // created before the child process is launched; otherwise the server binding
  // the pipe path can race with child's connection to the pipe.
  PlatformChannelEndpoint local_channel_endpoint;
  PlatformChannelServerEndpoint server_endpoint;
  if (launch_type == LaunchType::CHILD || launch_type == LaunchType::PEER) {
    local_channel_endpoint = channel.TakeLocalEndpoint();
  } else if (launch_type == LaunchType::NAMED_CHILD ||
             launch_type == LaunchType::NAMED_PEER) {
    NamedPlatformChannel::Options options;
    options.server_name = server_name;
    NamedPlatformChannel named_channel(options);
    server_endpoint = named_channel.TakeServerEndpoint();
  }

  OutgoingInvitation child_invitation;
  ScopedMessagePipeHandle pipe;
  if (launch_type == LaunchType::CHILD ||
      launch_type == LaunchType::NAMED_CHILD) {
    pipe = child_invitation.AttachMessagePipe(kTestChildMessagePipeName);
    command_line.AppendSwitch(kRunAsBrokerClient);
  } else if (launch_type == LaunchType::PEER ||
             launch_type == LaunchType::NAMED_PEER) {
    peer_connection_ = std::make_unique<PeerConnection>();

    // TODO(https://844763): Either move peer connection into the C API or
    // (preferably) get rid of it altogether. For now we do this dance to get
    // the new public handle types to work with the peer connection API.
    ScopedInternalPlatformHandle local_handle;
    if (local_channel_endpoint.is_valid()) {
      local_handle = PlatformHandleToScopedInternalPlatformHandle(
          local_channel_endpoint.TakePlatformHandle());
    } else {
#if defined(OS_POSIX) || defined(OS_WIN)
      DCHECK(server_endpoint.is_valid());
      local_handle = PlatformHandleToScopedInternalPlatformHandle(
          server_endpoint.TakePlatformHandle());
      local_handle.get().needs_connection = true;
#else
      NOTREACHED();
#endif
    }
    pipe = peer_connection_->Connect(
        ConnectionParams(TransportProtocol::kLegacy, std::move(local_handle)));
  }

  test_child_ =
      base::SpawnMultiProcessTestChild(test_child_main, command_line, options);
  if (launch_type == LaunchType::CHILD || launch_type == LaunchType::PEER)
    channel.RemoteProcessLaunchAttempted();

  if (launch_type == LaunchType::CHILD) {
    DCHECK(local_channel_endpoint.is_valid());
    OutgoingInvitation::Send(std::move(child_invitation), test_child_.Handle(),
                             std::move(local_channel_endpoint),
                             process_error_callback_);
  } else if (launch_type == LaunchType::NAMED_CHILD) {
    DCHECK(server_endpoint.is_valid());
    OutgoingInvitation::Send(std::move(child_invitation), test_child_.Handle(),
                             std::move(server_endpoint),
                             process_error_callback_);
  }

  CHECK(test_child_.IsValid());
  return pipe;
}

int MultiprocessTestHelper::WaitForChildShutdown() {
  CHECK(test_child_.IsValid());

  int rv = -1;
  WaitForMultiprocessTestChildExit(test_child_, TestTimeouts::action_timeout(),
                                   &rv);
  test_child_.Close();
  return rv;
}

bool MultiprocessTestHelper::WaitForChildTestShutdown() {
  return WaitForChildShutdown() == 0;
}

// static
void MultiprocessTestHelper::ChildSetup() {
  CHECK(base::CommandLine::InitializedForCurrentProcess());

  auto& command_line = *base::CommandLine::ForCurrentProcess();
  NamedPlatformChannel::ServerName named_pipe(
      command_line.GetSwitchValueNative(kNamedPipeName));
  if (command_line.HasSwitch(kRunAsBrokerClient)) {
    mojo::IncomingInvitation invitation;
#if defined(OS_MACOSX) && !defined(OS_IOS)
    CHECK(base::MachPortBroker::ChildSendTaskPortToParent("mojo_test"));
#endif
    if (!named_pipe.empty()) {
      invitation = mojo::IncomingInvitation::Accept(
          mojo::NamedPlatformChannel::ConnectToServer(named_pipe));
    } else {
      auto endpoint =
          mojo::PlatformChannel::RecoverPassedEndpointFromCommandLine(
              command_line);
      invitation = IncomingInvitation::Accept(std::move(endpoint));
    }
    primordial_pipe = invitation.ExtractMessagePipe(kTestChildMessagePipeName);
  } else {
    if (!named_pipe.empty()) {
      NamedPlatformHandle pipe_name(named_pipe);
      primordial_pipe = g_child_peer_connection.Get().Connect(ConnectionParams(
          TransportProtocol::kLegacy, CreateClientHandle(pipe_name)));
    } else {
      primordial_pipe = g_child_peer_connection.Get().Connect(ConnectionParams(
          TransportProtocol::kLegacy,
          PlatformChannelPair::PassClientHandleFromParentProcess(
              command_line)));
    }
  }
}

// static
int MultiprocessTestHelper::RunClientMain(
    const base::Callback<int(MojoHandle)>& main,
    bool pass_pipe_ownership_to_main) {
  return RunClientFunction(
      [main](MojoHandle handle) { return main.Run(handle); },
      pass_pipe_ownership_to_main);
}

// static
int MultiprocessTestHelper::RunClientTestMain(
    const base::Callback<void(MojoHandle)>& main) {
  return RunClientFunction(
      [main](MojoHandle handle) {
        main.Run(handle);
        return (::testing::Test::HasFatalFailure() ||
                ::testing::Test::HasNonfatalFailure())
                   ? 1
                   : 0;
      },
      true /* pass_pipe_ownership_to_main */);
}

// static
mojo::ScopedMessagePipeHandle MultiprocessTestHelper::primordial_pipe;

}  // namespace test
}  // namespace edk
}  // namespace mojo
