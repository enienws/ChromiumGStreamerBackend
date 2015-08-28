// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VARIATIONS_SERVICE_VARIATIONS_SERVICE_CLIENT_H_
#define COMPONENTS_VARIATIONS_SERVICE_VARIATIONS_SERVICE_CLIENT_H_

#include <string>

#include "base/callback.h"
#include "base/strings/string16.h"
#include "base/version.h"
#include "components/version_info/version_info.h"

namespace base {
class SequencedWorkerPool;
}

namespace net {
class URLRequestContextGetter;
}

namespace network_time {
class NetworkTimeTracker;
}

namespace chrome_variations {

// An abstraction of operations that depend on the embedder's (e.g. Chrome)
// environment.
class VariationsServiceClient {
 public:
  virtual ~VariationsServiceClient() {}

  // Returns the current application locale (e.g. "en-US").
  virtual std::string GetApplicationLocale() = 0;

  // Returns the SequencedWorkerPool on which the VariationsService should run
  // tasks that may block.
  virtual base::SequencedWorkerPool* GetBlockingPool() = 0;

  // Returns a callback that when run returns the base::Version to use for
  // variations seed simulation. VariationsService guarantees that the callback
  // will be run on the pool returned by
  // VariationsServiceClient::GetBlockingPool().
  virtual base::Callback<base::Version(void)>
  GetVersionForSimulationCallback() = 0;

  virtual net::URLRequestContextGetter* GetURLRequestContext() = 0;
  virtual network_time::NetworkTimeTracker* GetNetworkTimeTracker() = 0;

  // Gets the channel of the embedder.
  virtual version_info::Channel GetChannel() = 0;

  // Allows the embedder to override the string resource specified by |hash|
  // with |string| in the resource bundle if desired.
  virtual void OverrideUIString(uint32_t hash,
                                const base::string16& string) = 0;

  // Called from VariationsService::PerformPreMainMessageLoopStartup().
  virtual void OnInitialStartup() {}
};

}  // namespace chrome_variations

#endif  // COMPONENTS_VARIATIONS_SERVICE_VARIATIONS_SERVICE_CLIENT_H_
