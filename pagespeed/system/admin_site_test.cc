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

// Unit tests for AdminSite.

#include "pagespeed/system/admin_site.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include "net/instaweb/http/public/async_fetch.h"
#include "net/instaweb/rewriter/public/custom_rewrite_test_base.h"
#include "net/instaweb/rewriter/public/rewrite_driver.h"
#include "net/instaweb/rewriter/public/rewrite_driver_factory.h"
#include "net/instaweb/rewriter/public/rewrite_test_base.h"
#include "net/instaweb/rewriter/public/server_context.h"
#include "net/instaweb/rewriter/public/test_rewrite_driver_factory.h"
#include "pagespeed/system/system_rewrite_options.h"
#include "pagespeed/system/system_server_context.h"
#include "pagespeed/kernel/base/gmock.h"
#include "pagespeed/kernel/base/gtest.h"
#include "pagespeed/kernel/base/message_handler.h"
#include "pagespeed/kernel/base/base64_util.h"
#include "pagespeed/kernel/base/statistics.h"
#include "pagespeed/kernel/base/mock_message_handler.h"
#include "pagespeed/kernel/base/scoped_ptr.h"
#include "pagespeed/kernel/base/string.h"
#include "pagespeed/kernel/base/thread_system.h"
#include "pagespeed/kernel/http/google_url.h"
#include "pagespeed/kernel/http/http_names.h"
#include "pagespeed/kernel/http/query_params.h"
#include "pagespeed/kernel/util/platform.h"

namespace net_instaweb {

namespace {

class SystemServerContextNoProxyHtml : public SystemServerContext {
 public:
  explicit SystemServerContextNoProxyHtml(RewriteDriverFactory* factory)
      : SystemServerContext(factory, "fake_hostname", 80 /* fake port */) {
  }

  virtual bool ProxiesHtml() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(SystemServerContextNoProxyHtml);
};

class AdminSiteTest : public CustomRewriteTestBase<SystemRewriteOptions> {
 protected:
  AdminSiteTest()
      : thread_system_(Platform::CreateThreadSystem()),
        options_(new SystemRewriteOptions(thread_system_.get())),
        admin_site_(new AdminSite(factory()->static_asset_manager(), timer(),
                                  message_handler())) {
  }

  virtual void SetUp() {
    CustomRewriteTestBase<SystemRewriteOptions>::SetUp();
    // Message-dump parsing discards the first potentially partial line.
    message_handler()->Message(kInfo, "Start admin test message buffer.");
    server_context_.reset(SetupServerContext(options_.release()));
  }

  virtual void TearDown() {
    RewriteTestBase::TearDown();
  }

  // Set up the ServerContext. The ServerContext is only used for
  // PrintCaches method. If we remove this dependency later,
  // we don't need to set up ServerContext in this unit test.
  ServerContext* SetupServerContext(SystemRewriteOptions* config) {
    scoped_ptr<SystemServerContext> server_context(
        new SystemServerContextNoProxyHtml(factory()));
    server_context->reset_global_options(config);
    server_context->set_statistics(factory()->statistics());
    return server_context.release();
  }

  scoped_ptr<ThreadSystem> thread_system_;
  scoped_ptr<ServerContext> server_context_;
  scoped_ptr<SystemRewriteOptions> options_;
  scoped_ptr<AdminSite> admin_site_;
};

TEST_F(AdminSiteTest, StructuredMessagesInHistoryPage) {
  EXPECT_EQ(message_handler(), admin_site_->MessageHandlerForTesting());
  // Due to the size limit to the SharedCircularBuffer, the earliest message
  // in the buffer may be incomplete. In order to always display complete
  // messages on the history page, we simply ignore all the things before the
  // first new line. So here we inject a useless line at the beginning to show
  // that we throw out the first (possibly) incomplete line.
  message_handler()->Message(kInfo, "Ignore the first line.");
  message_handler()->Message(kError, "Test for %s", "Errors");
  message_handler()->Message(kWarning, "Test for %s", "Warnings");
  message_handler()->Message(kInfo, "Test for %s", "Infos");
  GoogleString buffer;
  StringAsyncFetch fetch(rewrite_driver()->request_context(), &buffer);
  // The value of the first argument AdminSite::AdminSource
  // does not matter in this test. So we just test for kPageSpeedAdmin here.
  admin_site_->MessageHistoryHandler(*(rewrite_driver()->options()),
                                     AdminSite::kPageSpeedAdmin, &fetch);
  EXPECT_THAT(buffer, ::testing::HasSubstr(
                          "class='pagespeed-message-entry' data-severity='error'"));
  EXPECT_THAT(buffer, ::testing::HasSubstr(
                          "class='pagespeed-message-entry' data-severity='warning'"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("Test for Errors"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("Test for Warnings"));
}

TEST_F(AdminSiteTest, ArchiveReadDoesNotRequireLiveSnapshot) {
  char directory[] = "/tmp/pagespeed-admin-restart-XXXXXX";
  ASSERT_NE(nullptr, mkdtemp(directory));
  GoogleString path(directory);
  EXPECT_EQ(0, mkdir((path + "/history").c_str(), 0700));
  const GoogleString payload =
      "I[Mon, 05 Apr 2010 18:51:26 GMT] [Info] [987] "
      "Script https://archive.example/no-live.js didn't shrink.\n";
  const GoogleString header = StrCat("PSH1 1 123456789 0 ",
      Integer64ToString(payload.size()),
      " 1270493486000 0 0 1 0 0 1270493486000 1 0 0 1 0\n");
  { std::ofstream file(path + "/history/checkpoint"); file << header; }
  { std::ofstream file(path + "/history/segment-00000000000000000001.psh");
    file << header << payload; }
  SystemRewriteOptions archive_options(thread_system_.get());
  archive_options.set_file_cache_path(path);
  GoogleString error;
  EXPECT_EQ(RewriteOptions::kOptionOk, archive_options.ParseAndSetOptionFromName1(
      "HistoryArchive", "on", &error, message_handler()));
  EXPECT_EQ(nullptr, message_handler()->history_buffer());
  QueryParams query;
  GoogleString body;
  StringAsyncFetch fetch(rewrite_driver()->request_context(), &body);
  admin_site_->AdminPage(true,
      GoogleUrl("http://localhost/pagespeed_admin/javascript"), query,
      rewrite_driver()->options(), nullptr, &fetch, nullptr, nullptr, nullptr,
      nullptr, nullptr, server_context_.get(), factory()->statistics(),
      factory()->statistics(), &archive_options);
  EXPECT_THAT(body, ::testing::HasSubstr("https://archive.example/no-live.js"));
  EXPECT_THAT(body, ::testing::Not(::testing::HasSubstr("<section class='pagespeed-history-controls'>")));
  EXPECT_THAT(body, ::testing::Not(::testing::HasSubstr("Browse history")));
  unlink((path + "/history/checkpoint").c_str());
  unlink((path + "/history/segment-00000000000000000001.psh").c_str());
  rmdir((path + "/history").c_str());
  rmdir(path.c_str());
}

TEST_F(AdminSiteTest, DashboardShowsSavingsAndUptime) {
  message_handler()->Message(
      kInfo, "PS_IMAGE_DETAIL\t%lld\t%u\t%u\t%s\t%s\t%s",
      static_cast<long long>(timer()->NowMs()), 10000u, 4000u, "image/webp",
      "https://example.com/recent.jpg",
      "https://example.com/recent.jpg.pagespeed.ic.abc.webp");
  GoogleString buffer;
  StringAsyncFetch fetch(rewrite_driver()->request_context(), &buffer);
  admin_site_->DashboardHandler(AdminSite::kPageSpeedAdmin, &fetch,
                                factory()->statistics());
  EXPECT_THAT(buffer, ::testing::HasSubstr("Performance dashboard"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("TOTAL DATA SAVED"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("PAGESPEED UPTIME"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("JavaScript</span>"));
  EXPECT_THAT(buffer, ::testing::HasSubstr(">CSS</span>"));
  EXPECT_THAT(buffer,
              ::testing::HasSubstr("IMAGE REWRITES"));
  EXPECT_THAT(buffer,
              ::testing::HasSubstr("Inspect recent optimized images"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("Savings by resource"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("Rewrite activity"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("Purge cache"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("cache?purge=*"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("pagespeed-purge-status"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("Recent image optimization"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("https://example.com/recent.jpg"));
  EXPECT_THAT(buffer,
              ::testing::HasSubstr("pagespeed-dashboard-stack-image"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("pagespeed-admin-sidebar"));
}

TEST_F(AdminSiteTest, DashboardShowsHtmlActivityWithoutInventingSavings) {
  Statistics* stats = factory()->statistics();
  stats->GetVariable("html_cache_inserts")->Add(4);
  stats->GetVariable("html_cache_bytes_served")->Add(4096);
  GoogleString buffer;
  StringAsyncFetch fetch(rewrite_driver()->request_context(), &buffer);
  admin_site_->DashboardHandler(AdminSite::kPageSpeedAdmin, &fetch, stats);
  EXPECT_THAT(buffer, ::testing::HasSubstr("<span>HTML</span><strong>Not measured</strong>"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("Cached HTML served: 4.0 KB"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("<strong>HTML</strong><span>4 publications</span>"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("pagespeed-dashboard-activity-html"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("activity-track'><i style='width:100%"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("pagespeed-dashboard-total-value'>0 B</div>"));
  EXPECT_THAT(buffer, ::testing::Not(::testing::HasSubstr("HTML CACHE HITS")));
}

TEST_F(AdminSiteTest, ImagesPageShowsRecordedRewriteAndComparison) {
  message_handler()->Message(
      kInfo,
      "PS_IMAGE_DETAIL\t%lld\t%u\t%u\t%s\t%s\t%s",
      static_cast<long long>(timer()->NowMs()), 10000u, 4000u, "image/webp",
      "https://example.com/photo.jpg",
      "https://example.com/photo.jpg.pagespeed.ic.abc.webp");
  GoogleString buffer;
  StringAsyncFetch fetch(rewrite_driver()->request_context(), &buffer);
  admin_site_->ImagesHandler(AdminSite::kPageSpeedAdmin, &fetch);
  EXPECT_THAT(buffer, ::testing::HasSubstr("Recently optimized images"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("https://example.com/photo.jpg"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("image/webp"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("5.8 KB"));
  EXPECT_THAT(buffer,
              ::testing::HasSubstr("pagespeed-image-preview-button"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("pagespeed-image-modal"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("Image comparison"));
}
// TODO(xqyin): Add unit tests for other methods in AdminSite.

TEST_F(AdminSiteTest, HtmlCacheMetricsStayOnDedicatedPage) {
  factory()->statistics()->GetVariable("html_cache_hits")->Add(3);
  factory()->statistics()->GetVariable("html_cache_misses")->Add(1);
  GoogleString buffer;
  StringAsyncFetch fetch(rewrite_driver()->request_context(), &buffer);
  admin_site_->HtmlCacheHandler(AdminSite::kPageSpeedAdmin, &fetch,
      factory()->statistics(), *rewrite_driver()->options());
  EXPECT_THAT(buffer, ::testing::HasSubstr("No HTML rewrites recorded"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("Hit rate: 75%"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("HTML cache: <strong>Disabled"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("not a live cache inventory"));
  GoogleString dashboard;
  StringAsyncFetch dashboard_fetch(rewrite_driver()->request_context(),
                                   &dashboard);
  admin_site_->DashboardHandler(AdminSite::kPageSpeedAdmin, &dashboard_fetch,
                                 factory()->statistics());
  EXPECT_THAT(dashboard, ::testing::HasSubstr("href='html_cache'"));
  EXPECT_THAT(dashboard, ::testing::Not(::testing::HasSubstr("HTML CACHE HITS")));
  EXPECT_THAT(dashboard, ::testing::Not(::testing::HasSubstr("<h2>HTML rewrite cache</h2>")));
  EXPECT_THAT(dashboard, ::testing::HasSubstr("Savings by resource"));
}

TEST_F(AdminSiteTest, HtmlCacheRecordsAreEscapedAndDeduplicated) {
  GoogleString encoded_url;
  Web64Encode("https://example.com/blog/?x=1&y=<script>", &encoded_url);
  for (int bytes : {1000, 2000}) {
    message_handler()->Message(kInfo,
        "PS_HTML_DETAIL\t1000\t5000\t%d\tmobile\tWebP capable\t"
        "optimized-html-v1:abcdefghijklmnopqrst\t%s", bytes,
        encoded_url.c_str());
  }
  GoogleString buffer;
  StringAsyncFetch fetch(rewrite_driver()->request_context(), &buffer);
  admin_site_->HtmlCacheHandler(AdminSite::kPageSpeedAdmin, &fetch,
      factory()->statistics(), *rewrite_driver()->options());
  EXPECT_THAT(buffer, ::testing::HasSubstr("data-device='mobile'"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("WebP capable"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("1.9 KB"));
  EXPECT_THAT(buffer, ::testing::Not(::testing::HasSubstr("&y=<script>")));
  EXPECT_THAT(buffer, ::testing::HasSubstr("&amp;y=&lt;script&gt;"));
  size_t first = buffer.find("<tr data-device=");
  ASSERT_NE(GoogleString::npos, first);
  EXPECT_EQ(GoogleString::npos, buffer.find("<tr data-device=", first + 1));
  EXPECT_THAT(buffer, ::testing::HasSubstr("pagespeed-html-search"));
  EXPECT_THAT(buffer, ::testing::HasSubstr("pagespeed-html-format"));
}

TEST_F(AdminSiteTest, HtmlCacheRejectsMalformedAndNonWebRecords) {
  GoogleString encoded_url;
  Web64Encode("javascript:alert(1)", &encoded_url);
  message_handler()->Message(kInfo,
      "PS_HTML_DETAIL\t1000\t5000\t100\tdesktop\tAVIF capable\t"
      "optimized-html-v1:abcdefghijklmnopqrst\t%s", encoded_url.c_str());
  message_handler()->Message(kInfo, "PS_HTML_DETAIL\tbroken");
  message_handler()->Message(kInfo,
      "PS_HTML_DETAIL\t1000\t500\t-1\tdesktop\tAVIF capable\t"
      "optimized-html-v1:abcdefghijklmnopqrst\tbad-base64");
  GoogleString buffer;
  StringAsyncFetch fetch(rewrite_driver()->request_context(), &buffer);
  admin_site_->HtmlCacheHandler(AdminSite::kPageSpeedAdmin, &fetch,
      factory()->statistics(), *rewrite_driver()->options());
  EXPECT_THAT(buffer, ::testing::HasSubstr("No HTML rewrites recorded"));
  EXPECT_THAT(buffer, ::testing::Not(::testing::HasSubstr("javascript:alert")));
}

TEST_F(AdminSiteTest, HtmlCacheHistoryIsBoundedAndNewestFirst) {
  GoogleString encoded_url;
  Web64Encode("https://example.com/blog/", &encoded_url);
  for (int i = 0; i < 105; ++i) {
    message_handler()->Message(kInfo,
        "PS_HTML_DETAIL\t1000\t5000\t100\tdesktop\tAVIF capable\t"
        "optimized-html-v1:variant-%03d\t%s", i, encoded_url.c_str());
  }
  GoogleString buffer;
  StringAsyncFetch fetch(rewrite_driver()->request_context(), &buffer);
  admin_site_->HtmlCacheHandler(AdminSite::kPageSpeedAdmin, &fetch,
      factory()->statistics(), *rewrite_driver()->options());
  size_t position = 0;
  int rows = 0;
  while ((position = buffer.find("<tr data-device=", position)) !=
         GoogleString::npos) {
    ++rows;
    ++position;
  }
  EXPECT_EQ(100, rows);
  EXPECT_LT(buffer.find("optimized-html-v1:variant-104"),
            buffer.find("optimized-html-v1:variant-005"));
  EXPECT_EQ(GoogleString::npos,
            buffer.find("optimized-html-v1:variant-004"));
}

TEST_F(AdminSiteTest, ExistingDiagnosticsAreFilteredWithoutInventingRewriteFields) {
  message_handler()->Message(kInfo, "Script https://diagnostic.example/fail.js didn't shrink.");
  message_handler()->Message(kWarning, "CSS parsing error in https://diagnostic.example/fail.css");
  GoogleString buffer;
  StringAsyncFetch fetch(rewrite_driver()->request_context(), &buffer);
  admin_site_->MessageHistoryHandler(*rewrite_driver()->options(),
      AdminSite::kPageSpeedAdmin, &fetch, nullptr, "", "javascript");
  EXPECT_THAT(buffer, ::testing::HasSubstr("https://diagnostic.example/fail.js"));
  EXPECT_THAT(buffer, ::testing::Not(::testing::HasSubstr("https://diagnostic.example/fail.css")));
  EXPECT_THAT(buffer, ::testing::HasSubstr("not a complete per-resource rewrite history"));
}

TEST_F(AdminSiteTest, DeleteRefusedWhenPurgeDisabled) {
  // Cache purging is disabled by default, so a cache-entry delete is refused
  // rather than performed.
  ASSERT_FALSE(rewrite_driver()->options()->enable_cache_purge());
  GoogleUrl gurl("http://localhost/pagespeed_admin/cache");
  QueryParams query_params;
  query_params.ParseFromUrl(GoogleUrl(
      "http://localhost/pagespeed_admin/cache"
      "?url=http://example.com/x.css&Delete"));
  GoogleString body;
  StringAsyncFetch fetch(rewrite_driver()->request_context(), &body);
  SystemRewriteOptions global_options(thread_system_.get());
  admin_site_->AdminPage(false, gurl, query_params, rewrite_driver()->options(),
                         nullptr, &fetch, nullptr, nullptr, nullptr, nullptr,
                         nullptr, server_context_.get(), factory()->statistics(),
                         factory()->statistics(), &global_options);
  EXPECT_EQ(HttpStatus::kForbidden, fetch.response_headers()->status_code());
  EXPECT_THAT(body, ::testing::HasSubstr("Deleting not enabled"));
  EXPECT_THAT(body, ::testing::HasSubstr("EnableCachePurge"));
}

class GraphQueryTest : public ::testing::Test {
 protected:
  static const int64 kNowMs = 1700000000000LL;

  GraphQuery Parse(StringPiece query) {
    GoogleUrl gurl(
        StrCat("http://example.com/pagespeed_admin/console?", query));
    QueryParams params;
    params.ParseFromUrl(gurl);
    return ParseGraphQuery(params, kNowMs);
  }
};

const int64 GraphQueryTest::kNowMs;

TEST_F(GraphQueryTest, DefaultsAskForTheGraphsOverTheWholeLog) {
  const GraphQuery q = Parse("json=1");
  EXPECT_EQ(0, q.start_time);
  EXPECT_EQ(kNowMs, q.end_time);
  EXPECT_EQ(3000, q.granularity_ms);
  EXPECT_TRUE(q.dump_for_graphs);
  EXPECT_TRUE(q.var_titles.empty());
}

TEST_F(GraphQueryTest, GranularityIsHeldToTheBounds) {
  EXPECT_EQ(kMinGraphGranularityMs, Parse("granularity=0").granularity_ms);
  EXPECT_EQ(kMinGraphGranularityMs, Parse("granularity=1").granularity_ms);
  EXPECT_EQ(kMinGraphGranularityMs, Parse("granularity=-5").granularity_ms);
  EXPECT_EQ(kMinGraphGranularityMs, Parse("granularity=999").granularity_ms);
  EXPECT_EQ(1000, Parse("granularity=1000").granularity_ms);
  EXPECT_EQ(60000, Parse("granularity=60000").granularity_ms);
  EXPECT_EQ(kMaxGraphGranularityMs,
            Parse("granularity=999999999999999").granularity_ms);
  // Not a number reads as 0, which is held to the lower bound.
  EXPECT_EQ(kMinGraphGranularityMs, Parse("granularity=abc").granularity_ms);
}

TEST_F(GraphQueryTest, TimesAreNonNegativeAndNotFarInTheFuture) {
  EXPECT_EQ(0, Parse("start_time=-100").start_time);
  EXPECT_EQ(0, Parse("end_time=-100").end_time);
  EXPECT_EQ(12345, Parse("start_time=12345").start_time);
  EXPECT_EQ(kNowMs + 86400000, Parse("end_time=99999999999999999").end_time);
  EXPECT_EQ(kNowMs + 86400000,
            Parse("start_time=99999999999999999").start_time);
  EXPECT_EQ(kNowMs - 1000, Parse("end_time=1699999999000").end_time);
}

TEST_F(GraphQueryTest, VariableNamesAreListedAndBounded) {
  const GraphQuery named = Parse("var_titles=a,b,,c");
  EXPECT_FALSE(named.dump_for_graphs);
  EXPECT_EQ(3u, named.var_titles.size());
  GoogleString many = "var_titles=";
  for (size_t i = 0; i < 3 * kMaxGraphVarTitles; ++i) {
    StrAppend(&many, i == 0 ? "" : ",", "v", IntegerToString(i));
  }
  const GraphQuery capped = Parse(many);
  EXPECT_FALSE(capped.dump_for_graphs);
  EXPECT_EQ(kMaxGraphVarTitles, capped.var_titles.size());
  // Several var_titles parameters share the same bound.
  GoogleString repeated;
  for (size_t i = 0; i < 3 * kMaxGraphVarTitles; ++i) {
    StrAppend(&repeated, i == 0 ? "" : "&", "var_titles=w", IntegerToString(i));
  }
  EXPECT_EQ(kMaxGraphVarTitles, Parse(repeated).var_titles.size());
}

class AdminSiteExpensiveReadsTest : public AdminSiteTest {
 protected:
  // Fetches an admin leaf through AdminPage; returns the status code.
  int FetchLeaf(StringPiece path, StringPiece query, GoogleString* body,
                bool is_global = false) {
    GoogleUrl gurl(StrCat("http://localhost", path));
    QueryParams query_params;
    if (!query.empty()) {
      query_params.ParseFromUntrustedString(query);
    }
    StringAsyncFetch fetch(rewrite_driver()->request_context(), body);
    SystemRewriteOptions global_options(thread_system_.get());
    admin_site_->AdminPage(is_global, gurl, query_params,
                           rewrite_driver()->options(), nullptr, &fetch,
                           nullptr, nullptr, nullptr, nullptr, nullptr,
                           server_context(), factory()->statistics(),
                           factory()->statistics(), &global_options);
    return fetch.response_headers()->status_code();
  }
};

TEST_F(AdminSiteExpensiveReadsTest, ConfigAdmitsOneReadAtATime) {
  GoogleString body;
  EXPECT_EQ(HttpStatus::kOK,
            FetchLeaf("/pagespeed_admin/config", "", &body));
  admin_site_->SetExpensiveSlotBusyForTesting(kConfig, true);
  EXPECT_EQ(429, FetchLeaf("/pagespeed_admin/config", "", &body));
  EXPECT_THAT(body, ::testing::HasSubstr("busy"));
  admin_site_->SetExpensiveSlotBusyForTesting(kConfig, false);
  EXPECT_EQ(HttpStatus::kOK,
            FetchLeaf("/pagespeed_admin/config", "", &body));
}

TEST_F(AdminSiteExpensiveReadsTest, MessageHistoryAdmitsOneReadAtATime) {
  GoogleString body;
  EXPECT_EQ(HttpStatus::kOK,
            FetchLeaf("/pagespeed_admin/message_history", "", &body));
  admin_site_->SetExpensiveSlotBusyForTesting(kMessages, true);
  EXPECT_EQ(429, FetchLeaf("/pagespeed_admin/message_history", "", &body));
  admin_site_->SetExpensiveSlotBusyForTesting(kMessages, false);
}

TEST_F(AdminSiteExpensiveReadsTest, GraphsJsonAdmitsOneReadAtATime) {
  // No console logger in the fixture, so an admitted read answers 404;
  // a refused one answers 429 either way.
  GoogleString body;
  EXPECT_EQ(HttpStatus::kNotFound,
            FetchLeaf("/pagespeed_admin/graphs", "json=1", &body));
  admin_site_->SetExpensiveSlotBusyForTesting(kGraphs, true);
  EXPECT_EQ(429, FetchLeaf("/pagespeed_admin/graphs", "json=1", &body));
  admin_site_->SetExpensiveSlotBusyForTesting(kGraphs, false);
}

TEST_F(AdminSiteExpensiveReadsTest, CacheLookupAdmitsOneReadAtATime) {
  admin_site_->SetExpensiveSlotBusyForTesting(kCacheLookup, true);
  GoogleString body;
  EXPECT_EQ(429, FetchLeaf("/pagespeed_admin/cache",
                           "url=http://example.com/x.css", &body));
  EXPECT_THAT(body, ::testing::HasSubstr("busy"));
  admin_site_->SetExpensiveSlotBusyForTesting(kCacheLookup, false);
}

TEST_F(AdminSiteExpensiveReadsTest, ACompletedCacheLookupFreesItsSlot) {
  // A lookup that runs to completion releases its slot: the next one is
  // admitted, not answered 429.
  GoogleString first;
  EXPECT_NE(429, FetchLeaf("/pagespeed_admin/cache",
                           "url=http://example.com/x.css", &first));
  GoogleString second;
  EXPECT_NE(429, FetchLeaf("/pagespeed_admin/cache",
                           "url=http://example.com/y.css", &second));
}

TEST_F(AdminSiteExpensiveReadsTest, CacheStatusAdmitsOneReadAtATime) {
  GoogleString body;
  EXPECT_EQ(HttpStatus::kOK,
            FetchLeaf("/pagespeed_admin/cache", "", &body, true));
  admin_site_->SetExpensiveSlotBusyForTesting(kCacheStatus, true);
  EXPECT_EQ(429, FetchLeaf("/pagespeed_admin/cache", "", &body, true));
  // The per-vhost listing takes no slot.
  EXPECT_EQ(HttpStatus::kOK,
            FetchLeaf("/pagespeed_admin/cache", "", &body, false));
  admin_site_->SetExpensiveSlotBusyForTesting(kCacheStatus, false);
}

TEST_F(AdminSiteExpensiveReadsTest, LeavesDoNotBlockEachOther) {
  admin_site_->SetExpensiveSlotBusyForTesting(kConfig, true);
  GoogleString body;
  EXPECT_EQ(HttpStatus::kOK,
            FetchLeaf("/pagespeed_admin/message_history", "", &body));
  admin_site_->SetExpensiveSlotBusyForTesting(kConfig, false);
}

TEST_F(AdminSiteExpensiveReadsTest, ABusySlotIsWaitedForBriefly) {
  admin_site_->SetExpensiveSlotBusyForTesting(kConfig, true);
  std::thread releaser([this]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    admin_site_->ReleaseExpensiveSlot(kConfig);
  });
  GoogleString body;
  EXPECT_EQ(HttpStatus::kOK,
            FetchLeaf("/pagespeed_admin/config", "", &body));
  releaser.join();
}

TEST_F(AdminSiteTest, DeleteAllowedWhenPurgeEnabled) {
  options()->set_enable_cache_purge(true);
  GoogleUrl gurl("http://localhost/pagespeed_admin/cache");
  QueryParams query_params;
  query_params.ParseFromUrl(GoogleUrl(
      "http://localhost/pagespeed_admin/cache"
      "?url=http://example.com/x.css&Delete"));
  GoogleString body;
  StringAsyncFetch fetch(rewrite_driver()->request_context(), &body);
  SystemRewriteOptions global_options(thread_system_.get());
  // The fully wired fixture context: the delete runs a real metadata lookup.
  admin_site_->AdminPage(false, gurl, query_params, rewrite_driver()->options(),
                         nullptr, &fetch, nullptr, nullptr, nullptr, nullptr,
                         nullptr, server_context(), factory()->statistics(),
                         factory()->statistics(), &global_options);
  EXPECT_NE(HttpStatus::kForbidden, fetch.response_headers()->status_code());
  EXPECT_THAT(body, ::testing::Not(::testing::HasSubstr("Deleting not enabled")));
}
}  // namespace

}  // namespace net_instaweb
