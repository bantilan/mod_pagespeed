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

#include "net/instaweb/rewriter/public/critical_images_finder_test_base.h"
#include "net/instaweb/rewriter/public/mock_critical_images_finder.h"
#include "net/instaweb/rewriter/public/rewrite_options.h"
#include "net/instaweb/rewriter/public/rewrite_test_base.h"
#include "pagespeed/kernel/base/gtest.h"
#include "pagespeed/kernel/base/string.h"
#include "pagespeed/kernel/base/string_writer.h"

namespace net_instaweb {

class PrioritizeLcpImagesFilterTest : public RewriteTestBase {
 public:
  PrioritizeLcpImagesFilterTest() : writer_(&output_) {}

 protected:
  void SetUp() override {
    options()->EnableFilter(RewriteOptions::kPrioritizeLcpImages);
    RewriteTestBase::SetUp();
    rewrite_driver()->AddFilters();
    rewrite_driver()->SetWriter(&writer_);
  }

  // Installs learned LCP/critical-image evidence (absolute URLs, as the
  // mock finder uses identity keys) and reports it available.
  void SetLearnedCriticalImages(const GoogleString& url1,
                                const GoogleString& url2 = "") {
    MockCriticalImagesFinder* finder =
        new MockCriticalImagesFinder(statistics());
    StringSet* learned = new StringSet();
    if (!url1.empty()) {
      learned->insert(url1);
    }
    if (!url2.empty()) {
      learned->insert(url2);
    }
    finder->set_critical_images(learned);
    server_context()->set_critical_images_finder(finder);
  }

  // Reports a beacon-learning state with no usable evidence.
  void SetCriticalImagesAvailability(CriticalImagesFinder::Availability state) {
    TestCriticalImagesFinder* finder =
        new TestCriticalImagesFinder(NULL, statistics());
    finder->set_available(state);
    server_context()->set_critical_images_finder(finder);
  }

  GoogleString output_;

 private:
  StringWriter writer_;
};

TEST_F(PrioritizeLcpImagesFilterTest, PrioritizesFirstImage) {
  const GoogleString input =
      "<head></head><body>"
      "<img loading=\"lazy\" src=\"/hero.jpg\">"
      "<img loading=\"lazy\" src=\"/next.jpg\">"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\" "
      "fetchpriority=\"high\"></head><body>"
      "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "<img loading=\"lazy\" src=\"/next.jpg\">"
      "</body>";
  Parse("prioritize_first_image", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

TEST_F(PrioritizeLcpImagesFilterTest, AddsNativeLazyLoadingToLaterImages) {
  const GoogleString input =
      "<head></head><body>"
      "<img src=\"/hero.jpg\">"
      "<img src=\"/below-fold.jpg\">"
      "<img loading=\"eager\" src=\"/keep-eager.jpg\">"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\" "
      "fetchpriority=\"high\"></head><body>"
      "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "<img src=\"/below-fold.jpg\" loading=\"lazy\">"
      "<img loading=\"eager\" src=\"/keep-eager.jpg\">"
      "</body>";
  Parse("add_native_lazy_to_later_images", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

// Learned LCP evidence beats document position: the logo must not steal the
// single preload, and the learned hero must not be lazy-loaded.
TEST_F(PrioritizeLcpImagesFilterTest, LearnedLcpBeatsFirstImage) {
  SetLearnedCriticalImages("http://test.com/hero.jpg");
  const GoogleString input =
      "<head></head><body>"
      "<img src=\"/logo.png\" width=\"120\" height=\"40\">"
      "<img src=\"/hero.jpg\">"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\" "
      "fetchpriority=\"high\"></head><body>"
      "<img src=\"/logo.png\" width=\"120\" height=\"40\">"
      "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "</body>";
  Parse("learned_lcp_beats_first_image", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

// Learned-critical images are never lazy-loaded, even past the hero slot.
TEST_F(PrioritizeLcpImagesFilterTest, LearnedCriticalImagesAreNeverLazyLoaded) {
  SetLearnedCriticalImages("http://test.com/hero.jpg",
                           "http://test.com/gallery.jpg");
  const GoogleString input =
      "<head></head><body>"
      "<img src=\"/hero.jpg\">"
      "<img src=\"/gallery.jpg\">"
      "<img src=\"/below.jpg\">"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\" "
      "fetchpriority=\"high\"></head><body>"
      "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "<img src=\"/gallery.jpg\">"
      "<img src=\"/below.jpg\" loading=\"lazy\">"
      "</body>";
  Parse("learned_critical_never_lazy", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

// Evidence that matches nothing (a changed page variant) must not produce a
// guess-preload: the first image is left alone instead of being assumed hero.
TEST_F(PrioritizeLcpImagesFilterTest, EvidenceMissLeavesFirstImageAlone) {
  SetLearnedCriticalImages("http://test.com/absent.jpg");
  const GoogleString input =
      "<head></head><body>"
      "<img src=\"/top.jpg\">"
      "<img src=\"/second.jpg\">"
      "</body>";
  const GoogleString expected =
      "<head></head><body>"
      "<img src=\"/top.jpg\">"
      "<img src=\"/second.jpg\" loading=\"lazy\">"
      "</body>";
  Parse("evidence_miss_leaves_first_alone", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

// While beacon learning has produced nothing yet, the first-image heuristic
// still applies.
TEST_F(PrioritizeLcpImagesFilterTest, NoDataKeepsFirstImageHeuristic) {
  SetCriticalImagesAvailability(CriticalImagesFinder::kNoDataYet);
  const GoogleString input =
      "<head></head><body>"
      "<img src=\"/hero.jpg\">"
      "<img src=\"/below-fold.jpg\">"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\" "
      "fetchpriority=\"high\"></head><body>"
      "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "<img src=\"/below-fold.jpg\" loading=\"lazy\">"
      "</body>";
  Parse("no_data_keeps_heuristic", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

// Hidden images are never LCP: the hidden attribute and whitespace-tolerant
// inline hiding must disqualify them as hero and as lazy candidates.
TEST_F(PrioritizeLcpImagesFilterTest, HiddenImagesAreSkipped) {
  const GoogleString input =
      "<head></head><body>"
      "<img hidden src=\"/hidden.jpg\">"
      "<img style=\"display: none\" src=\"/styled.jpg\">"
      "<img style=\"VISIBILITY : HIDDEN\" src=\"/caps.jpg\">"
      "<img src=\"/hero.jpg\">"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\" "
      "fetchpriority=\"high\"></head><body>"
      "<img hidden src=\"/hidden.jpg\">"
      "<img style=\"display: none\" src=\"/styled.jpg\">"
      "<img style=\"VISIBILITY : HIDDEN\" src=\"/caps.jpg\">"
      "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "</body>";
  Parse("hidden_images_skipped", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

// Hiding propagates from ancestors: images under hidden elements are skipped.
TEST_F(PrioritizeLcpImagesFilterTest, HiddenAncestorImagesAreSkipped) {
  const GoogleString input =
      "<head></head><body>"
      "<div hidden><img src=\"/menu.jpg\"></div>"
      "<div style=\"display:none\"><img src=\"/tab.jpg\"></div>"
      "<img src=\"/hero.jpg\">"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\" "
      "fetchpriority=\"high\"></head><body>"
      "<div hidden><img src=\"/menu.jpg\"></div>"
      "<div style=\"display:none\"><img src=\"/tab.jpg\"></div>"
      "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "</body>";
  Parse("hidden_ancestor_skipped", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

// An explicit author priority signal must survive: fetchpriority="high"
// images are never lazy-loaded.
TEST_F(PrioritizeLcpImagesFilterTest, FetchpriorityHighIsNeverLazyLoaded) {
  const GoogleString input =
      "<head></head><body>"
      "<img src=\"/hero.jpg\">"
      "<img fetchpriority=\"high\" src=\"/second.jpg\">"
      "<img src=\"/third.jpg\">"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\" "
      "fetchpriority=\"high\"></head><body>"
      "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "<img fetchpriority=\"high\" src=\"/second.jpg\">"
      "<img src=\"/third.jpg\" loading=\"lazy\">"
      "</body>";
  Parse("fetchpriority_high_never_lazy", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

// Trackers and empty sources stay ineligible, as before.
TEST_F(PrioritizeLcpImagesFilterTest, TrackersAndEmptySourcesAreSkipped) {
  const GoogleString input =
      "<head></head><body>"
      "<img width=\"1\" height=\"1\" src=\"/tracker.gif\">"
      "<img src=\"\">"
      "<img src=\"/hero.jpg\">"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\" "
      "fetchpriority=\"high\"></head><body>"
      "<img width=\"1\" height=\"1\" src=\"/tracker.gif\">"
      "<img src=\"\">"
      "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "</body>";
  Parse("trackers_skipped", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

TEST_F(PrioritizeLcpImagesFilterTest, PrioritizesImgInsidePicture) {
  const GoogleString input =
      "<head></head><body>"
      "<picture><source srcset=\"/hero.webp\"><img src=\"/hero.jpg\" "
      "loading=\"lazy\"></picture>"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\" "
      "fetchpriority=\"high\"></head><body>"
      "<picture><source srcset=\"/hero.webp\">"
      "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "</picture>"
      "</body>";
  Parse("prioritize_picture_img", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

}  // namespace net_instaweb
