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

TEST_F(AdminSiteTest, HtmlCacheEmptyPageAndDashboardMetrics) {
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
  EXPECT_THAT(dashboard, ::testing::HasSubstr("HTML CACHE HITS"));
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

}  // namespace

}  // namespace net_instaweb
