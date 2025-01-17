// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/loader/modulescript/module_tree_linker_registry.h"

#include "third_party/blink/renderer/core/loader/modulescript/module_tree_linker.h"
#include "third_party/blink/renderer/core/script/settings_object.h"
#include "third_party/blink/renderer/platform/weborigin/kurl.h"
#include "third_party/blink/renderer/platform/weborigin/kurl_hash.h"

namespace blink {

void ModuleTreeLinkerRegistry::Trace(blink::Visitor* visitor) {
  visitor->Trace(active_tree_linkers_);
}

ModuleTreeLinker* ModuleTreeLinkerRegistry::Fetch(
    const KURL& url,
    const SettingsObject& fetch_client_settings_object,
    const KURL& base_url,
    WebURLRequest::RequestContext destination,
    const ScriptFetchOptions& options,
    Modulator* modulator,
    ModuleTreeClient* client) {
  ModuleTreeLinker* fetcher =
      ModuleTreeLinker::Fetch(url, fetch_client_settings_object, base_url,
                              destination, options, modulator, this, client);
  DCHECK(fetcher->IsFetching());
  active_tree_linkers_.insert(fetcher);
  return fetcher;
}

ModuleTreeLinker* ModuleTreeLinkerRegistry::FetchDescendantsForInlineScript(
    ModuleScript* module_script,
    const SettingsObject& fetch_client_settings_object,
    WebURLRequest::RequestContext destination,
    Modulator* modulator,
    ModuleTreeClient* client) {
  ModuleTreeLinker* fetcher = ModuleTreeLinker::FetchDescendantsForInlineScript(
      module_script, fetch_client_settings_object, destination, modulator, this,
      client);
  DCHECK(fetcher->IsFetching());
  active_tree_linkers_.insert(fetcher);
  return fetcher;
}

void ModuleTreeLinkerRegistry::ReleaseFinishedFetcher(
    ModuleTreeLinker* fetcher) {
  DCHECK(fetcher->HasFinished());

  auto it = active_tree_linkers_.find(fetcher);
  DCHECK_NE(it, active_tree_linkers_.end());
  active_tree_linkers_.erase(it);
}

}  // namespace blink
