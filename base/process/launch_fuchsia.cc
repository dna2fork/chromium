// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/launch.h"

#include <lib/fdio/limits.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/util.h>
#include <stdint.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/fuchsia/default_job.h"
#include "base/fuchsia/fuchsia_logging.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/scoped_generic.h"

namespace base {

namespace {

bool GetAppOutputInternal(const CommandLine& cmd_line,
                          bool include_stderr,
                          std::string* output,
                          int* exit_code) {
  DCHECK(exit_code);

  LaunchOptions options;

  // LaunchProcess will automatically clone any stdio fd we do not explicitly
  // map.
  int pipe_fd[2];
  if (pipe(pipe_fd) < 0)
    return false;
  options.fds_to_remap.emplace_back(pipe_fd[1], STDOUT_FILENO);
  if (include_stderr)
    options.fds_to_remap.emplace_back(pipe_fd[1], STDERR_FILENO);

  Process process = LaunchProcess(cmd_line, options);
  close(pipe_fd[1]);
  if (!process.IsValid()) {
    close(pipe_fd[0]);
    return false;
  }

  output->clear();
  for (;;) {
    char buffer[256];
    ssize_t bytes_read = read(pipe_fd[0], buffer, sizeof(buffer));
    if (bytes_read <= 0)
      break;
    output->append(buffer, bytes_read);
  }
  close(pipe_fd[0]);

  return process.WaitForExit(exit_code);
}

ScopedZxHandle OpenHandleForPath(const FilePath& path) {
  if (!PathExists(path)) {
    DLOG(ERROR) << "Path does not exist: " << path;
    return ScopedZxHandle();
  }

  // Open the specified |path|.
  File dir(path, File::FLAG_OPEN | File::FLAG_READ);
  ScopedPlatformFile scoped_fd(dir.TakePlatformFile());

  // Unwrap |scoped_fd| into |handles|. Negative result indicates failure.
  zx_handle_t handles[FDIO_MAX_HANDLES] = {};
  uint32_t types[FDIO_MAX_HANDLES] = {};
  zx_status_t num_handles =
      fdio_transfer_fd(scoped_fd.get(), 0, handles, types);
  if (num_handles <= 0) {
    DCHECK_LT(num_handles, 0);
    ZX_LOG(ERROR, num_handles) << "fdio_transfer_fd";
    return ScopedZxHandle();
  }

  // fdio_transfer_fd() has torn-down the file-descriptor, on success.
  ignore_result(scoped_fd.release());

  // Wrap the returned handles, so they will be closed on error.
  ScopedZxHandle owned_handles[FDIO_MAX_HANDLES];
  for (int i = 0; i < FDIO_MAX_HANDLES; ++i)
    owned_handles[i] = ScopedZxHandle(handles[i]);

  // We expect a single handle, of type PA_FDIO_REMOTE.
  if (num_handles != 1 || types[0] != PA_FDIO_REMOTE) {
    LOG(ERROR) << "Path " << path.AsUTF8Unsafe() << " had " << num_handles
               << " handles, and type:" << types[0];
    return ScopedZxHandle();
  }

  return std::move(owned_handles[0]);
}

fdio_spawn_action_t FdioSpawnAction(uint32_t action) {
  fdio_spawn_action_t new_action = {};
  new_action.action = action;
  return new_action;
}

fdio_spawn_action_t FdioSpawnActionCloneFd(int local_fd, int target_fd) {
  fdio_spawn_action_t action = FdioSpawnAction(FDIO_SPAWN_ACTION_CLONE_FD);
  action.fd.local_fd = local_fd;
  action.fd.target_fd = target_fd;
  return action;
}

fdio_spawn_action_t FdioSpawnActionAddNamespaceEntry(const char* prefix,
                                                     zx_handle_t handle) {
  fdio_spawn_action_t action = FdioSpawnAction(FDIO_SPAWN_ACTION_ADD_NS_ENTRY);
  action.ns.prefix = prefix;
  action.ns.handle = handle;
  return action;
}

fdio_spawn_action_t FdioSpawnActionAddHandle(uint32_t id, zx_handle_t handle) {
  fdio_spawn_action_t action = FdioSpawnAction(FDIO_SPAWN_ACTION_ADD_HANDLE);
  action.h.id = id;
  action.h.handle = handle;
  return action;
}

}  // namespace

Process LaunchProcess(const CommandLine& cmdline,
                      const LaunchOptions& options) {
  return LaunchProcess(cmdline.argv(), options);
}

// TODO(768416): Investigate whether we can make LaunchProcess() create
// unprivileged processes by default (no implicit capabilities are granted).
Process LaunchProcess(const std::vector<std::string>& argv,
                      const LaunchOptions& options) {
  // fdio_spawn_etc() accepts an array of |fdio_spawn_action_t|, describing
  // namespace entries, descriptors and handles to launch the child process
  // with.
  std::vector<fdio_spawn_action_t> spawn_actions;

  // Handles to be transferred to the child are owned by this vector, so that
  // they they are closed on early-exit, and can be release()d otherwise.
  std::vector<ScopedZxHandle> transferred_handles;

  // Add caller-supplied handles for transfer. We must do this first to ensure
  // that the handles are consumed even if some later step fails.
  for (const auto& id_and_handle : options.handles_to_transfer) {
    spawn_actions.push_back(
        FdioSpawnActionAddHandle(id_and_handle.id, id_and_handle.handle));
    transferred_handles.emplace_back(id_and_handle.handle);
  }

  // Determine the job under which to launch the new process.
  zx_handle_t job = options.job_handle != ZX_HANDLE_INVALID ? options.job_handle
                                                            : GetDefaultJob();
  DCHECK_NE(ZX_HANDLE_INVALID, job);

  // Construct an |argv| array of C-strings from the supplied std::strings.
  std::vector<const char*> argv_cstr;
  argv_cstr.reserve(argv.size() + 1);
  for (const auto& arg : argv)
    argv_cstr.push_back(arg.c_str());
  argv_cstr.push_back(nullptr);

  // Determine the environment to pass to the new process.
  // If |clear_environ|, |environ| or |current_directory| are set then we
  // construct a new (possibly empty) environment, otherwise we let fdio_spawn()
  // clone the caller's environment into the new process.
  uint32_t spawn_flags = FDIO_SPAWN_CLONE_LDSVC | options.spawn_flags;

  EnvironmentMap environ_modifications = options.environ;
  if (!options.current_directory.empty()) {
    environ_modifications["PWD"] = options.current_directory.value();
  } else {
    FilePath cwd;
    GetCurrentDirectory(&cwd);
    environ_modifications["PWD"] = cwd.value();
  }

  std::unique_ptr<char* []> new_environ;
  if (!environ_modifications.empty()) {
    char* const empty_environ = nullptr;
    char* const* old_environ = options.clear_environ ? &empty_environ : environ;
    new_environ = AlterEnvironment(old_environ, environ_modifications);
  } else if (!options.clear_environ) {
    spawn_flags |= FDIO_SPAWN_CLONE_ENVIRON;
  }

  // Add actions to clone handles for any specified paths into the new process'
  // namespace.
  std::vector<const char*> mapped_paths_cstr;
  if (!options.paths_to_map.empty()) {
    DCHECK((options.spawn_flags & FDIO_SPAWN_CLONE_NAMESPACE) == 0);
    mapped_paths_cstr.reserve(options.paths_to_map.size());
    transferred_handles.reserve(transferred_handles.size() +
                                options.paths_to_map.size());

    for (auto& path_to_map : options.paths_to_map) {
      ScopedZxHandle handle(OpenHandleForPath(path_to_map));
      if (!handle)
        return Process();
      spawn_actions.push_back(FdioSpawnActionAddNamespaceEntry(
          path_to_map.value().c_str(), handle.get()));
      mapped_paths_cstr.push_back(path_to_map.value().c_str());
      transferred_handles.push_back(std::move(handle));
    }
  }

  // Add any file-descriptors to be cloned into the new process.
  // Note that if FDIO_SPAWN_CLONE_STDIO is set, then any stdio entries in
  // |fds_to_remap| will be used in place of the parent process' descriptors.
  for (const auto& src_target : options.fds_to_remap) {
    spawn_actions.push_back(
        FdioSpawnActionCloneFd(src_target.first, src_target.second));
  }

  ScopedZxHandle process_handle;
  // fdio_spawn_etc() will write a null-terminated scring to |error_message| in
  // case of failure, so we avoid unnecessarily initializing it here.
  char error_message[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_status_t status = fdio_spawn_etc(
      job, spawn_flags, argv_cstr[0], argv_cstr.data(), new_environ.get(),
      spawn_actions.size(), spawn_actions.data(), process_handle.receive(),
      error_message);

  // fdio_spawn_etc() will close all handles specified in add-handle actions,
  // regardless of whether it succeeds or fails, so release our copies.
  for (auto& transferred_handle : transferred_handles)
    ignore_result(transferred_handle.release());

  if (status != ZX_OK) {
    ZX_LOG(ERROR, status) << "fdio_spawn: " << error_message;
    return Process();
  }

  // Wrap the handle into a Process, and wait for it to terminate, if requested.
  Process process(process_handle.release());
  if (options.wait) {
    status = zx_object_wait_one(process.Handle(), ZX_TASK_TERMINATED,
                                ZX_TIME_INFINITE, nullptr);
    ZX_DCHECK(status == ZX_OK, status) << "zx_object_wait_one";
  }

  return process;
}

bool GetAppOutput(const CommandLine& cl, std::string* output) {
  int exit_code;
  bool result = GetAppOutputInternal(cl, false, output, &exit_code);
  return result && exit_code == EXIT_SUCCESS;
}

bool GetAppOutput(const std::vector<std::string>& argv, std::string* output) {
  return GetAppOutput(CommandLine(argv), output);
}

bool GetAppOutputAndError(const CommandLine& cl, std::string* output) {
  int exit_code;
  bool result = GetAppOutputInternal(cl, true, output, &exit_code);
  return result && exit_code == EXIT_SUCCESS;
}

bool GetAppOutputAndError(const std::vector<std::string>& argv,
                          std::string* output) {
  return GetAppOutputAndError(CommandLine(argv), output);
}

bool GetAppOutputWithExitCode(const CommandLine& cl,
                              std::string* output,
                              int* exit_code) {
  // Contrary to GetAppOutput(), |true| return here means that the process was
  // launched and the exit code was waited upon successfully, but not
  // necessarily that the exit code was EXIT_SUCCESS.
  return GetAppOutputInternal(cl, false, output, exit_code);
}

void RaiseProcessToHighPriority() {
  // Fuchsia doesn't provide an API to change process priority.
}

}  // namespace base
