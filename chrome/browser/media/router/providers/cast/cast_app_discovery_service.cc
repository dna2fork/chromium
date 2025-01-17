// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/router/providers/cast/cast_app_discovery_service.h"

#include "base/time/tick_clock.h"
#include "chrome/browser/media/router/providers/cast/cast_media_route_provider_metrics.h"
#include "components/cast_channel/cast_message_handler.h"
#include "components/cast_channel/cast_socket.h"
#include "components/cast_channel/cast_socket_service.h"

namespace media_router {

namespace {

// The minimum time that must elapse before an app availability result can be
// force refreshed.
static constexpr base::TimeDelta kRefreshThreshold =
    base::TimeDelta::FromMinutes(1);

bool ShouldRefreshAppAvailability(
    const CastAppAvailabilityTracker::AppAvailability& availability,
    base::TimeTicks now) {
  switch (availability.first) {
    case cast_channel::GetAppAvailabilityResult::kAvailable:
      return false;
    case cast_channel::GetAppAvailabilityResult::kUnavailable:
      return now - availability.second > kRefreshThreshold;
    case cast_channel::GetAppAvailabilityResult::kUnknown:
      return true;
  }

  NOTREACHED();
  return false;
}

bool HasAllRequiredCapabilities(int required_capabilities,
                                const MediaSinkInternal& sink) {
  return (required_capabilities & sink.cast_data().capabilities) ==
         required_capabilities;
}

}  // namespace

CastAppDiscoveryServiceImpl::CastAppDiscoveryServiceImpl(
    cast_channel::CastMessageHandler* message_handler,
    cast_channel::CastSocketService* socket_service,
    MediaSinkServiceBase* media_sink_service,
    const base::TickClock* clock)
    : message_handler_(message_handler),
      socket_service_(socket_service),
      media_sink_service_(media_sink_service),
      clock_(clock),
      weak_ptr_factory_(this) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  DCHECK(message_handler_);
  DCHECK(socket_service_);
  DCHECK(clock_);
  socket_service_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&CastAppDiscoveryServiceImpl::Init,
                                base::Unretained(this)));
}

CastAppDiscoveryServiceImpl::~CastAppDiscoveryServiceImpl() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  media_sink_service_->RemoveObserver(this);
}

CastAppDiscoveryService::Subscription
CastAppDiscoveryServiceImpl::StartObservingMediaSinks(
    const CastMediaSource& source,
    const SinkQueryCallback& callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const MediaSource::Id& source_id = source.source_id();

  // Return cached results immediately, if available.
  auto cached_sinks = GetAvailableSinks(source);
  if (!cached_sinks.empty())
    callback.Run(source_id, cached_sinks);

  auto& callback_list = sink_queries_[source_id];
  if (!callback_list) {
    callback_list = std::make_unique<SinkQueryCallbackList>();
    callback_list->set_removal_callback(base::BindRepeating(
        &CastAppDiscoveryServiceImpl::MaybeRemoveSinkQueryEntry,
        base::Unretained(this), source));

    // Note: even though we retain availability results for an app unregistered
    // from the tracker, we will send app availability requests again when it
    // is re-registered. This gives us a chance to refresh the results in case
    // it changed.
    base::flat_set<std::string> new_app_ids =
        availability_tracker_.RegisterSource(source);
    const auto& sinks = media_sink_service_->GetSinks();
    for (const auto& app_id : new_app_ids) {
      // Note: The following logic assumes |sinks| will not change as it is
      // being iterated.
      for (const auto& sink : sinks) {
        int channel_id = sink.second.cast_data().cast_channel_id;
        cast_channel::CastSocket* socket =
            socket_service_->GetSocket(channel_id);
        if (!socket) {
          DVLOG(1) << "Socket not found for id " << channel_id;
          continue;
        }

        RequestAppAvailability(socket, app_id, sink.first);
      }
    }
  }

  return callback_list->Add(callback);
}

void CastAppDiscoveryServiceImpl::Refresh() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto app_ids = availability_tracker_.GetRegisteredApps();
  base::TimeTicks now = clock_->NowTicks();
  const auto& sinks = media_sink_service_->GetSinks();
  // Note: The following logic assumes |sinks| will not change as it is
  // being iterated.
  for (const auto& sink : sinks) {
    for (const auto& app_id : app_ids) {
      if (ShouldRefreshAppAvailability(
              availability_tracker_.GetAvailability(sink.first, app_id), now)) {
        int channel_id = sink.second.cast_data().cast_channel_id;
        cast_channel::CastSocket* socket =
            socket_service_->GetSocket(channel_id);
        if (!socket) {
          DVLOG(1) << "Socket not found for id " << channel_id;
          continue;
        }

        RequestAppAvailability(socket, app_id, sink.first);
      }
    }
  }
}

void CastAppDiscoveryServiceImpl::MaybeRemoveSinkQueryEntry(
    const CastMediaSource& source) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = sink_queries_.find(source.source_id());
  CHECK(it != sink_queries_.end());

  if (it->second->empty()) {
    availability_tracker_.UnregisterSource(source);
    sink_queries_.erase(it);
  }
}

void CastAppDiscoveryServiceImpl::Init() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  media_sink_service_->AddObserver(this);
}

void CastAppDiscoveryServiceImpl::OnSinkAddedOrUpdated(
    const MediaSinkInternal& sink) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  cast_channel::CastSocket* socket =
      socket_service_->GetSocket(sink.cast_data().cast_channel_id);
  if (!socket)
    return;

  const MediaSink::Id& sink_id = sink.sink().id();

  // Any queries that currently contains this sink should be updated.
  UpdateSinkQueries(availability_tracker_.GetSupportedSources(sink_id));

  for (const std::string& app_id : availability_tracker_.GetRegisteredApps()) {
    auto availability = availability_tracker_.GetAvailability(sink_id, app_id);
    if (availability.first != cast_channel::GetAppAvailabilityResult::kUnknown)
      continue;

    RequestAppAvailability(socket, app_id, sink_id);
  }
}

void CastAppDiscoveryServiceImpl::OnSinkRemoved(const MediaSinkInternal& sink) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const MediaSink::Id& sink_id = sink.sink().id();
  UpdateSinkQueries(availability_tracker_.RemoveResultsForSink(sink_id));
}

void CastAppDiscoveryServiceImpl::RequestAppAvailability(
    cast_channel::CastSocket* socket,
    const std::string& app_id,
    const MediaSink::Id& sink_id) {
  message_handler_->RequestAppAvailability(
      socket, app_id,
      base::BindOnce(&CastAppDiscoveryServiceImpl::UpdateAppAvailability,
                     weak_ptr_factory_.GetWeakPtr(), clock_->NowTicks(),
                     sink_id));
}

void CastAppDiscoveryServiceImpl::UpdateAppAvailability(
    base::TimeTicks start_time,
    const MediaSink::Id& sink_id,
    const std::string& app_id,
    cast_channel::GetAppAvailabilityResult availability) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RecordAppAvailabilityResult(availability, clock_->NowTicks() - start_time);
  if (!media_sink_service_->GetSinkById(sink_id))
    return;

  DVLOG(1) << "App " << app_id << " on sink " << sink_id << " is "
           << cast_channel::GetAppAvailabilityResultToString(availability);

  UpdateSinkQueries(availability_tracker_.UpdateAppAvailability(
      sink_id, app_id, {availability, clock_->NowTicks()}));
}

void CastAppDiscoveryServiceImpl::UpdateSinkQueries(
    const std::vector<CastMediaSource>& sources) {
  for (const auto& source : sources) {
    const MediaSource::Id& source_id = source.source_id();
    auto it = sink_queries_.find(source_id);
    if (it == sink_queries_.end())
      continue;
    it->second->Notify(source_id, GetAvailableSinks(source));
  }
}

bool CastAppDiscoveryServiceImpl::SinkSupportsSource(
    const MediaSinkInternal& sink,
    const CastMediaSource& source) const {
  const auto& app_infos = source.app_infos();
  for (const auto& app_info : app_infos) {
    auto required_capabilities = app_info.required_capabilities;
    auto availability =
        availability_tracker_.GetAvailability(sink.sink().id(), app_info.app_id)
            .first;
    if (availability == cast_channel::GetAppAvailabilityResult::kAvailable &&
        HasAllRequiredCapabilities(required_capabilities, sink)) {
      return true;
    }
  }
  return false;
}

std::vector<MediaSinkInternal> CastAppDiscoveryServiceImpl::GetAvailableSinks(
    const CastMediaSource& source) const {
  const auto& sinks = media_sink_service_->GetSinks();
  std::vector<MediaSinkInternal> available_sinks;
  for (const auto& sink : sinks) {
    if (SinkSupportsSource(sink.second, source))
      available_sinks.push_back(sink.second);
  }
  return available_sinks;
}

}  // namespace media_router
