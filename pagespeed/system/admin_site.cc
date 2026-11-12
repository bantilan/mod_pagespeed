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

#include "pagespeed/system/admin_site.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <memory>
#include <ctime>
#include <cstring>
#include <set>
#include <vector>
#include <vector>

#include "net/instaweb/http/public/async_fetch.h"
#include "net/instaweb/http/public/http_cache.h"
#include "net/instaweb/rewriter/public/rewrite_options.h"
#include "net/instaweb/rewriter/public/rewrite_query.h"
#include "net/instaweb/rewriter/public/server_context.h"
#include "net/instaweb/util/public/property_cache.h"
#include "net/instaweb/util/public/property_store.h"
//#include "strings/stringpiece_utils.h"
#include "pagespeed/kernel/base/cache_interface.h"
#include "pagespeed/kernel/base/base64_util.h"
#include "pagespeed/kernel/base/callback.h"
#include "pagespeed/kernel/base/message_handler.h"
#include "pagespeed/kernel/base/null_message_handler.h"
#include "pagespeed/kernel/base/statistics.h"
#include "pagespeed/kernel/base/string.h"
#include "pagespeed/kernel/base/string_util.h"
#include "pagespeed/kernel/base/string_writer.h"
#include "pagespeed/kernel/base/timer.h"
#include "pagespeed/kernel/cache/purge_context.h"
#include "pagespeed/kernel/html/html_keywords.h"
#include "pagespeed/kernel/http/content_type.h"
#include "pagespeed/kernel/http/google_url.h"
#include "pagespeed/kernel/http/http_names.h"
#include "pagespeed/kernel/http/query_params.h"
#include "pagespeed/kernel/http/request_headers.h"
#include "pagespeed/kernel/http/response_headers.h"
#include "pagespeed/kernel/util/statistics_logger.h"
#include "pagespeed/system/history_archive.h"
#include "pagespeed/kernel/base/time_util.h"
#include "pagespeed/system/system_cache_path.h"
#include "pagespeed/system/system_caches.h"
#include "pagespeed/system/system_rewrite_options.h"

namespace net_instaweb {

// Generated from JS, CSS, and HTML source via net/instaweb/js/data_to_c.cc.
extern const char* CSS_admin_site_css;
extern const char* CSS_caches_css;
extern const char* JS_config_js;
extern const char* CSS_console_css;
extern const char* CSS_graphs_css;
extern const char* CSS_statistics_css;
extern const char* JS_caches_js;
extern const char* JS_caches_js_opt;
extern const char* JS_console_js;
extern const char* JS_console_js_opt;
extern const char* JS_graphs_js;
extern const char* JS_graphs_js_opt;
extern const char* JS_messages_js;
extern const char* JS_messages_js_opt;
extern const char* JS_statistics_js;
extern const char* JS_statistics_js_opt;

namespace {

struct Tab {
  const char* label;
  const char* title;
  const char* admin_link;       // relative from /pagespeed_admin/
  const char* statistics_link;  // relative from /mod_pagespeed_statistics
};

GoogleString EscapeHistory(StringPiece text) {
  GoogleString escaped;
  HtmlKeywords::Escape(text, &escaped);
  return escaped;
}

bool HistoryDate(const GoogleString& date, int64* ms) {
  if (date.size() != 10 || date[4] != '-' || date[7] != '-') return false;
  struct tm value = {};
  char* end = strptime(date.c_str(), "%Y-%m-%d", &value);
  if (!end || *end || value.tm_year < 70) return false;
  time_t time = timegm(&value);
  char normalized[20];
  strftime(normalized, sizeof(normalized), "%Y-%m-%d", &value);
  if (date != normalized || time < 0) return false;
  *ms = static_cast<int64>(time) * 1000;
  return true;
}

GoogleString HistoryControls(const SystemRewriteOptions& options,
    const QueryParams& query, StringPiece kind, MessageHandler* handler,
    GoogleString* log, bool* combined) {
  *combined = false;
  if (!options.history_archive()) return "";
  GoogleString from, until, cursor, error;
  query.Lookup1Unescaped("from", &from);
  query.Lookup1Unescaped("until", &until);
  query.Lookup1Unescaped("cursor", &cursor);
  int64 begin = 0, end = 253402300800000LL;
  if ((!from.empty() && !HistoryDate(from, &begin)) ||
      (!until.empty() && !HistoryDate(until, &end))) error = "Invalid UTC date";
  if (!until.empty()) end += 86400000;
  if (begin >= end) error = "Invalid date range";
  SharedCircularBuffer* buffer = handler->history_buffer();
  SharedCircularBuffer::HistorySnapshot live;
  NullMessageHandler null_handler;
  bool have_snapshot = buffer != nullptr &&
      buffer->SnapshotHistory(&live, &null_handler);
  HistoryArchive::Page page;
  page = HistoryArchive::Browse(options.file_cache_path(), kind,
      begin, end, cursor, have_snapshot ? &live : nullptr);
  if (error.empty() && !page.error.empty()) error = page.error;
  *log = page.log.empty() ? "\n" : page.log;
  if (cursor.empty() && have_snapshot) {
    size_t newline = live.data.find('\n');
    if (newline != GoogleString::npos)
      *log += HistoryArchive::FilterLog(
          StringPiece(live.data).substr(newline + 1), begin, end);
  }
  *combined = true;
  if (cursor.empty() && !have_snapshot) {
    GoogleString memory;
    StringWriter writer(&memory);
    if (handler->Dump(&writer)) {
      size_t newline = memory.find('\n');
      if (newline != GoogleString::npos)
        *log += HistoryArchive::FilterLog(
            StringPiece(memory).substr(newline + 1), begin, end);
    }
  }
  GoogleString controls =
      "<section class='pagespeed-history-controls'><form method='get'>"
      "<label>From (UTC) <input type='date' name='from' value='";
  controls += EscapeHistory(from) + "'></label><label>Through (UTC) "
      "<input type='date' name='until' value='" + EscapeHistory(until) +
      "'></label><button type='submit'>Browse history</button></form><p>";
  controls += "One chronological history combines current memory with archived "
      "records. Archive pages are bounded to 100 matching records; older links "
      "continue into retained history. Existing view filters still apply.</p>";
  if (!error.empty()) controls += "<p role='alert'>" + EscapeHistory(error) + "</p>";
  if (!page.next.empty()) controls += "<a href='?from=" +
      EscapeHistory(from) + "&amp;until=" + EscapeHistory(until) + "&amp;cursor=" +
      EscapeHistory(page.next) + "'>Older records &rarr;</a>";
  controls += "</section>";
  return controls;
}

void WriteHistoryHealth(MessageHandler* handler, AsyncFetch* fetch,
                        const SystemRewriteOptions* options) {
  SharedCircularBuffer* buffer = handler->history_buffer();
  SharedCircularBuffer::HistoryHealth health;
  bool have_health = buffer != nullptr && buffer->GetHistoryHealth(&health);
  if (!have_health && options != nullptr && options->history_archive())
    fetch->Write("<section class='pagespeed-history-controls' role='alert'>"
        "Archive source health is unavailable. Verify MessageBufferSize and "
        "that the module and PSOL builds match.</section>", handler);
  fetch->Write("<section class='pagespeed-dashboard-grid pagespeed-history-cards'>",
      handler);
  const char* card_labels[] = {"Archive status", "Snapshot interval",
      "Retention", "Storage limit"};
  GoogleString interval = options->history_archive_interval() == 1 ? "1 second" :
      StrCat(IntegerToString(options->history_archive_interval()), " seconds");
  GoogleString retention = StrCat(IntegerToString(options->history_archive_retention_days()), " days");
  GoogleString limit = StrCat(IntegerToString(options->history_archive_max_size_mb()), " MiB");
  const char* values[] = {options->history_archive() ? "Enabled" : "Disabled",
      interval.c_str(), retention.c_str(), limit.c_str()};
  for (int i = 0; i < 4; ++i)
    fetch->Write(StrCat("<article class='pagespeed-dashboard-card'><div "
        "class='pagespeed-dashboard-card-label'>", card_labels[i],
        "</div><div class='pagespeed-dashboard-value'>", values[i],
        "</div></article>"), handler);
  fetch->Write("</section><section class='pagespeed-dashboard-panel'>"
      "<div class='pagespeed-dashboard-panel-heading'><div>"
      "<h2>Archive health</h2><p>Shared-buffer snapshots run in the background</p>"
      "</div></div><div class='pagespeed-history-status'>", handler);
  GoogleString time = "No successful snapshot yet";
  if (health.last_success_ms) ConvertTimeToString(health.last_success_ms, &time);
  GoogleString next = health.next_snapshot_ms > 0 ? "Scheduled" :
      (options->history_archive() ? "Awaiting scheduler" : "Not scheduled");
  if (health.next_snapshot_ms > 0) ConvertTimeToString(health.next_snapshot_ms, &next);
  GoogleString text = StrCat("<p>Last successful snapshot: ", time,
      "</p><p>Next scheduled snapshot: ", next,
      "</p><p>Archive disk usage at last successful snapshot: ", Integer64ToString(health.disk_bytes),
      " bytes. Archived records (cumulative): ", Integer64ToString(health.records),
      ". Currently retained records: ", Integer64ToString(health.retained_records),
      ". Detected missing bytes: ", Integer64ToString(health.lost_bytes),
      ". Uncertain restart/reset gaps: ", Integer64ToString(health.restart_gaps),
      ". Archive failures: ", Integer64ToString(health.failures), ".</p>");
  if (health.error) text += "<p role='alert'>Last error: " +
      EscapeHistory(strerror(health.error)) + "</p>";
  if (health.error == EINVAL) text += "<p>Check archive configuration and metadata: "
      "all server contexts must share one FileCachePath, and the message buffer "
      "must not exceed 16 MiB. Invalid or damaged archive metadata also stops "
      "archiving rather than risking duplicate records.</p>";
  text += "<p>Gap counts are source-wide; the types and number of overwritten "
      "events cannot be reconstructed. Restarts may lose an unknown unsaved tail. "
      "These are cumulative classifications, not counts of currently retained records. "
      "A single message may match more than one history view.</p>";
  const char* labels[] = {"Messages", "Images", "HTML cache", "JavaScript diagnostics", "CSS diagnostics"};
  text += "<table class='pagespeed-admin-data-table'><thead><tr><th>View</th>"
      "<th>Cumulative archived matches</th></tr></thead><tbody>";
  for (int i = 0; i < 5; ++i) text += StrCat("<tr><td>", labels[i], "</td><td>",
      Integer64ToString(health.categories[i]), "</td></tr>");
  text += "</tbody></table></div></section>";
  fetch->Write(text, handler);
}

// TODO(jmarantz): disable or recolor links to pages that are not available
// based on the current config.
const Tab kTabs[] = {
    {"Dashboard", "Dashboard", "dashboard", nullptr},
    {"Historical Archive", "Historical Archive", "history", nullptr},
    {"Images", "Optimized Images", "images", nullptr},
    {"HTML Cache", "HTML Cache", "html_cache", nullptr},
    {"JavaScript", "JavaScript Diagnostics", "javascript", nullptr},
    {"CSS", "CSS Diagnostics", "css", nullptr},
    {"Statistics", "Statistics", "statistics", "?"},
    {"Configuration", "Configuration", "config", "?config"},
    {"Histograms", "Histograms", "histograms", "?histograms"},
    {"Caches", "Caches", "cache", "?cache"},
    {"Console", "Console", "console", nullptr},
    {"Message History", "Message History", "message_history", nullptr},
    {"Graphs", "Graphs", "graphs", nullptr},
};

struct ImageRewriteRow {
  int64 timestamp_ms;
  int64 original_bytes;
  int64 optimized_bytes;
  GoogleString output_mime_type;
  GoogleString original_url;
  GoogleString optimized_url;
};

void ReadRecentImageRewrites(MessageHandler* message_handler, size_t max_rows,
                             std::vector<ImageRewriteRow>* rows,
                             const GoogleString* stored = nullptr) {
  GoogleString log;
  StringWriter log_writer(&log);
  if (stored != nullptr) log = *stored;
  else if (!message_handler->Dump(&log_writer)) {
    return;
  }
  StringPieceVector messages;
  message_handler->ParseMessageDumpIntoMessages(log, &messages);
  for (int i = static_cast<int>(messages.size()) - 1;
       i >= 0 && rows->size() < max_rows; --i) {
    StringPiece message = messages[i];
    size_t marker = message.find("PS_IMAGE_DETAIL\t");
    if (marker == StringPiece::npos) {
      continue;
    }
    StringPieceVector fields;
    SplitStringPieceToVector(message.substr(marker), "\t", &fields, true);
    if (fields.size() != 7) {
      continue;
    }
    ImageRewriteRow row;
    if (!StringToInt64(fields[1], &row.timestamp_ms) ||
        !StringToInt64(fields[2], &row.original_bytes) ||
        !StringToInt64(fields[3], &row.optimized_bytes) ||
        row.original_bytes < row.optimized_bytes) {
      continue;
    }
    row.output_mime_type = fields[4].as_string();
    row.original_url = fields[5].as_string();
    row.optimized_url = fields[6].as_string();
    rows->push_back(row);
  }
}

int64 DashboardCounter(Statistics* stats, const char* name,
                       const std::map<GoogleString, int64>& previous) {
  Variable* variable = stats->FindVariable(name);
  UpDownCounter* counter = variable == nullptr
      ? stats->FindUpDownCounter(name) : nullptr;
  int64 live = std::max<int64>(0, variable != nullptr ? variable->Get() :
      (counter != nullptr ? counter->Get() : 0));
  auto found = previous.find(name);
  return live + (found == previous.end() ? 0 : found->second);
}

std::map<GoogleString, int64> PreviousDashboardCounters(
    Statistics* stats, const SystemRewriteOptions* options) {
  std::map<GoogleString, int64> previous;
  if (options != nullptr && options->history_archive() &&
      stats->console_logger() != nullptr) {
    stats->console_logger()->PreviousDashboardCounters(&previous);
  }
  return previous;
}

struct HtmlCacheRow {
  int64 created_ms, expires_ms, bytes;
  GoogleString device, formats, variant, url;
};

void ReadRecentHtmlRewrites(MessageHandler* handler,
                            std::vector<HtmlCacheRow>* rows,
                            const GoogleString* stored = nullptr) {
  GoogleString log;
  StringWriter writer(&log);
  if (stored != nullptr) log = *stored;
  else if (!handler->Dump(&writer)) return;
  StringPieceVector messages;
  handler->ParseMessageDumpIntoMessages(log, &messages);
  std::set<GoogleString> seen;
  for (int i = static_cast<int>(messages.size()) - 1;
       i >= 0 && rows->size() < 100; --i) {
    size_t marker = messages[i].find("PS_HTML_DETAIL\t");
    if (marker == StringPiece::npos) continue;
    StringPieceVector fields;
    SplitStringPieceToVector(messages[i].substr(marker), "\t", &fields, true);
    HtmlCacheRow row;
    if (fields.size() != 8 ||
        !StringToInt64(fields[1], &row.created_ms) || row.created_ms <= 0 ||
        !StringToInt64(fields[2], &row.expires_ms) ||
        row.expires_ms <= row.created_ms ||
        !StringToInt64(fields[3], &row.bytes) || row.bytes <= 0 ||
        row.bytes > 1024 * 1024 || fields[7].size() > 6000 ||
        !Web64Decode(fields[7], &row.url) || row.url.size() > 4096 ||
        !GoogleUrl(row.url).IsWebValid() ||
        !fields[6].starts_with("optimized-html-v1:") || fields[6].size() > 128 ||
        (fields[4] != "desktop" && fields[4] != "mobile" &&
         fields[4] != "tablet") ||
        (fields[5] != "AVIF capable" && fields[5] != "WebP capable" &&
         fields[5] != "Original formats")) continue;
    row.device = fields[4].as_string();
    row.formats = fields[5].as_string();
    row.variant = fields[6].as_string();
    if (seen.insert(row.variant).second) rows->push_back(row);
  }
}

GoogleString FormatDashboardBytes(int64 bytes) {
  static const int64 kKilobyte = 1024;
  static const int64 kMegabyte = kKilobyte * 1024;
  static const int64 kGigabyte = kMegabyte * 1024;
  int64 unit = 1;
  const char* suffix = "B";
  if (bytes >= kGigabyte) {
    unit = kGigabyte;
    suffix = "GB";
  } else if (bytes >= kMegabyte) {
    unit = kMegabyte;
    suffix = "MB";
  } else if (bytes >= kKilobyte) {
    unit = kKilobyte;
    suffix = "KB";
  }
  if (unit == 1) {
    return StrCat(Integer64ToString(bytes), " ", suffix);
  }
  return StrCat(Integer64ToString(bytes / unit), ".",
                Integer64ToString((bytes % unit) * 10 / unit), " ", suffix);
}

GoogleString FormatDashboardUptime(int64 elapsed_ms) {
  int64 elapsed_seconds = std::max<int64>(0, elapsed_ms / 1000);
  int64 days = elapsed_seconds / 86400;
  int64 hours = (elapsed_seconds / 3600) % 24;
  int64 minutes = (elapsed_seconds / 60) % 60;
  int64 seconds = elapsed_seconds % 60;
  if (days > 0) {
    return StrCat(Integer64ToString(days), "d ", Integer64ToString(hours),
                  "h ", Integer64ToString(minutes), "m");
  }
  return StrCat(Integer64ToString(hours), "h ", Integer64ToString(minutes),
                "m ", Integer64ToString(seconds), "s");
}

void WriteAdminPageHeading(AsyncFetch* fetch, MessageHandler* handler,
                           StringPiece eyebrow, StringPiece title,
                           StringPiece description) {
  fetch->Write(StrCat(
                   "<div class='pagespeed-dashboard-heading'><div>"
                   "<p class='pagespeed-dashboard-eyebrow'>",
                   eyebrow, "</p><h1>", title,
                   "</h1><p class='pagespeed-dashboard-description'>",
                   description, "</p></div></div>"),
               handler);
}

int DashboardPercent(int64 value, int64 total) {
  return total <= 0 ? 0 : static_cast<int>(value * 100 / total);
}

void WriteHtmlCacheSummary(Statistics* stats, AsyncFetch* fetch,
                           MessageHandler* handler,
                           const std::map<GoogleString, int64>& previous = {}) {
  const char* names[] = {"html_cache_hits", "html_cache_misses",
                         "html_cache_bypasses", "html_cache_inserts"};
  const char* labels[] = {"HTML CACHE HITS", "LOOKUP MISSES",
                          "BYPASSED LOOKUPS", "HTML PUBLICATIONS"};
  const char* notes[] = {"Parsing and filters skipped", "Entry absent or stale",
                         "Request or rewrite not eligible", "Best-effort cache writes"};
  fetch->Write("<div class='pagespeed-dashboard-grid'>", handler);
  for (int i = 0; i < 4; ++i) {
    fetch->Write(StrCat("<article class='pagespeed-dashboard-card'><div "
        "class='pagespeed-dashboard-card-label'>", labels[i],
        "</div><div class='pagespeed-dashboard-value'>",
        Integer64ToString(DashboardCounter(stats, names[i], previous)),
        "</div><div class='pagespeed-dashboard-card-note'>", notes[i],
        "</div></article>"), handler);
  }
  int64 hits = DashboardCounter(stats, "html_cache_hits", previous);
  int64 misses = DashboardCounter(stats, "html_cache_misses", previous);
  fetch->Write(StrCat("</div><p class='pagespeed-dashboard-footnote'>Hit rate: ",
      IntegerToString(DashboardPercent(hits, hits + misses)),
      "% of lookups. HTML served from cache: ",
      FormatDashboardBytes(DashboardCounter(stats, "html_cache_bytes_served", previous)),
      ". Counters include available saved samples plus current activity; served bytes are not "
      "compression savings.</p>"), handler);
}

// Controls the generation of an HTML Admin page.  Constructing it
// establishes the content-type as HTML and response code 200, and
// puts in a banner with links to all the admin pages, ready for
// appending more <body> elements.  Destructing AdminHtml closes the
// body and completes the fetch.
class AdminHtml {
 public:
  AdminHtml(StringPiece current_link, StringPiece head_extra,
            AdminSite::AdminSource source, Timer* timer, AsyncFetch* fetch,
            MessageHandler* handler)
      : fetch_(fetch), handler_(handler) {
    fetch->response_headers()->SetStatusAndReason(HttpStatus::kOK);
    fetch->response_headers()->Add(HttpAttributes::kContentType, "text/html");

    int64 now_ms = timer->NowMs();
    fetch->response_headers()->SetLastModified(now_ms);

    // Let PageSpeed dynamically minify the html, css, and javasript
    // generated by the admin page, to the extent it's not done
    // already by the tools.  Note, this does mean that viewing the
    // statistics and histograms pages will affect the statistics and
    // histograms.  If we decide this is too annoying, then we can
    // instead procedurally minify the css/js and leave the html
    // alone.
    //
    // Note that we at least turn off add_instrumenation here by explicitly
    // giving a filter list without "+" or "-".
    fetch->response_headers()->Add(
        RewriteQuery::kPageSpeedFilters,
        "rewrite_css,rewrite_javascript,collapse_whitespace");

    // Generate some navigational links to help our users get to other
    // admin pages.
    fetch->Write("<!DOCTYPE html>\n<html><head>"
                 "<meta name='viewport' content='width=device-width, initial-scale=1'>",
                 handler_);
    fetch->Write(StrCat("<style>", CSS_admin_site_css, "</style>"), handler_);

    GoogleString buf;
    for (int i = 0, n = arraysize(kTabs); i < n; ++i) {
      const Tab& tab = kTabs[i];
      const char* link = nullptr;
      switch (source) {
        case AdminSite::kPageSpeedAdmin:
          link = tab.admin_link;
          break;
        case AdminSite::kStatistics:
          link = tab.statistics_link;
          break;
        case AdminSite::kOther:
          link = nullptr;
          break;
      }
      if (link != nullptr) {
        StringPiece style;
        if (tab.admin_link == current_link) {
          style = " class='pagespeed-admin-tab-active' aria-current='page'";
          fetch->Write(StrCat("<title>PageSpeed ", tab.title, "</title>"),
                       handler_);
        }
        StrAppend(&buf, "<a href='", link, "'", style, ">", tab.label,
                  "</a>\n");
      }
    }

    fetch->Write(StrCat(head_extra, "</head>"), handler_);
    fetch->Write("<body class='pagespeed-admin-body'>"
                 "<header class='pagespeed-admin-header'>"
                 "<div class='pagespeed-admin-brand'>"
                 "<span class='pagespeed-admin-brand-mark' "
                 "aria-hidden='true'></span>mod_pagespeed</div>"
                 "<span class='pagespeed-admin-header-label'>ADMIN "
                 "CONSOLE</span></header>"
                 "<div class='pagespeed-admin-layout'>"
                 "<aside class='pagespeed-admin-sidebar'>"
                 "<div class='pagespeed-admin-sidebar-label'>WORKSPACE</div>"
                 "<nav class='pagespeed-admin-tabs' "
                 "aria-label='PageSpeed administration'>",
                 handler_);
    fetch->Write(buf, handler_);
    fetch->Write("</nav></aside><main class='pagespeed-admin-content'>\n",
                 handler_);
    fetch->Flush(handler_);
  }

  ~AdminHtml() {
    fetch_->Write("</main></div></body></html>", handler_);
    fetch_->Done(true);
  }

 private:
  AsyncFetch* fetch_;
  MessageHandler* handler_;
};

}  // namespace

AdminSite::AdminSite(StaticAssetManager* static_asset_manager, Timer* timer,
                     MessageHandler* message_handler)
    : message_handler_(message_handler),
      static_asset_manager_(static_asset_manager),
      timer_(timer),
      start_time_ms_(timer->NowMs()) {}

// Handler which serves PSOL console.
void AdminSite::ConsoleHandler(const SystemRewriteOptions& global_options,
                               const RewriteOptions& options,
                               AdminSource source,
                               const QueryParams& query_params,
                               AsyncFetch* fetch, Statistics* statistics) {
  if (query_params.Has("json")) {
    ConsoleJsonHandler(query_params, fetch, statistics);
    return;
  }

  MessageHandler* handler = message_handler_;
  bool statistics_enabled = global_options.statistics_enabled();
  bool logging_enabled = global_options.statistics_logging_enabled();
  bool log_dir_set = !global_options.log_dir().empty();

  // TODO(jmarantz): change StaticAssetManager to take options by const ref.
  // TODO(sligocki): Move static content to a data2cc library.
  StringPiece console_js = options.Enabled(RewriteOptions::kDebug)
                               ? JS_console_js
                               : JS_console_js_opt;
  // TODO(sligocki): Do we want to have a minified version of console CSS?
  GoogleString head_markup = StrCat("<style>", CSS_console_css, "</style>\n");
  AdminHtml admin_html("console", head_markup, source, timer_, fetch,
                       message_handler_);
  WriteAdminPageHeading(fetch, handler, "OPERATIONS", "Console",
                        "Live PageSpeed diagnostics and operational status.");
  if (statistics_enabled && logging_enabled && log_dir_set) {
    fetch->Write(
        "<div class='pagespeed-console' id='suggestions'>\n"
        "  <div class='pagespeed-console-status' id='pagespeed-console-status'"
        " role='status'>Loading recent diagnostics...</div>\n"
        "  <div class='pagespeed-console-summary'"
        " id='pagespeed-console-summary'></div>\n"
        "  <div class='pagespeed-console-toolbar'>\n"
        "    <div class='pagespeed-console-toolbar-copy'>\n"
        "      <h2>Operational signals</h2>\n"
        "      <p>Failure and cache trends from the last 24 hours.</p>\n"
        "    </div>\n"
        "    <input class='pagespeed-console-search'"
        " id='pagespeed-console-search' type='search'"
        " placeholder='Filter signals...' aria-label='Filter signals'>\n"
        "  </div>\n"
        "  <div class='pagespeed-console-grid'"
        " id='pagespeed-graphs-container'></div>\n"
        "</div>\n"
        "<script>var pagespeedStatisticsUrl = '';</script>\n"
        "<script>",
        handler);
    // From the admin page, the console JSON is relative, so it can
    // be set to ''.  Formerly it was set to options.statistics_handler_path(),
    // but there does not appear to be a disadvantage to always handling it
    // from whatever URL served this console HTML.
    //
    // TODO(jmarantz): Change the JS to remove pagespeedStatisticsUrl.
    fetch->Write(console_js, handler);
    fetch->Write("pagespeed.startConsole();", handler);
    fetch->Write("</script>\n", handler);
  } else {
    fetch->Write(
        "<section class='pagespeed-console-unavailable'>\n"
        "  <h2>Console needs a little setup</h2>\n"
        "  <p>PageSpeed cannot load the operational console because:</p>\n"
        "  <ul>\n",
        handler);
    if (!statistics_enabled) {
      fetch->Write("  <li>Statistics is not enabled.</li>\n", handler);
    }
    if (!logging_enabled) {
      fetch->Write(
          "  <li>StatisticsLogging is not enabled."
          "</li>\n",
          handler);
    }
    if (!log_dir_set) {
      fetch->Write("  <li>LogDir is not set.</li>\n", handler);
    }
    fetch->Write(
        "</ul>\n"
        "<p>\n"
        "  Configure these options to enable the console. See the\n"
        "  <a href='https://developers.google.com/"
        "speed/pagespeed/module/console'>console documentation</a>\n"
        "  for more details.\n"
        "</p>\n"
        "</section>\n",
        handler);
  }
}

void AdminSite::StatisticsJsonHandler(AsyncFetch* fetch, Statistics* stats) {
  fetch->response_headers()->SetStatusAndReason(HttpStatus::kOK);
  fetch->response_headers()->Add(HttpAttributes::kContentType,
                                 kContentTypeJson.mime_type());
  stats->DumpJson(fetch, message_handler_);
  fetch->Done(true);
}

void AdminSite::StatisticsHandler(const RewriteOptions& options,
                                  AdminSource source, AsyncFetch* fetch,
                                  Statistics* stats) {
  GoogleString head_markup =
      StrCat("<style>", CSS_statistics_css, "</style>\n");
  AdminHtml admin_html("statistics", head_markup, source, timer_, fetch,
                       message_handler_);
  WriteAdminPageHeading(fetch, message_handler_, "LIVE METRICS", "Statistics",
                        "Explore PageSpeed counters grouped by the subsystem "
                        "they describe.");
  // We have to call Dump() here to write data to sources for
  // system/system_test.sh: Line 79. We use JS to update the data in refreshes.
  fetch->Write("<pre id='stat'>", message_handler_);
  stats->Dump(fetch, message_handler_);
  fetch->Write("</pre>\n", message_handler_);
  StringPiece statistics_js = options.Enabled(RewriteOptions::kDebug)
                                  ? JS_statistics_js
                                  : JS_statistics_js_opt;
  fetch->Write(StrCat("<script type='text/javascript'>", statistics_js,
                      "\npagespeed.Statistics.Start();</script>\n"),
               message_handler_);
}

void AdminSite::DashboardHandler(AdminSource source, AsyncFetch* fetch,
                                 Statistics* stats,
                                 const SystemRewriteOptions* archive_options,
                                 const GoogleString* recent_images) {
  AdminHtml admin_html("dashboard", "", source, timer_, fetch,
                       message_handler_);

  const auto previous = PreviousDashboardCounters(stats, archive_options);

  int64 javascript_saved =
      DashboardCounter(stats, "javascript_total_bytes_saved", previous);
  int64 css_saved = DashboardCounter(stats, "css_filter_total_bytes_saved", previous);
  int64 image_saved = DashboardCounter(stats, "image_rewrite_total_bytes_saved", previous);
  javascript_saved = std::max<int64>(0, javascript_saved);
  css_saved = std::max<int64>(0, css_saved);
  image_saved = std::max<int64>(0, image_saved);
  int64 total_saved = std::max<int64>(0, javascript_saved) +
                      std::max<int64>(0, css_saved) +
                      std::max<int64>(0, image_saved);
  int64 javascript_rewrites =
      DashboardCounter(stats, "javascript_blocks_minified", previous);
  int64 css_rewrites = DashboardCounter(stats, "css_filter_blocks_rewritten", previous);
  int64 image_rewrites = DashboardCounter(stats, "image_rewrites", previous);
  int64 total_rewrites = javascript_rewrites + css_rewrites + image_rewrites;
  int64 html_publications = DashboardCounter(stats, "html_cache_inserts", previous);
  int64 html_served = DashboardCounter(stats, "html_cache_bytes_served", previous);
  int64 activity_total = total_rewrites + html_publications;
  GoogleString uptime = FormatDashboardUptime(timer_->NowMs() - start_time_ms_);
  int javascript_percent = DashboardPercent(javascript_saved, total_saved);
  int css_percent = DashboardPercent(css_saved, total_saved);
  int image_percent = DashboardPercent(image_saved, total_saved);

  fetch->Write(
      "<section class='pagespeed-dashboard'>"
      "<div class='pagespeed-dashboard-heading'>"
      "<div><p class='pagespeed-dashboard-eyebrow'>LIVE OVERVIEW</p>"
      "<h1>Performance dashboard</h1>"
      "<p class='pagespeed-dashboard-description'>Live optimization counters "
      "and recent image history for this PageSpeed instance.</p></div>"
      "<div class='pagespeed-dashboard-controls'>"
      "<span class='pagespeed-dashboard-live'><i></i> INSTANCE ACTIVE</span>"
      "<a class='pagespeed-history-compact-link' href='history'>Archive status</a>"
      "<button type='button' class='pagespeed-purge-button' "
      "id='pagespeed-purge-cache'>Purge cache</button></div></div>"
      "<p class='pagespeed-purge-status' id='pagespeed-purge-status' "
      "role='status' aria-live='polite'></p>"
      "<div class='pagespeed-dashboard-grid'>"
      "<article class='pagespeed-dashboard-card pagespeed-dashboard-total'>"
      "<div class='pagespeed-dashboard-card-label'>TOTAL DATA SAVED</div>",
      message_handler_);
  if (!previous.empty()) {
    fetch->Write("<div class='pagespeed-dashboard-card-note'>Recovered saved "
        "statistics + current run. Sampled history excludes unsaved activity; "
        "legacy reset boundaries may be incomplete.</div>", message_handler_);
  }
  fetch->Write(StrCat("<div class='pagespeed-dashboard-total-value'>",
                      FormatDashboardBytes(total_saved), "</div>"),
               message_handler_);
  fetch->Write(
      "<div class='pagespeed-dashboard-card-note'>Combined savings from "
      "JavaScript, CSS, and image rewrites</div></article>"
      "<article class='pagespeed-dashboard-card pagespeed-dashboard-uptime'>"
      "<div class='pagespeed-dashboard-card-label'>PAGESPEED UPTIME</div>",
      message_handler_);
  fetch->Write(StrCat("<div class='pagespeed-dashboard-total-value'>", uptime,
                      "</div>"),
               message_handler_);
  fetch->Write(
      "<div class='pagespeed-dashboard-card-note'>Since this admin instance "
      "initialized</div></article>"
      "<article class='pagespeed-dashboard-card'>"
      "<div class='pagespeed-dashboard-card-label'>SUCCESSFUL REWRITES</div>",
      message_handler_);
  fetch->Write(
      StrCat("<div class='pagespeed-dashboard-value'>",
             Integer64ToString(total_rewrites),
             "</div><div class='pagespeed-dashboard-card-note'>Across image, "
             "CSS, and JavaScript resources</div></article>"
             "<article class='pagespeed-dashboard-card'>"
             "<div class='pagespeed-dashboard-card-label'>IMAGE REWRITES</div>"
             "<a class='pagespeed-dashboard-image-link' href='images'>"
             "<div class='pagespeed-dashboard-value'>",
             Integer64ToString(image_rewrites),
             "</div><span>Inspect recent optimized images</span></a>"
             "</article></div>"),
      message_handler_);

  fetch->Write(
      "<div class='pagespeed-dashboard-panels'>"
      "<section class='pagespeed-dashboard-panel'>"
      "<div class='pagespeed-dashboard-panel-heading'><div><h2>Savings by "
      "resource</h2><p>Available saved statistics plus current activity</p>"
      "</div><span class='pagespeed-dashboard-period'>RECORDED</span></div>"
      "<div class='pagespeed-dashboard-chart' role='img' aria-label='Savings "
      "by resource: JavaScript, CSS, and images'>"
      "<div class='pagespeed-dashboard-stack'>"
      "<span class='pagespeed-dashboard-stack-js' style='width:",
      message_handler_);
  fetch->Write(StrCat(IntegerToString(javascript_percent), "%'></span>"
                      "<span class='pagespeed-dashboard-stack-css' style='width:",
                      IntegerToString(css_percent), "%'></span>"
                      "<span class='pagespeed-dashboard-stack-image' "
                      "style='width:",
                      IntegerToString(image_percent), "%'></span></div>"),
               message_handler_);

  const char* labels[] = {"JavaScript", "CSS", "Images"};
  const char* classes[] = {"js", "css", "image"};
  int64 values[] = {javascript_saved, css_saved, image_saved};
  int percentages[] = {javascript_percent, css_percent, image_percent};
  for (int i = 0; i < arraysize(labels); ++i) {
    fetch->Write(StrCat("<div class='pagespeed-dashboard-breakdown-row'>"
                        "<span class='pagespeed-dashboard-legend-dot "
                        "pagespeed-dashboard-legend-",
                        classes[i], "'></span><span>", labels[i],
                        "</span><strong>",
                        FormatDashboardBytes(values[i]), "</strong><small>",
                        IntegerToString(percentages[i]), "%</small></div>"),
                 message_handler_);
  }
  fetch->Write(
      "<div class='pagespeed-dashboard-breakdown-row'>"
      "<span class='pagespeed-dashboard-legend-dot pagespeed-dashboard-legend-html'>"
      "</span><span>HTML</span><strong>Not measured</strong>"
      "<small aria-label='Savings percentage unavailable'>&mdash;</small></div>",
      message_handler_);
  fetch->Write(StrCat("<p class='pagespeed-dashboard-footnote'>Cached HTML served: ",
      FormatDashboardBytes(html_served),
      ". HTML input/output savings are not recorded; cache-served bytes are "
      "excluded from savings totals and percentages.</p>"), message_handler_);
  fetch->Write(
      "<p class='pagespeed-dashboard-footnote'>Savings include available saved "
      "statistics and current counters. They represent optimized "
      "resource bytes, not a measurement of all network traffic.</p>"
      "</section><section class='pagespeed-dashboard-panel'>"
      "<div class='pagespeed-dashboard-panel-heading'><div>"
      "<h2>Rewrite activity</h2><p>Resource rewrites and HTML cache publications</p>"
      "</div></div><div class='pagespeed-dashboard-activity'>",
      message_handler_);
  const char* activity_labels[] = {"Images", "JavaScript", "CSS", "HTML"};
  const char* activity_classes[] = {"image", "js", "css", "html"};
  const char* activity_icons[] = {"IM", "JS", "#", "HT"};
  int64 activity_values[] = {image_rewrites, javascript_rewrites, css_rewrites,
                             html_publications};
  for (int i = 0; i < arraysize(activity_labels); ++i) {
    int percent = DashboardPercent(activity_values[i], activity_total);
    fetch->Write(StrCat("<div class='pagespeed-dashboard-activity-row'>"
                        "<span class='pagespeed-dashboard-activity-icon "
                        "pagespeed-dashboard-activity-",
                        activity_classes[i], "'>",
                        activity_icons[i],
                        "</span><div class='pagespeed-dashboard-activity-main'>"
                        "<div><strong>", activity_labels[i], "</strong>"
                        "<span>", Integer64ToString(activity_values[i]),
                        i == 3 ? " publications</span></div>" : " rewrites</span></div>",
                        "<span class='pagespeed-dashboard-"
                        "activity-track'><i style='width:",
                        IntegerToString(percent), "%'></i></span></div></div>"),
                 message_handler_);
  }
  fetch->Write(
      "</div></section></div><section class='pagespeed-dashboard-panel "
      "pagespeed-dashboard-recent'><div class='pagespeed-dashboard-panel-"
      "heading'><div><h2>Recent image optimization</h2><p>",
      message_handler_);
  fetch->Write(recent_images != nullptr ?
      "Latest image records from live memory and retained archive, when available." :
      "Latest image assets rewritten by PageSpeed.", message_handler_);
  fetch->Write("</p></div><a href='images'>Browse image history &rarr;</a></div>",
               message_handler_);
  std::vector<ImageRewriteRow> recent_rows;
  ReadRecentImageRewrites(message_handler_, 4, &recent_rows, recent_images);
  if (recent_rows.empty()) {
    fetch->Write(
        recent_images != nullptr ?
        "<div class='pagespeed-dashboard-recent-empty'>No image rewrite "
        "records were found in the current buffer or retained archive.</div>" :
        "<div class='pagespeed-dashboard-recent-empty'>Successful image "
        "rewrites will appear here while their entries remain in the bounded "
        "message buffer.</div>",
        message_handler_);
  } else {
    fetch->Write(
        "<div class='pagespeed-dashboard-recent-wrap'><table "
        "class='pagespeed-admin-data-table pagespeed-dashboard-recent-table'>"
        "<thead><tr><th>IMAGE</th>"
        "<th>FORMAT</th><th>BEFORE</th><th>AFTER</th><th>SAVED</th><th>TIME</th>"
        "</tr></thead><tbody>",
        message_handler_);
    for (const ImageRewriteRow& row : recent_rows) {
      GoogleString escaped_url, escaped_mime;
      HtmlKeywords::Escape(row.original_url, &escaped_url);
      HtmlKeywords::Escape(row.output_mime_type, &escaped_mime);
      int64 saved = row.original_bytes - row.optimized_bytes;
      fetch->Write(
          StrCat("<tr><td class='pagespeed-dashboard-recent-url' title='",
                 escaped_url, "'>", escaped_url, "</td><td><code>",
                 escaped_mime, "</code></td><td>",
                 FormatDashboardBytes(row.original_bytes), "</td><td>",
                 FormatDashboardBytes(row.optimized_bytes),
                 "</td><td class='pagespeed-dashboard-recent-saved'>",
                 FormatDashboardBytes(saved),
                 "</td><td><time class='pagespeed-image-time' "
                 "data-timestamp-ms='", Integer64ToString(row.timestamp_ms),
                 "'>Loading date...</time></td></tr>"),
          message_handler_);
    }
    fetch->Write("</tbody></table></div>", message_handler_);
  }
  fetch->Write(
      "</section></section><script>(function(){var e=document.querySelectorAll("
      "'.pagespeed-image-time');for(var i=0;i<e.length;i++){var t=Number("
      "e[i].getAttribute('data-timestamp-ms'));if(t>0){e[i].textContent="
      "new Date(t).toLocaleString();}}var b=document.getElementById("
      "'pagespeed-purge-cache'),s=document.getElementById("
      "'pagespeed-purge-status');b.addEventListener('click',function(){"
      "if(!window.confirm('Purge the entire PageSpeed cache? Cached resources "
      "will be regenerated as pages are requested.'))return;b.disabled=true;"
      "s.textContent='Purging cache...';fetch('cache?purge=*',{credentials:"
      "'same-origin',cache:'no-store'}).then(function(r){return r.text().then("
      "function(t){if(!r.ok)throw new Error(t||'HTTP '+r.status);return t;});})"
      ".then(function(t){s.textContent=t;if(t.replace(/^\\s+|\\s+$/g,'')!="
      "'Purge successful'){s.setAttribute('data-result','error');}else{"
      "s.setAttribute('data-result','success');}}).catch(function(){s.textContent="
      "'Cache purge request failed.';s.setAttribute('data-result','error');})"
      ".then(function(){b.disabled=false;});"
      "});})();</script>",
      message_handler_);
}

void AdminSite::HistoryArchiveHandler(AdminSource source, AsyncFetch* fetch,
                                      const SystemRewriteOptions& options) {
  AdminHtml admin_html("history", "", source, timer_, fetch, message_handler_);
  fetch->response_headers()->SetDateAndCaching(timer_->NowMs(), 0,
                                                ", private, no-store");
  WriteAdminPageHeading(fetch, message_handler_, "STORAGE & RETENTION",
      "Historical Archive",
      "Monitor snapshots, archive capacity, and any gaps in retained history.");
  WriteHistoryHealth(message_handler_, fetch, &options);
}

void AdminSite::HtmlCacheHandler(AdminSource source, AsyncFetch* fetch,
                                 Statistics* stats,
                                 const RewriteOptions& options,
                                 const GoogleString* stored, StringPiece controls,
                                 const SystemRewriteOptions* archive_options) {
  fetch->response_headers()->SetDateAndCaching(
      timer_->NowMs(), 0, ", private, no-store");
  AdminHtml admin_html("html_cache", "", source, timer_, fetch,
                       message_handler_);
  fetch->Write(controls, message_handler_);
  std::vector<HtmlCacheRow> rows;
  ReadRecentHtmlRewrites(message_handler_, &rows, stored);
  fetch->Write(
      "<section class='pagespeed-html-cache-page'>"
      "<div class='pagespeed-dashboard-heading'>"
      "<p class='pagespeed-dashboard-eyebrow'>HTML OPTIMIZATION</p>"
      "<h1>HTML rewrite cache</h1>"
      "<p class='pagespeed-dashboard-description'>Recent completed HTML "
      "rewrites, isolated by request headers and browser capabilities.</p>"
      "</div>", message_handler_);
  const auto previous = PreviousDashboardCounters(stats, archive_options);
  WriteHtmlCacheSummary(stats, fetch, message_handler_, previous);
  fetch->Write(StrCat(
      "<section class='pagespeed-dashboard-panel'><div "
      "class='pagespeed-dashboard-panel-heading'><div>"
      "<h2>Recent HTML publications</h2><p>HTML cache: <strong>",
      options.html_cache() ? "Enabled" : "Disabled",
      "</strong>. Configured TTL cap: ",
      Integer64ToString(options.html_cache_ttl_sec()), " seconds.</p></div>"
      "<a href='config'>View configuration &rarr;</a></div>",
      stored ? "<p class='pagespeed-dashboard-footnote'>Recent publications merged from memory with retained history. " :
      "<p class='pagespeed-dashboard-footnote'>Up to 100 recently recorded "
      "variants from the bounded message buffer (MessageBufferSize must be nonzero). ",
      "This is publication history, not a live cache inventory: "
      "eviction, purge, policy changes, or new beacon evidence can invalidate "
      "an entry before its TTL. No HTML bodies or raw request headers are "
      "recorded. Opening a URL makes a new request using your browser's "
      "headers; it is not a snapshot of the listed variant.</p>"),
      message_handler_);
  if (rows.empty()) {
    fetch->Write(stored ?
        "<div class='pagespeed-images-empty'><h2>No HTML publications match</h2>"
        "<p>Try another date range or an older page.</p></div></section></section>" :
        "<div class='pagespeed-images-empty'><h2>No HTML rewrites recorded</h2>"
        "<p>Enable HtmlCache and MessageBufferSize, then visit a publicly "
        "cacheable HTML page. Entries appear after beacon learning and "
        "resource rewrites settle. Private requests and sampled beacon "
        "responses are not recorded.</p></div></section></section>",
        message_handler_);
    return;
  }
  fetch->Write(
      "<div class='pagespeed-images-toolbar'><label "
      "class='pagespeed-images-search-wrap'><input type='search' "
      "id='pagespeed-html-search' placeholder='Search page URL or variant' "
      "aria-label='Search page URL or variant'></label><div>"
      "<label class='pagespeed-images-filter-label'>Device "
      "<select id='pagespeed-html-device' aria-label='Filter by device'>"
      "<option value='all'>All devices</option><option value='desktop'>"
      "Desktop</option><option value='mobile'>Mobile</option>"
      "<option value='tablet'>Tablet</option></select></label>"
      "<label class='pagespeed-images-filter-label'>Capabilities "
      "<select id='pagespeed-html-format' aria-label='Filter by image capability'>"
      "<option value='all'>All capabilities</option>"
      "<option>AVIF capable</option><option>WebP capable</option>"
      "<option>Original formats</option></select></label></div></div>"
      "<div class='pagespeed-images-table-wrap'><table "
      "class='pagespeed-admin-data-table pagespeed-html-cache-table'>"
      "<thead><tr><th>Created</th><th>Page URL</th><th>Device</th>"
      "<th>Image capability</th><th>Variant</th><th>HTML size</th>"
      "<th>Age</th><th>Expires / TTL</th></tr></thead>"
      "<tbody id='pagespeed-html-rows'>", message_handler_);
  for (const HtmlCacheRow& row : rows) {
    GoogleString escaped_url, escaped_variant;
    HtmlKeywords::Escape(row.url, &escaped_url);
    HtmlKeywords::Escape(row.variant, &escaped_variant);
    GoogleString html;
    html += StrCat("<tr data-device='", row.device, "' data-format='",
              row.formats, "' data-variant='", escaped_variant,
              "'><td><time data-html-time='",
              Integer64ToString(row.created_ms), "'>",
              Integer64ToString(row.created_ms), "</time></td>");
    html += StrCat("<td class='pagespeed-image-url'><a href='", escaped_url,
              "' target='_blank' rel='noopener noreferrer' "
              "referrerpolicy='no-referrer'>", escaped_url,
              "</a></td><td>", row.device, "</td><td>", row.formats,
              "</td><td><code title='", escaped_variant, "'>");
    GoogleString short_variant;
    HtmlKeywords::Escape(row.variant.substr(row.variant.size() - 12),
                          &short_variant);
    html += StrCat(short_variant, "</code></td><td>",
              FormatDashboardBytes(row.bytes), "</td><td data-html-age='",
              Integer64ToString(row.created_ms), "'>",
              Integer64ToString(std::max<int64>(0,
                  timer_->NowMs() - row.created_ms) / 1000), "s</td>");
    html += StrCat("<td><time data-html-time='",
              Integer64ToString(row.expires_ms), "'>",
              Integer64ToString(row.expires_ms),
              "</time><span class='pagespeed-html-ttl' data-html-expiry='",
              Integer64ToString(row.expires_ms), "'>",
              row.expires_ms > timer_->NowMs() ? "Within TTL" : "TTL elapsed",
              "</span></td></tr>");
    fetch->Write(html, message_handler_);
  }
  fetch->Write(
      "</tbody></table></div><p id='pagespeed-html-results' "
      "class='pagespeed-dashboard-footnote' role='status' aria-live='polite'>"
      "</p></section></section><script>(function(){"
      "var search=document.getElementById('pagespeed-html-search'),"
      "device=document.getElementById('pagespeed-html-device'),"
      "format=document.getElementById('pagespeed-html-format'),"
      "rows=document.getElementById('pagespeed-html-rows').rows,"
      "result=document.getElementById('pagespeed-html-results');"
      "function filter(){var q=search.value.toLowerCase(),n=0;"
      "for(var i=0;i<rows.length;i++){var r=rows[i],show="
      "(r.textContent+' '+r.getAttribute('data-variant')).toLowerCase()."
      "indexOf(q)!==-1&&"
      "(device.value==='all'||r.getAttribute('data-device')===device.value)&&"
      "(format.value==='all'||r.getAttribute('data-format')===format.value);"
      "r.hidden=!show;if(show)n++;}result.textContent=n+' of '+rows.length+"
      "' recorded variants shown. Refresh to load new publications.';}"
      "function times(){var now=Date.now(),nodes=document.querySelectorAll("
      "'[data-html-time]');for(var i=0;i<nodes.length;i++){var t=Number("
      "nodes[i].getAttribute('data-html-time'));nodes[i].textContent="
      "new Date(t).toLocaleString();}nodes=document.querySelectorAll("
      "'[data-html-age]');for(var j=0;j<nodes.length;j++)nodes[j].textContent="
      "Math.max(0,Math.floor((now-Number(nodes[j].getAttribute('data-html-age')))"
      "/1000))+'s';nodes=document.querySelectorAll('[data-html-expiry]');"
      "for(var k=0;k<nodes.length;k++){var n=nodes[k],valid=now<Number("
      "n.getAttribute('data-html-expiry'));n.textContent=valid?'Within TTL':"
      "'TTL elapsed';n.setAttribute('data-elapsed',valid?'false':'true');}}"
      "search.addEventListener('input',filter);device.addEventListener('change',"
      "filter);format.addEventListener('change',filter);times();filter();"
      "window.setInterval(times,1000);})();</script>", message_handler_);
}

void AdminSite::ImagesHandler(AdminSource source, AsyncFetch* fetch,
                              const GoogleString* stored, StringPiece controls) {
  std::vector<ImageRewriteRow> rows;
  int64 total_saved = 0;
  static const size_t kMaxRows = 100;
  ReadRecentImageRewrites(message_handler_, kMaxRows, &rows, stored);
  for (const ImageRewriteRow& row : rows) {
    total_saved += row.original_bytes - row.optimized_bytes;
  }

  AdminHtml admin_html("images", "", source, timer_, fetch,
                       message_handler_);
  fetch->Write(controls, message_handler_);
  fetch->Write(StrCat(
      "<section class='pagespeed-images-page'>"
      "<div class='pagespeed-dashboard-heading'>"
      "<p class='pagespeed-dashboard-eyebrow'>IMAGE OPTIMIZATION</p>"
      "<h1>Recently optimized images</h1>"
      "<p class='pagespeed-dashboard-description'>",
      stored ? "Recent rewrites merged from memory with retained history." :
      "Recent successful rewrites recorded in the PageSpeed message buffer.",
      "</p></div><div class='pagespeed-images-summary'>"), message_handler_);
  fetch->Write(StrCat("<strong>",
                      IntegerToString(static_cast<int>(rows.size())),
                      "</strong><span>recent rewrites</span><strong>",
                      FormatDashboardBytes(total_saved),
                      "</strong><span>saved in this list</span></div>"),
               message_handler_);

  if (rows.empty()) {
    fetch->Write(stored ?
        "<div class='pagespeed-images-empty'><h2>No archived images match</h2>"
        "<p>Try another date range or an older page.</p></div></section>" :
        "<div class='pagespeed-images-empty'><h2>No image rewrites found</h2>"
        "<p>Successful image rewrites will appear here. The message buffer "
        "must be enabled, and this list only includes entries still retained "
        "in that bounded buffer.</p></div></section>",
        message_handler_);
  } else {
    fetch->Write(
        "<div class='pagespeed-images-toolbar'><label "
        "class='pagespeed-images-search-wrap'><input type='search' "
        "id='pagespeed-images-search' placeholder='Search image URL or format' "
        "aria-label='Search image URL or format'></label><div>"
        "<label class='pagespeed-images-filter-label'>Format "
        "<select id='pagespeed-images-format' aria-label='Filter by format'>"
        "<option value='all'>All formats</option><option value='image/avif'>"
        "AVIF</option><option value='image/webp'>WebP</option>"
        "<option value='image/jpeg'>JPEG</option><option value='image/png'>"
        "PNG</option></select></label><label class='pagespeed-images-filter-label'>"
        "Sort <select id='pagespeed-images-sort' aria-label='Sort images'>"
        "<option value='newest'>Newest first</option><option value='saved'>"
        "Most saved</option></select></label></div></div>"
        "<div class='pagespeed-images-table-wrap'><table "
        "class='pagespeed-admin-data-table pagespeed-images-table'><thead><tr>"
        "<th>Date</th><th>Original</th><th>Format</th><th>Before</th>"
        "<th>After</th><th>Saved</th><th>Preview</th>"
        "</tr></thead><tbody id='pagespeed-images-rows'>",
        message_handler_);
    for (const ImageRewriteRow& row : rows) {
      GoogleString escaped_original, escaped_optimized, escaped_mime;
      HtmlKeywords::Escape(row.original_url, &escaped_original);
      HtmlKeywords::Escape(row.optimized_url, &escaped_optimized);
      HtmlKeywords::Escape(row.output_mime_type, &escaped_mime);
      int64 saved = row.original_bytes - row.optimized_bytes;
      GoogleString row_html;
      StrAppend(&row_html,
                "<tr data-format='", escaped_mime,
                "' data-timestamp-ms='",
                Integer64ToString(row.timestamp_ms), "' data-saved-bytes='",
                Integer64ToString(saved),
                "'><td><time class='pagespeed-image-time' ");
      StrAppend(&row_html, "data-timestamp-ms='",
                Integer64ToString(row.timestamp_ms), "'>Loading date...",
                "</time></td><td class='pagespeed-image-url'><a href='");
      StrAppend(&row_html, escaped_original,
                "' target='_blank' rel='noopener noreferrer' "
                "referrerpolicy='no-referrer' title='Open original image'>",
                escaped_original, "</a></td><td><code>", escaped_mime,
                "</code></td><td>", FormatDashboardBytes(row.original_bytes),
                "</td><td>");
      StrAppend(&row_html, FormatDashboardBytes(row.optimized_bytes),
                "</td><td class='pagespeed-image-saved'>");
      StrAppend(&row_html, FormatDashboardBytes(saved),
                "</td><td><button type='button' class='pagespeed-image-"
                "preview-button' data-original-url='");
      StrAppend(&row_html, escaped_original, "' data-optimized-url='");
      StrAppend(&row_html, escaped_optimized,
                "' data-format='", escaped_mime, "' data-original-bytes='");
      StrAppend(&row_html, Integer64ToString(row.original_bytes),
                "' data-optimized-bytes='");
      StrAppend(&row_html, Integer64ToString(row.optimized_bytes),
                "'>Compare</button></td></tr>");
      fetch->Write(row_html, message_handler_);
    }
    fetch->Write(
        "</tbody></table></div><div class='pagespeed-images-list-footer'>"
        "<span id='pagespeed-images-result-count'></span><span>Filters apply to "
        "the records loaded in this page.</span></div>"
        "<div id='pagespeed-images-empty-filter' class='pagespeed-images-empty-"
        "filter' style='display:none'>No images match those filters.</div>"
        "<div class='pagespeed-image-modal' "
        "id='pagespeed-image-modal' role='dialog' aria-modal='true' "
        "aria-labelledby='pagespeed-image-modal-title' aria-hidden='true'>"
        "<div class='pagespeed-image-modal-card'><div class='pagespeed-image-"
        "modal-header'><div><h2 id='pagespeed-image-modal-title'>Image "
        "comparison</h2><p id='pagespeed-image-modal-meta'></p></div>"
        "<button type='button' class='pagespeed-image-modal-close' "
        "aria-label='Close image comparison'>&times;</button></div>"
        "<div class='pagespeed-image-modal-compare'><figure><img "
        "id='pagespeed-image-modal-original' alt='Original image' "
        "referrerpolicy='no-referrer'><figcaption><span>Original</span>"
        "<span><strong id='pagespeed-image-modal-original-size'></strong> "
        "<a id='pagespeed-image-modal-original-link' target='_blank' "
        "rel='noopener noreferrer' referrerpolicy='no-referrer'>Open</a></span>"
        "</figcaption>"
        "</figure><figure><img id='pagespeed-image-modal-optimized' "
        "alt='Optimized image' referrerpolicy='no-referrer'><figcaption>"
        "<span><span id='pagespeed-image-modal-format'></span><br><strong "
        "id='pagespeed-image-modal-optimized-size'></strong></span>"
        "<a id='pagespeed-image-modal-optimized-link' target='_blank' "
        "rel='noopener noreferrer' referrerpolicy='no-referrer'>Open</a>"
        "</figcaption>"
        "</figure></div><p class='pagespeed-image-modal-saved' "
        "id='pagespeed-image-modal-saved'></p></div></div></section>",
        message_handler_);
    fetch->Write(
        "<script>(function(){var times=document.querySelectorAll("
        "'.pagespeed-image-time');for(var i=0;i<times.length;i++){var t=Number("
        "times[i].getAttribute('data-timestamp-ms'));if(t>0){times[i].textContent="
        "new Date(t).toLocaleString();}}var modal=document.getElementById("
        "'pagespeed-image-modal');var rows=document.getElementById("
        "'pagespeed-images-rows');if(rows){var search=document.getElementById("
        "'pagespeed-images-search');var format=document.getElementById("
        "'pagespeed-images-format');var sort=document.getElementById("
        "'pagespeed-images-sort');var entries=Array.prototype.slice.call("
        "rows.querySelectorAll('tr'));var count=document.getElementById("
        "'pagespeed-images-result-count');var empty=document.getElementById("
        "'pagespeed-images-empty-filter');function filterRows(){var query=search."
        "value.toLowerCase();var selected=format.value;entries.sort(function(a,b){"
        "return sort.value==='saved'?Number(b.getAttribute('data-saved-bytes'))-"
        "Number(a.getAttribute('data-saved-bytes')):Number(b.getAttribute("
        "'data-timestamp-ms'))-Number(a.getAttribute('data-timestamp-ms'));});"
        "var visible=0;for(var j=0;j<entries.length;j++){var match=(selected==="
        "'all'||entries[j].getAttribute('data-format')===selected)&&"
        "entries[j].textContent.toLowerCase().indexOf(query)!==-1;entries[j]."
        "style.display=match?'':'none';if(match)visible++;rows.appendChild("
        "entries[j]);}count.textContent='Showing '+visible+' of '+entries.length+"
        "' rewrites';empty.style.display=visible?'none':'block';}search.addEventListener("
        "'input',filterRows);format.addEventListener('change',filterRows);sort."
        "addEventListener('change',filterRows);filterRows();}if(!modal)return;"
        "var close=modal.querySelector("
        "'.pagespeed-image-modal-close');function hide(){modal.classList.remove("
        "'is-open');modal.setAttribute('aria-hidden','true');document.body."
        "style.overflow='';if(lastTrigger)lastTrigger.focus();}var lastTrigger=null;"
        "document.addEventListener('click',function(event){"
        "var button=event.target.closest('.pagespeed-image-preview-button');"
        "if(button){var original=button.getAttribute('data-original-url');"
        "var optimized=button.getAttribute('data-optimized-url');var format="
        "button.getAttribute('data-format');var before=Number(button.getAttribute("
        "'data-original-bytes'));var after=Number(button.getAttribute("
        "'data-optimized-bytes'));document.getElementById("
        "'pagespeed-image-modal-title').textContent=original;document.getElementById("
        "'pagespeed-image-modal-meta').textContent='Original and optimized image';"
        "document.getElementById('pagespeed-image-modal-original').src=original;"
        "document.getElementById('pagespeed-image-modal-optimized').src=optimized;"
        "document.getElementById('pagespeed-image-modal-original-link').href=original;"
        "document.getElementById('pagespeed-image-modal-optimized-link').href=optimized;"
        "document.getElementById('pagespeed-image-modal-format').textContent=format;"
        "document.getElementById('pagespeed-image-modal-original-size').textContent="
        "before.toLocaleString()+' bytes';document.getElementById("
        "'pagespeed-image-modal-optimized-size').textContent=after.toLocaleString()"
        "+' bytes';document.getElementById('pagespeed-image-modal-saved').textContent="
        "'Saved '+Math.max(0,before-after).toLocaleString()+' bytes ('+"
        "(before>0?Math.round((before-after)*100/before):0)+'% smaller)';modal."
        "classList.add('is-open');modal.setAttribute('aria-hidden','false');"
        "document.body.style.overflow='hidden';lastTrigger=button;close.focus();"
        "return;}if(event."
        "target===modal||event.target===close){hide();}});document.addEventListener("
        "'keydown',function(event){if(event.key==='Escape'&&modal.classList."
        "contains('is-open'))hide();});})();</script>",
        message_handler_);
  }
}

void AdminSite::GraphsHandler(const RewriteOptions& options, AdminSource source,
                              const QueryParams& query_params,
                              AsyncFetch* fetch, Statistics* statistics) {
  if (query_params.Has("json")) {
    ConsoleJsonHandler(query_params, fetch, statistics);
    return;
  }
  GoogleString head_markup = StrCat("<style>", CSS_graphs_css, "</style>\n");
  AdminHtml admin_html("graphs", head_markup, source, timer_, fetch,
                       message_handler_);
  WriteAdminPageHeading(fetch, message_handler_, "TRENDS", "Graphs",
                        "Track cache behavior, image rewrites, and runtime "
                        "activity over time.");
  fetch->Write(
      "<div id='cache_applied'>Loading Charts...</div>"
      "<div id='cache_type'>Loading Charts...</div>"
      "<div id='ipro'>Loading Charts...</div>"
      "<div id='image_rewriting'>Loading Charts...</div>"
      "<div id='realtime'>Loading Charts...</div>",
      message_handler_);
  StringPiece graphs_js =
      options.Enabled(RewriteOptions::kDebug) ? JS_graphs_js : JS_graphs_js_opt;
  fetch->Write(StrCat("<script type='text/javascript'>", graphs_js,
                      "\npagespeed.Graphs.Start();</script>\n"),
               message_handler_);
}

void AdminSite::ConsoleJsonHandler(const QueryParams& params, AsyncFetch* fetch,
                                   Statistics* statistics) {
  StatisticsLogger* console_logger = statistics->console_logger();
  if (console_logger == nullptr) {
    fetch->response_headers()->SetStatusAndReason(HttpStatus::kNotFound);
    fetch->response_headers()->Add(HttpAttributes::kContentType, "text/plain");
    fetch->Write(
        "console_logger must be enabled to use '?json' query parameter.",
        message_handler_);
  } else {
    fetch->response_headers()->SetStatusAndReason(HttpStatus::kOK);

    // TODO(morlovich): It would be more secure to do:
    //    Content-Type: application/javascript; charset=utf-8
    // instead of using a JSON one.  There are probably a few other
    // anti-sniffing headers we could add as well.
    //
    // Also, it would be good to start what we serve with )]}' and a newline,
    // and updating the client js, to make completely sure the browser can't be
    // tricked into running it.
    fetch->response_headers()->Add(HttpAttributes::kContentType,
                                   kContentTypeJson.mime_type());

    std::set<GoogleString> var_titles;
    // Default is to fetch data used on graphs page.
    bool dump_for_graphs = true;

    // Default values for start_time, end_time, and granularity_ms in case the
    // query does not include these parameters.
    int64 start_time = 0;
    int64 end_time = timer_->NowMs();
    // Granularity is the difference in ms between data points. If it is not
    // specified by the query, the default value is 3000 ms, the same as the
    // default logging granularity.
    int64 granularity_ms = 3000;
    for (int i = 0; i < params.size(); ++i) {
      GoogleString value;
      if (params.UnescapedValue(i, &value)) {
        StringPiece name = params.name(i);
        if (name == "start_time") {
          StringToInt64(value, &start_time);
        } else if (name == "end_time") {
          StringToInt64(value, &end_time);
        } else if (name == "var_titles") {
          // Fetch specified data if users populate var_titles.
          dump_for_graphs = false;
          std::vector<StringPiece> variable_names;
          SplitStringPieceToVector(value, ",", &variable_names, true);
          for (size_t i = 0; i < variable_names.size(); ++i) {
            var_titles.insert(variable_names[i].as_string());
          }
        } else if (name == "granularity") {
          StringToInt64(value, &granularity_ms);
        }
      }
    }
    console_logger->DumpJSON(dump_for_graphs, var_titles, start_time, end_time,
                             granularity_ms, fetch, message_handler_);
  }
  fetch->Done(true);
}

void AdminSite::PrintHistograms(AdminSource source, AsyncFetch* fetch,
                                Statistics* stats) {
  AdminHtml admin_html("histograms", "", source, timer_, fetch,
                       message_handler_);
  WriteAdminPageHeading(fetch, message_handler_, "DISTRIBUTIONS", "Histograms",
                        "Review the spread and percentiles of measured "
                        "PageSpeed operations.");
  fetch->Write("<div id='pagespeed-histograms'>", message_handler_);
  stats->RenderHistograms(fetch, message_handler_);
  fetch->Write("</div>", message_handler_);
}

namespace {

static const char kTableStart[] =
    "<div class='pagespeed-admin-table-wrap'><table "
    "class='pagespeed-admin-data-table pagespeed-caches-structure'>\n"
    "  <thead>\n"
    "    <tr>\n"
    "      <th>Cache</th><th>Detail</th><th>Structure</th>\n"
    "    </tr>\n"
    "  </thead>\n"
    "  <tbody>";

static const char kTableEnd[] =
    "  </tbody>\n"
    "</table></div>";

// Takes a complicated descriptor like
//    "HTTPCache(Fallback(small=Batcher(cache=Stats(parefix=memcached_async,"
//    "cache=Async(AprMemCache)),parallelism=1,max=1000),large=Stats("
//    "prefix=file_cache,cache=FileCache)))"
// and strips away the crap most users don't want to see, as they most
// likely did not configure it, and return
//    "Async AprMemCache FileCache"
GoogleString HackCacheDescriptor(StringPiece name) {
  GoogleString out;
  // There's a lot of complicated syntax in the cache name giving the
  // detailed hierarchical structure.  This is really hard to read and
  // overly cryptic; it's designed for unit tests.  But let's extract
  // a few keywords out of this to understand the main pointers.
  static const char* kCacheKeywords[] = {
      "Compressed",  "Async",     "SharedMemCache", "LRUCache",
      "AprMemCache", "FileCache", "RedisCache"};
  const char* delim = "";
  for (int i = 0, n = arraysize(kCacheKeywords); i < n; ++i) {
    if (name.find(kCacheKeywords[i]) != StringPiece::npos) {
      StrAppend(&out, delim, kCacheKeywords[i]);
      delim = " ";
    }
  }
  if (out.empty()) {
    name.CopyToString(&out);
  }
  return out;
}

// Takes a complicated descriptor like
//    "HTTPCache(Fallback(small=Batcher(cache=Stats(prefix=memcached_async,"
//    "cache=Async(AprMemCache)),parallelism=1,max=1000),large=Stats("
//    "prefix=file_cache,cache=FileCache)))"
// and injects HTML line-breaks and indentation based on the parent depth,
// yielding HTML that renders like this (with &nbsp; and <br/>)
//    HTTPCache(
//       Fallback(
//          small=Batcher(
//             cache=Stats(
//                prefix=memcached_async,
//                cache=Async(
//                   AprMemCache)),
//             parallelism=1,
//             max=1000),
//          large=Stats(
//             prefix=file_cache,
//             cache=FileCache)))
GoogleString IndentCacheDescriptor(StringPiece name) {
  GoogleString out, buf;
  int depth = 0;
  for (int i = 0, n = name.size(); i < n; ++i) {
    StrAppend(&out, HtmlKeywords::Escape(name.substr(i, 1), &buf));
    switch (name[i]) {
      case '(':
        ++depth;
        FALLTHROUGH_INTENDED;
      case ',':
        out += "<br/>";
        for (int j = 0; j < depth; ++j) {
          out += "&nbsp; &nbsp;";
        }
        break;
      case ')':
        --depth;
        break;
    }
  }
  return out;
}

GoogleString CacheInfoHtmlSnippet(StringPiece label, StringPiece descriptor) {
  GoogleString out, escaped;
  StrAppend(&out, "<tr style='vertical-align:top;'><td>", label,
            "</td><td><input id='", label);
  StrAppend(&out, "_toggle' type='checkbox' ",
            "onclick=\"pagespeed.Caches.toggleDetail('", label,
            "')\"/></td><td><code id='", label, "_summary'>");
  StrAppend(&out,
            HtmlKeywords::Escape(HackCacheDescriptor(descriptor), &escaped));
  StrAppend(&out, "</code><code id='", label,
            "_detail' style='display:none;'>");
  StrAppend(&out, IndentCacheDescriptor(descriptor));
  StrAppend(&out, "</code></td></tr>\n");
  return out;
}

}  // namespace

void AdminSite::PrintCaches(
    bool is_global, AdminSource source, const GoogleUrl& stripped_gurl,
    const QueryParams& query_params, const RewriteOptions* options,
    SystemCachePath* cache_path, AsyncFetch* fetch, SystemCaches* system_caches,
    CacheInterface* filesystem_metadata_cache, HTTPCache* http_cache,
    CacheInterface* metadata_cache, PropertyCache* page_property_cache,
    ServerContext* server_context) {
  GoogleString url;
  if ((source == kPageSpeedAdmin) &&
      query_params.Lookup1Unescaped("url", &url)) {
    // Delegate to ShowCacheHandler to get the cached value for that
    // URL, which it may do asynchronously, so we cannot use the
    // AdminHtml abstraction which closes the connection in its
    // destructor.
    GoogleString json_dummy;
    ServerContext::Format format = ServerContext::kFormatAsHtml;
    if (query_params.Lookup1Unescaped("json", &json_dummy)) {
      format = ServerContext::kFormatAsJson;
    }
    GoogleString ua;
    query_params.Lookup1Unescaped("user_agent", &ua);
    server_context->ShowCacheHandler(
        format, url, ua, query_params.Has("Delete"), fetch, options->Clone());
  } else if ((source == kPageSpeedAdmin) &&
             query_params.Lookup1Unescaped("new_set", &url)) {
    ResponseHeaders* response_headers = fetch->response_headers();
    response_headers->SetStatusAndReason(HttpStatus::kOK);
    response_headers->Add(HttpAttributes::kContentType, "text/html");
    fetch->Write(options->PurgeSetString(), message_handler_);
    fetch->Done(true);
  } else if ((source == kPageSpeedAdmin) &&
             query_params.Lookup1Unescaped("purge", &url)) {
    ResponseHeaders* response_headers = fetch->response_headers();
    if (!options->enable_cache_purge()) {
      response_headers->SetStatusAndReason(HttpStatus::kOK);
      response_headers->Add(HttpAttributes::kContentType, "text/html");
      // TODO(jmarantz): virtualize the formatting of this message so that
      // it's correct in ngx_pagespeed and mod_pagespeed (and IISpeed etc).
      fetch->Write("Purging not enabled: please add\n", message_handler_);
      HtmlKeywords::WritePre(
          StrCat("  ", server_context->FormatOption("EnableCachePurge", "on")),
          "", fetch, message_handler_);
      fetch->Write(
          "\nto your configuration file. See the \n"
          "<a href='https://developers.google.com/speed/pagespeed/module"
          "/system#purge_cache'>documentation</a> "
          "for more information about cache purging.",
          message_handler_);
      fetch->Done(true);
    } else if (url == "*") {
      PurgeHandler(url, cache_path, fetch);
    } else if (url.empty()) {
      response_headers->SetStatusAndReason(HttpStatus::kOK);
      response_headers->Add(HttpAttributes::kContentType, "text/html");
      fetch->Write("Empty URL", message_handler_);
      fetch->Done(true);
    } else {
      GoogleUrl origin(stripped_gurl.Origin());
      GoogleUrl resolved(origin, url);
      if (!resolved.IsWebValid()) {
        response_headers->SetStatusAndReason(HttpStatus::kOK);
        response_headers->Add(HttpAttributes::kContentType, "text/html");
        GoogleString escaped_url;
        HtmlKeywords::Escape(url, &escaped_url);
        fetch->Write(StrCat("Invalid URL: ", escaped_url), message_handler_);
        fetch->Done(true);
      } else {
        PurgeHandler(resolved.Spec(), cache_path, fetch);
      }
    }
  } else {
    GoogleString head_markup = StrCat("<style>", CSS_caches_css, "</style>\n");
    AdminHtml admin_html("cache", head_markup, source, timer_, fetch,
                         message_handler_);
    WriteAdminPageHeading(fetch, message_handler_, "CACHE INSPECTION", "Caches",
                          "Inspect cache entries, backend structures, and "
                          "URL invalidations.");

    fetch->Write("<div id='show_metadata'>", message_handler_);
    // Present a small form to enter a URL.
    if (source == kPageSpeedAdmin) {
      fetch->Write(
          "<div class='pagespeed-cache-section-heading'>"
          "<h2>Metadata lookup</h2>"
          "<p>Inspect the cached PageSpeed metadata for a URL and "
          "User-Agent.</p>"
          "</div>",
          message_handler_);
      const char* user_agent =
          fetch->request_headers()->Lookup1(HttpAttributes::kUserAgent);
      fetch->Write(ServerContext::ShowCacheForm(user_agent), message_handler_);
    }
    fetch->Write("</div>\n", message_handler_);
    // Display configured cache information.
    if (system_caches != nullptr) {
      int flags = SystemCaches::kDefaultStatFlags;
      if (is_global) {
        flags |= SystemCaches::kGlobalView;
      }

      // TODO(jmarantz): Consider whether it makes sense to disable
      // either of these flags to limit the content when someone asks
      // for info about the cache.
      flags |= SystemCaches::kIncludeMemcached;
      flags |= SystemCaches::kIncludeRedis;
      fetch->Write(
          "<div id='cache_struct'>"
          "<div class='pagespeed-cache-section-heading'>"
          "<h2>Cache structure</h2>"
          "<p>Configured storage layers and how PageSpeed composes them.</p>"
          "</div>",
          message_handler_);
      fetch->Write(kTableStart, message_handler_);
      CacheInterface* fsmdc = filesystem_metadata_cache;
      fetch->Write(
          StrCat(CacheInfoHtmlSnippet("HTTP Cache", http_cache->Name()),
                 CacheInfoHtmlSnippet("Metadata Cache", metadata_cache->Name()),
                 CacheInfoHtmlSnippet(
                     "Property Cache",
                     page_property_cache->property_store()->Name()),
                 CacheInfoHtmlSnippet(
                     "FileSystem Metadata Cache",
                     (fsmdc == nullptr) ? "none" : fsmdc->Name())),
          message_handler_);
      fetch->Write(kTableEnd, message_handler_);
      fetch->Write("</div>", message_handler_);

      fetch->Write(
          "<div id='physical_cache'>"
          "<div class='pagespeed-cache-section-heading'>"
          "<h2>Backend statistics</h2>"
          "<p>Live size and activity reported by configured cache backends.</p>"
          "</div>",
          message_handler_);
      GoogleString backend_stats;
      system_caches->PrintCacheStats(
          static_cast<SystemCaches::StatFlags>(flags), &backend_stats);
      if (!backend_stats.empty()) {
        HtmlKeywords::WritePre(backend_stats, "", fetch, message_handler_);
      }
      fetch->Write("</div>", message_handler_);

      fetch->Write("<div id='purge_cache'>", message_handler_);
      // Filled in by JS in caches.js: updatePurgeSet().
      fetch->Write(
          "<div class='pagespeed-cache-section-heading'>"
          "<h2>Purge set</h2>"
          "<p>Review invalidations and purge a URL or the full PageSpeed cache.</p>"
          "</div>"
          "<div id='purge_global'"
          " class='pagespeed-caches-purge-global'></div>"
          "<div id='purge_table'"
          " class='pagespeed-caches-purge-table'></div>"
          "</div>",  // closes 'purge_cache'.
          message_handler_);
    }
    StringPiece caches_js = options->Enabled(RewriteOptions::kDebug)
                                ? JS_caches_js
                                : JS_caches_js_opt;
    // Practice what we preach: put the blocking JS in the tail.
    // TODO(jmarantz): use static asset manager to compile & deliver JS
    // externally.
    fetch->Write(StrCat("<script type='text/javascript'>", caches_js,
                        "\npagespeed.Caches.Start();</script>\n"),
                 message_handler_);
  }
}

void AdminSite::PrintConfig(
    AdminSource source, AsyncFetch* fetch,
    SystemRewriteOptions* global_system_rewrite_options) {
  AdminHtml admin_html("config", "", source, timer_, fetch, message_handler_);
  WriteAdminPageHeading(fetch, message_handler_, "ACTIVE SETTINGS",
                        "Configuration",
                        "View the effective PageSpeed options for this "
                        "server instance.");
  fetch->Write(
      "<div class='pagespeed-config-page'>"
      "<div id='pagespeed-config-root'></div>"
      "<div id='pagespeed-config-source' class='pagespeed-config-source'>",
      message_handler_);
  HtmlKeywords::WritePre(global_system_rewrite_options->OptionsToString(), "",
                         fetch, message_handler_);
  fetch->Write(
      "</div></div><script type='text/javascript'>",
      message_handler_);
  fetch->Write(JS_config_js, message_handler_);
  fetch->Write("</script>\n", message_handler_);
}

void AdminSite::MessageHistoryHandler(const RewriteOptions& options,
                                      AdminSource source, AsyncFetch* fetch,
                                      const GoogleString* stored,
                                      StringPiece controls, StringPiece kind) {
  // Request for page /mod_pagespeed_message.
  GoogleString log;
  StringWriter log_writer(&log);
  AdminHtml admin_html(kind == "messages" ? "message_history" : kind, "",
                       source, timer_, fetch, message_handler_);
  fetch->Write(controls, message_handler_);
  WriteAdminPageHeading(fetch, message_handler_, "EVENT LOG",
      kind == "javascript" ? "JavaScript diagnostics" :
      (kind == "css" ? "CSS diagnostics" : "Message History"),
      kind == "messages" ? "Review PageSpeed notices, warnings, and rewrite diagnostics." :
      "Existing diagnostic messages only; not a complete per-resource rewrite history.");
  if (stored != nullptr) log = *stored;
  bool have_log = stored != nullptr || message_handler_->Dump(&log_writer);
  if (have_log && kind != "messages") {
    StringPieceVector records;
    message_handler_->ParseMessageDumpIntoMessages(log, &records);
    GoogleString filtered("\n");
    for (StringPiece record : records)
      if (HistoryArchive::Matches(kind, record)) StrAppend(&filtered, record, "\n");
    log.swap(filtered);
  }
  if (have_log) {
    fetch->Write("<div id='log'>", message_handler_);
    // Keep the raw record text in an escaped, machine-readable element. The
    // history UI extracts severity and timestamp without parsing HTML styling.
    StringPieceVector messages;
    message_handler_->ParseMessageDumpIntoMessages(log, &messages);
    for (int i = 0, size = messages.size(); i < size; ++i) {
      StringPiece display_message = messages[i];
      size_t detail_marker = display_message.find("\tPS_IMAGE_DETAIL\t");
      if (detail_marker != StringPiece::npos) {
        display_message = display_message.substr(0, detail_marker);
      }
      if (display_message.length() > 0) {
        const char* severity = "info";
        switch (message_handler_->GetMessageType(display_message)) {
          case kError: severity = "error"; break;
          case kWarning: severity = "warning"; break;
          case kFatal: severity = "fatal"; break;
          default: break;
        }
        GoogleString escaped_message;
        HtmlKeywords::Escape(message_handler_->ReformatMessage(display_message),
                             &escaped_message);
        fetch->Write(StrCat("<div class='pagespeed-message-entry' data-severity='",
                            severity, "'>", escaped_message, "</div>\n"),
                     message_handler_);
      }
    }
    fetch->Write("</div>\n", message_handler_);
    StringPiece messages_js = options.Enabled(RewriteOptions::kDebug)
                                  ? JS_messages_js
                                  : JS_messages_js_opt;
    fetch->Write(StrCat("<script type='text/javascript'>", messages_js,
                        "\npagespeed.Messages.Start();</script>\n"),
                 message_handler_);
  } else {
    fetch->Write(
        "<p>Writing to mod_pagespeed_message failed. \n"
        "Verify that MessageBufferSize is not set to 0 "
        "in pagespeed.conf.</p>\n",
        message_handler_);
  }
}

void AdminSite::AdminPage(
    bool is_global, const GoogleUrl& stripped_gurl,
    const QueryParams& query_params, const RewriteOptions* options,
    SystemCachePath* cache_path, AsyncFetch* fetch, SystemCaches* system_caches,
    CacheInterface* filesystem_metadata_cache, HTTPCache* http_cache,
    CacheInterface* metadata_cache, PropertyCache* page_property_cache,
    ServerContext* server_context, Statistics* statistics, Statistics* stats,
    SystemRewriteOptions* global_system_rewrite_options) {
  // The handler is "pagespeed_admin", so we must dispatch off of
  // the remainder of the URL.  For
  // "http://example.com/pagespeed_admin/foo?a=b" we want to pull out
  // "foo".
  //
  // Note that the comments here referring to "/pagespeed_admin" reflect
  // only the default admin path in Apache for fresh installs.  In fact
  // we can put the handler on any path, and this code should still work;
  // all the paths here are specified relative to the incoming URL.
  StringPiece path = stripped_gurl.PathSansQuery();  // "/pagespeed_admin/foo"
  path = path.substr(1);                             // "pagespeed_admin/foo"

  // If there are no slashes at all in the path, e.g. it's "pagespeed_admin",
  // then the relative references to "config" etc will not work.  We need
  // to serve the admin pages on "/pagespeed_admin/".  So if we got to this
  // point and there are no slashes, then we can just redirect immediately
  // by adding a slash.
  //
  // If the user has mapped the pagespeed_admin handler to a path with
  // an embedded slash, say "pagespeed/myadmin", then it's hard to tell
  // whether we should redirect, because we don't know what the the
  // intended path is.  In this case, we'll fall through to a leaf
  // analysis on "myadmin", fail to find a match, and print a "Did You Mean"
  // page.  It's not as good as a redirect but since we can't tell an
  // omitted slash from a typo it's the best we can do.
  if (path.find('/') == StringPiece::npos) {
    // If the URL is "/pagespeed_admin", then redirect to "/pagespeed_admin/" so
    // that relative URL references will work.
    ResponseHeaders* response_headers = fetch->response_headers();
    response_headers->SetStatusAndReason(HttpStatus::kMovedPermanently);
    GoogleString admin_with_slash = StrCat(stripped_gurl.AllExceptQuery(), "/");
    response_headers->Add(HttpAttributes::kLocation, admin_with_slash);
    response_headers->Add(HttpAttributes::kContentType, "text/html");
    GoogleString escaped_url;
    HtmlKeywords::Escape(admin_with_slash, &escaped_url);
    fetch->Write(StrCat("Redirecting to URL ", escaped_url), message_handler_);
    fetch->Done(true);
  } else {
    StringPiece leaf = stripped_gurl.LeafSansQuery();
    GoogleString history_log, history_controls;
    bool combined_history = false;
    bool dashboard_view = leaf == "dashboard" || leaf.empty();
    StringPiece kind = dashboard_view ? "images" :
        (leaf == "message_history" ? "messages" : leaf);
    if (kind == "messages" || kind == "images" || kind == "html_cache" ||
        kind == "javascript" || kind == "css" || dashboard_view) {
      history_controls = HistoryControls(*global_system_rewrite_options,
          query_params, kind, message_handler_, &history_log, &combined_history);
      if (global_system_rewrite_options->history_archive())
        fetch->response_headers()->SetDateAndCaching(timer_->NowMs(), 0,
                                                     ", private, no-store");
    }
    const GoogleString* stored = combined_history ? &history_log : nullptr;
    // Both summaries must use the same logger and matching live counters.
    Statistics* history_stats = stats;
    if ((dashboard_view || leaf == "html_cache") &&
        global_system_rewrite_options->history_archive() &&
        stats->console_logger() == nullptr && statistics->console_logger() != nullptr)
      history_stats = statistics;
    if ((leaf == "dashboard") || leaf.empty()) {
      DashboardHandler(kPageSpeedAdmin, fetch, history_stats, global_system_rewrite_options,
                       stored);
    } else if (leaf == "history") {
      HistoryArchiveHandler(kPageSpeedAdmin, fetch, *global_system_rewrite_options);
    } else if (leaf == "images") {
      ImagesHandler(kPageSpeedAdmin, fetch, stored, history_controls);
    } else if (leaf == "html_cache") {
      HtmlCacheHandler(kPageSpeedAdmin, fetch, history_stats, *options, stored,
                       history_controls, global_system_rewrite_options);
    } else if (leaf == "statistics") {
      StatisticsHandler(*options, kPageSpeedAdmin, fetch, stats);
    } else if (leaf == "stats_json") {
      StatisticsJsonHandler(fetch, stats);
    } else if (leaf == "graphs") {
      GraphsHandler(*options, kPageSpeedAdmin, query_params, fetch, statistics);
    } else if (leaf == "config") {
      PrintConfig(kPageSpeedAdmin, fetch, global_system_rewrite_options);
    } else if (leaf == "console") {
      // TODO(jmarantz): add vhost-local and aggregate message buffers.
      ConsoleHandler(*global_system_rewrite_options, *options, kPageSpeedAdmin,
                     query_params, fetch, statistics);
    } else if (leaf == "message_history" || leaf == "javascript" || leaf == "css") {
      MessageHistoryHandler(*options, kPageSpeedAdmin, fetch, stored,
                             history_controls, kind);
    } else if (leaf == "cache") {
      PrintCaches(is_global, kPageSpeedAdmin, stripped_gurl, query_params,
                  options, cache_path, fetch, system_caches,
                  filesystem_metadata_cache, http_cache, metadata_cache,
                  page_property_cache, server_context);
    } else if (leaf == "histograms") {
      PrintHistograms(kPageSpeedAdmin, fetch, stats);
    } else {
      fetch->response_headers()->SetStatusAndReason(HttpStatus::kNotFound);
      fetch->response_headers()->Add(HttpAttributes::kContentType, "text/html");
      fetch->Write("Unknown admin page: ", message_handler_);
      HtmlKeywords::WritePre(leaf, "", fetch, message_handler_);

      // It's possible that the handler is installed on /a/b/c, and we
      // are now reporting "unknown admin page: c".  This is kind of a guess,
      // but provide a nice link here to what might be the correct admin page.
      //
      // This is just a guess, so we don't want to redirect.
      fetch->Write("<br/>Did you mean to visit: ", message_handler_);
      GoogleString escaped_url;
      HtmlKeywords::Escape(StrCat(stripped_gurl.AllExceptQuery(), "/"),
                           &escaped_url);
      fetch->Write(
          StrCat("<a href='", escaped_url, "'>", escaped_url, "</a>\n"),
          message_handler_);
      fetch->Done(true);
    }
  }
}

void AdminSite::StatisticsPage(
    bool is_global, const QueryParams& query_params,
    const RewriteOptions* options, AsyncFetch* fetch,
    SystemCaches* system_caches, CacheInterface* filesystem_metadata_cache,
    HTTPCache* http_cache, CacheInterface* metadata_cache,
    PropertyCache* page_property_cache, ServerContext* server_context,
    Statistics* statistics, Statistics* stats,
    SystemRewriteOptions* global_system_rewrite_options) {
  if (query_params.Has("json")) {
    ConsoleJsonHandler(query_params, fetch, statistics);
  } else if (query_params.Has("config")) {
    PrintConfig(kStatistics, fetch, global_system_rewrite_options);
  } else if (query_params.Has("histograms")) {
    PrintHistograms(kStatistics, fetch, stats);
  } else if (query_params.Has("graphs")) {
    GraphsHandler(*options, kStatistics, query_params, fetch, statistics);
  } else if (query_params.Has("cache")) {
    GoogleUrl empty_url;
    PrintCaches(is_global, kStatistics, empty_url, query_params, options,
                nullptr,  // cache_path is reference from statistics page.
                fetch, system_caches, filesystem_metadata_cache, http_cache,
                metadata_cache, page_property_cache, server_context);
  } else {
    StatisticsHandler(*options, kStatistics, fetch, stats);
  }
}

namespace {

// Provides a Done(bool, StringPiece) entry point for use as a Purge
// callback. Translates the success into an Http status code for the
// AsyncFetch, sending any failure reason in the response body.
class PurgeFetchCallbackGasket {
 public:
  PurgeFetchCallbackGasket(AsyncFetch* fetch, MessageHandler* handler)
      : fetch_(fetch), message_handler_(handler) {}
  void Done(bool success, StringPiece reason) {
    ResponseHeaders* headers = fetch_->response_headers();
    headers->set_status_code(HttpStatus::kOK);
    headers->Add(HttpAttributes::kContentType, "text/html");
    // TODO(xqyin): Currently we may still return 'purge successful' even if
    // the URL does not exist in our cache. Figure out how to solve this case
    // while we don't want to search the whole cache which could be very large.
    if (success) {
      fetch_->Write("Purge successful", message_handler_);
    } else {
      GoogleString buf;
      fetch_->Write(HtmlKeywords::Escape(reason, &buf), message_handler_);
      fetch_->Write("\n", message_handler_);
      fetch_->Write(HtmlKeywords::Escape(error_, &buf), message_handler_);
    }
    fetch_->Done(true);
    delete this;
  }

  void set_error(StringPiece x) { x.CopyToString(&error_); }

 private:
  AsyncFetch* fetch_;
  MessageHandler* message_handler_;
  GoogleString error_;

  DISALLOW_COPY_AND_ASSIGN(PurgeFetchCallbackGasket);
};

}  // namespace

void AdminSite::PurgeHandler(StringPiece url, SystemCachePath* cache_path,
                             AsyncFetch* fetch) {
  PurgeContext* purge_context = cache_path->purge_context();
  int64 now_ms = timer_->NowMs();
  PurgeFetchCallbackGasket* gasket =
      new PurgeFetchCallbackGasket(fetch, message_handler_);
  PurgeContext::PurgeCallback* callback =
      NewCallback(gasket, &PurgeFetchCallbackGasket::Done);
  if (strings::EndsWith(url, "*")) {
    // If the url is "*" we'll just purge everything.  Note that we will
    // ignore any sub-paths in the expression.  We can only purge the
    // entire cache, or specific URLs, not general wildcards.
    purge_context->SetCachePurgeGlobalTimestampMs(now_ms, callback);
  } else {
    purge_context->AddPurgeUrl(url, now_ms, callback);
  }
}

}  // namespace net_instaweb
