/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef PAGESPEED_SYSTEM_ADMIN_SITE_H_
#define PAGESPEED_SYSTEM_ADMIN_SITE_H_

#include <atomic>
#include <cstddef>
#include <set>

#include "pagespeed/kernel/base/basictypes.h"
#include "pagespeed/kernel/base/string_util.h"

namespace net_instaweb {

class AsyncFetch;
class CacheInterface;
class GoogleUrl;
class HTTPCache;
class MessageHandler;
class PropertyCache;
class QueryParams;
class RewriteOptions;
class ServerContext;
class StaticAssetManager;
class Statistics;
class SystemCachePath;
class SystemCaches;
class SystemRewriteOptions;
class Timer;
class Writer;

// Bounds on the statistics-log query of the graphs and console JSON leaves.
// A finer granularity than the lower bound returns no more useful data and
// costs more to compute; a query may also name only so many variables.
const int64 kMinGraphGranularityMs = 1000;
const int64 kMaxGraphGranularityMs = 86400000;  // one day
const size_t kMaxGraphVarTitles = 64;

// The statistics-log query a graphs/console JSON request asks for, after the
// bounds above are applied.
struct GraphQuery {
  GraphQuery()
      : start_time(0),
        end_time(0),
        granularity_ms(3000),
        dump_for_graphs(true) {
  }
  int64 start_time;
  int64 end_time;
  int64 granularity_ms;
  // true: the set of variables the graphs page uses; false: `var_titles`.
  bool dump_for_graphs;
  std::set<GoogleString> var_titles;
};

// Reads start_time, end_time, granularity and var_titles from `params`.
// Missing values take their defaults (the whole log up to `now_ms` at the
// default logging granularity); granularity is held to
// [kMinGraphGranularityMs, kMaxGraphGranularityMs], times to be non-negative
// and not after one day past `now_ms`, and at most kMaxGraphVarTitles
// variable names are kept.
GraphQuery ParseGraphQuery(const QueryParams& params, int64 now_ms);

// The admin reads that cost the server real work, each admitted one at a time
// with a short wait for the slot, except where the read may complete
// asynchronously (see AdminSite::AcquireExpensiveSlot).
enum ExpensiveLeaf {
  kGraphs = 0,   // graphs/console JSON: reads the statistics log
  kConfig,       // configuration dump
  kMessages,     // plain message history
  kCacheLookup,  // cache?url=: builds a rewrite driver per lookup
  kCacheStatus,  // global cache listing: queries memcached/redis servers
  kNumLeaves,    // Sentinel; keep last.
};

// How long a request waits for a busy slot before it is answered 429.
const int kExpensiveSlotWaitMs = 100;

// Implements the /pagespeed_admin pages.
class AdminSite {
 public:
  // Identifies whether the user arrived at an admin page from a
  // /pagespeed_admin handler or a /*_pagespeed_statistics handler.
  // The main difference between these is that the _admin site might in the
  // future grant more privileges than the statistics site did, such as flushing
  // cache.  But it also affects the syntax of the links created to sub-pages
  // in the top navigation bar.
  enum AdminSource { kPageSpeedAdmin, kStatistics, kOther };

  AdminSite(StaticAssetManager* static_asset_manager, Timer* timer,
            MessageHandler* message_handler);

  ~AdminSite() {}

  // Handler which serves PSOL console.
  // Note: ConsoleHandler always succeeds.
  void ConsoleHandler(const SystemRewriteOptions& global_options,
                      const RewriteOptions& options, AdminSource source,
                      const QueryParams& query_params, AsyncFetch* fetch,
                      Statistics* statistics);

  // Displays recent Info/Warning/Error messages.: public ServerContext
  void MessageHistoryHandler(const RewriteOptions& options, AdminSource source,
                             AsyncFetch* fetch, const GoogleString* stored = nullptr,
                             StringPiece controls = "", StringPiece kind = "messages");

  // Deprecated handler for graphs in the PSOL console.
  void StatisticsGraphsHandler(
      Writer* writer, SystemRewriteOptions* global_system_rewrite_options);

  // Handle a request for /pagespeed_admin/*, which is a launching
  // point for all the administrator pages including stats,
  // message-histogram, console, etc.
  void AdminPage(bool is_global, const GoogleUrl& stripped_gurl,
                 const QueryParams& query_params, const RewriteOptions* options,
                 SystemCachePath* cache_path, AsyncFetch* fetch,
                 SystemCaches* system_caches,
                 CacheInterface* filesystem_metadata_cache,
                 HTTPCache* http_cache, CacheInterface* metadata_cache,
                 PropertyCache* page_property_cache,
                 ServerContext* server_context, Statistics* statistics,
                 Statistics* stats,
                 SystemRewriteOptions* global_system_rewrite_options);

  // Handle a request for the legacy /*_pagespeed_statistics page, which also
  // serves as a launching point for a subset of the admin pages.  Because the
  // admin pages are not uniformly sensitive, an existing PageSpeed user might
  // have granted public access to /mod_pagespeed_statistics, but we don't
  // want that to automatically imply access to the server cache.
  void StatisticsPage(bool is_global, const QueryParams& query_params,
                      const RewriteOptions* options, AsyncFetch* fetch,
                      SystemCaches* system_caches,
                      CacheInterface* filesystem_metadata_cache,
                      HTTPCache* http_cache, CacheInterface* metadata_cache,
                      PropertyCache* page_property_cache,
                      ServerContext* server_context, Statistics* statistics,
                      Statistics* stats,
                      SystemRewriteOptions* global_system_rewrite_options);

  // Returns JSON used by the PageSpeed Console JavaScript.
  void ConsoleJsonHandler(const QueryParams& params, AsyncFetch* fetch,
                          Statistics* statistics);

  // Handler for /mod_pagespeed_statistics and
  // /ngx_pagespeed_statistics, as well as
  // /...pagespeed__global_statistics.  If the latter,
  // is_global_request should be true.
  void StatisticsHandler(const RewriteOptions& options, AdminSource source,
                         AsyncFetch* fetch, Statistics* stats);

  // Displays a summary of PageSpeed runtime and optimization savings.
  void DashboardHandler(AdminSource source, AsyncFetch* fetch,
                        Statistics* stats,
                        const SystemRewriteOptions* archive_options = nullptr,
                        const GoogleString* recent_images = nullptr);

  // Displays configuration and health for the shared history archive.
  void HistoryArchiveHandler(AdminSource source, AsyncFetch* fetch,
                             const SystemRewriteOptions& options);

  // Lists recently rewritten images from the bounded PageSpeed message buffer.
  void ImagesHandler(AdminSource source, AsyncFetch* fetch,
                     const GoogleString* stored = nullptr, StringPiece controls = "");

  // Recent HTML cache publications; not a live backend inventory.
  void HtmlCacheHandler(AdminSource source, AsyncFetch* fetch,
                        Statistics* stats, const RewriteOptions& options,
                        const GoogleString* stored = nullptr, StringPiece controls = "",
                        const SystemRewriteOptions* archive_options = nullptr);

  // Responds to 'fetch' with data used on statistics page and graphs page
  // in JSON format.
  void StatisticsJsonHandler(AsyncFetch* fetch, Statistics* stats);

  // Display various charts on graphs page.
  // TODO(xqyin): Integrate this into console page.
  void GraphsHandler(const RewriteOptions& options, AdminSource source,
                     const QueryParams& query_params, AsyncFetch* fetch,
                     Statistics* stats);
  ;

  // Print details for configuration.
  void PrintConfig(AdminSource source, AsyncFetch* fetch,
                   SystemRewriteOptions* global_system_rewrite_options);

  // Print statistics about the caches.  In the future this will also
  // be a launching point for examining cache entries and purging them.
  void PrintCaches(bool is_global, AdminSource source,
                   const GoogleUrl& stripped_gurl,
                   const QueryParams& query_params,
                   const RewriteOptions* options, SystemCachePath* cache_path,
                   AsyncFetch* fetch, SystemCaches* system_caches,
                   CacheInterface* filesystem_metadata_cache,
                   HTTPCache* http_cache, CacheInterface* metadata_cache,
                   PropertyCache* page_property_cache,
                   ServerContext* server_context);

  // Print histograms showing the dynamics of server activity.
  void PrintHistograms(AdminSource source, AsyncFetch* fetch,
                       Statistics* stats);

  void PurgeHandler(StringPiece url, SystemCachePath* cache_path,
                    AsyncFetch* fetch);

  // Return the message handler for debugging use.
  MessageHandler* MessageHandlerForTesting() { return message_handler_; }

  // Frees the slot of `leaf` (see ExpensiveSlot::Detach).
  void ReleaseExpensiveSlot(ExpensiveLeaf leaf) {
    expensive_in_flight_[static_cast<size_t>(leaf)].store(
        false, std::memory_order_release);
  }

  // Marks the slot of an expensive read as taken (true) or free.
  void SetExpensiveSlotBusyForTesting(ExpensiveLeaf leaf, bool busy) {
    expensive_in_flight_[static_cast<size_t>(leaf)].store(
        busy, std::memory_order_release);
  }

 private:
  // Takes the slot of `leaf`, waiting up to `wait_ms` for it.  The wait
  // sleeps the calling thread, which can be the server's event loop: only a
  // read that finishes before its handler returns may wait.
  bool AcquireExpensiveSlot(ExpensiveLeaf leaf, int wait_ms);

  // Holds the slot of one expensive read for the length of a scope.  When the
  // slot could not be taken, held() is false and the caller answers 429.
  class ExpensiveSlot {
   public:
    ExpensiveSlot(AdminSite* site, ExpensiveLeaf leaf,
                  int wait_ms = kExpensiveSlotWaitMs)
        : site_(site),
          leaf_(leaf),
          held_(site->AcquireExpensiveSlot(leaf, wait_ms)) {}
    bool held() const { return held_; }
    // Frees the slot now; a later call and the destructor do nothing.
    void Release() {
      if (held_) site_->ReleaseExpensiveSlot(leaf_);
      held_ = false;
    }
    // Gives the slot up without freeing it: the caller frees it later with
    // ReleaseExpensiveSlot (an asynchronous read does so when it is done).
    void Detach() { held_ = false; }
    ~ExpensiveSlot() { Release(); }

   private:
    AdminSite* site_;
    ExpensiveLeaf leaf_;
    bool held_;

    DISALLOW_COPY_AND_ASSIGN(ExpensiveSlot);
  };
  friend class ExpensiveSlot;

  MessageHandler* message_handler_;
  StaticAssetManager* static_asset_manager_;
  Timer* timer_;
  int64 start_time_ms_;
  // One slot per expensive leaf (ExpensiveLeaf): the read in flight, if any.
  std::atomic<bool> expensive_in_flight_[kNumLeaves];
  DISALLOW_COPY_AND_ASSIGN(AdminSite);
};

}  // namespace net_instaweb

#endif  // PAGESPEED_SYSTEM_ADMIN_SITE_H_
