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

  // Extracts href="..." from the first auto preload link in output_.
  GoogleString FirstPreloadHref() {
    StringPiece out(output_);
    StringPiece::size_type pos = out.find("<link rel=\"preload\"");
    EXPECT_NE(StringPiece::npos, pos);
    if (pos == StringPiece::npos) {
      return "";
    }
    static const char kHref[] = "href=\"";
    StringPiece::size_type start = out.find(kHref, pos) + sizeof(kHref) - 1;
    StringPiece::size_type end = out.find("\"", start);
    return out.substr(start, end - start).as_string();
  }

  // Extracts src="..." from the first img in output_.
  GoogleString FirstImgSrc() {
    StringPiece out(output_);
    StringPiece::size_type pos = out.find("<img");
    EXPECT_NE(StringPiece::npos, pos);
    if (pos == StringPiece::npos) {
      return "";
    }
    static const char kSrc[] = "src=\"";
    StringPiece::size_type start = out.find(kSrc, pos) + sizeof(kSrc) - 1;
    StringPiece::size_type end = out.find("\"", start);
    return out.substr(start, end - start).as_string();
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

// The beacon reports original-URL hashes but this filter runs post-rewrite:
// the stamped original hash must match evidence even when the current src
// is rewritten (WG-07). Without this the learned hero is left unpreloaded
// and lazy-loaded once image rewriting kicks in.
TEST_F(PrioritizeLcpImagesFilterTest, LearnedEvidenceMatchesBeaconHashOnRewrittenSrc) {
  SetLearnedCriticalImages("http://test.com/hero.jpg");
  const GoogleString input =
      "<head></head><body>"
      "<img src=\"/logo.png\" width=\"120\" height=\"40\">"
      "<img src=\"/hero.jpg.pagespeed.ic.HASH.webp\" "
      "data-pagespeed-url-hash=\"http://test.com/hero.jpg\">"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" "
      "href=\"/hero.jpg.pagespeed.ic.HASH.webp\" "
      "fetchpriority=\"high\"></head><body>"
      "<img src=\"/logo.png\" width=\"120\" height=\"40\">"
      "<img src=\"/hero.jpg.pagespeed.ic.HASH.webp\" "
      "data-pagespeed-url-hash=\"http://test.com/hero.jpg\" "
      "fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "</body>";
  Parse("learned_evidence_matches_beacon_hash", input);
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
  // WG-04 contract change: the old expectation preloaded the img fallback
  // src, but supporting browsers fetch the <source> candidate instead, so
  // that preload could never match. The preload now mirrors the source.
  const GoogleString input =
      "<head></head><body>"
      "<picture><source srcset=\"/hero.webp\"><img src=\"/hero.jpg\" "
      "loading=\"lazy\"></picture>"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\" "
      "fetchpriority=\"high\" imagesrcset=\"/hero.webp\"></head><body>"
      "<picture><source srcset=\"/hero.webp\">"
      "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "</picture>"
      "</body>";
  Parse("prioritize_picture_img", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

// The preload must name the final rewritten URL, not the pre-rewrite one:
// with cache extension on, both must carry the fingerprinted URL.
class PrioritizeLcpImagesExtendCacheTest
    : public PrioritizeLcpImagesFilterTest {
 protected:
  void SetUp() override {
    options()->EnableFilter(RewriteOptions::kExtendCacheImages);
    PrioritizeLcpImagesFilterTest::SetUp();
  }
};

TEST_F(PrioritizeLcpImagesExtendCacheTest, PreloadMatchesRewrittenHeroUrl) {
  AddFileToMockFetcher(StrCat(kTestDomain, "hero.jpg"), "Puzzle.jpg",
                       kContentTypeJpeg, 100);
  SetFetchResponse404("below.jpg");
  Parse("preload_matches_rewritten",
        "<head></head><body>"
        "<img src=\"/hero.jpg\">"
        "<img src=\"/below.jpg\">"
        "</body>");
  EXPECT_EQ(FirstImgSrc(), FirstPreloadHref()) << output_;
  EXPECT_NE(GoogleString::npos, FirstPreloadHref().find(".pagespeed."))
      << output_;
}

// A responsive hero preloads via imagesrcset/imagesizes copied from the img,
// so the browser picks the same candidate; href stays as fallback.
TEST_F(PrioritizeLcpImagesFilterTest, SrcsetHeroGetsImagesrcsetPreload) {
  const GoogleString input =
      "<head></head><body>"
      "<img src=\"/hero.jpg\" "
      "srcset=\"/hero-480.jpg 480w, /hero-800.jpg 800w\" "
      "sizes=\"(max-width: 600px) 480px, 800px\">"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\" "
      "fetchpriority=\"high\" "
      "imagesrcset=\"/hero-480.jpg 480w, /hero-800.jpg 800w\" "
      "imagesizes=\"(max-width: 600px) 480px, 800px\"></head><body>"
      "<img src=\"/hero.jpg\" "
      "srcset=\"/hero-480.jpg 480w, /hero-800.jpg 800w\" "
      "sizes=\"(max-width: 600px) 480px, 800px\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "</body>";
  Parse("srcset_imagesrcset", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

// A single usable <source> (format switching) mirrors onto the preload,
// including media/type gating and the img's sizes.
TEST_F(PrioritizeLcpImagesFilterTest, SingleSourcePictureGetsImagesrcset) {
  const GoogleString input =
      "<head></head><body>"
      "<picture>"
      "<source media=\"(min-width: 800px)\" srcset=\"/hero-large.jpg\" "
      "type=\"image/jpeg\">"
      "<img src=\"/hero.jpg\" sizes=\"(min-width: 800px) 800px, 100vw\">"
      "</picture>"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\" "
      "fetchpriority=\"high\" imagesrcset=\"/hero-large.jpg\" "
      "imagesizes=\"(min-width: 800px) 800px, 100vw\" "
      "media=\"(min-width: 800px)\" type=\"image/jpeg\"></head><body>"
      "<picture>"
      "<source media=\"(min-width: 800px)\" srcset=\"/hero-large.jpg\" "
      "type=\"image/jpeg\">"
      "<img src=\"/hero.jpg\" sizes=\"(min-width: 800px) 800px, 100vw\" "
      "fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "</picture>"
      "</body>";
  Parse("single_source_mirror", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

// Multiple usable sources need viewport-dependent selection a single hint
// cannot express: no preload, but the hero keeps its element priority.
TEST_F(PrioritizeLcpImagesFilterTest, MultiSourcePictureSkipsPreload) {
  const GoogleString input =
      "<head></head><body>"
      "<picture>"
      "<source media=\"(min-width: 800px)\" srcset=\"/large.jpg\">"
      "<source srcset=\"/small.jpg\">"
      "<img src=\"/hero.jpg\">"
      "</picture>"
      "</body>";
  const GoogleString expected =
      "<head></head><body>"
      "<picture>"
      "<source media=\"(min-width: 800px)\" srcset=\"/large.jpg\">"
      "<source srcset=\"/small.jpg\">"
      "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "</picture>"
      "</body>";
  Parse("multi_source_skip", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

// A <source> without srcset contributes nothing: fall back to the img src.
TEST_F(PrioritizeLcpImagesFilterTest, SourcelessPicturePreloadsImgSrc) {
  const GoogleString input =
      "<head></head><body>"
      "<picture><source media=\"(min-width: 800px)\">"
      "<img src=\"/hero.jpg\"></picture>"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\" "
      "fetchpriority=\"high\"></head><body>"
      "<picture><source media=\"(min-width: 800px)\">"
      "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "</picture>"
      "</body>";
  Parse("sourceless_picture", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

// An author preload for the hero suppresses the automatic duplicate.
TEST_F(PrioritizeLcpImagesFilterTest, AuthorPreloadDeduped) {
  const GoogleString input =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\"></head>"
      "<body>"
      "<img src=\"/hero.jpg\">"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\"></head>"
      "<body>"
      "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "</body>";
  Parse("author_preload_dedup", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

// Dedup compares absolutized URLs, not raw strings.
TEST_F(PrioritizeLcpImagesFilterTest, AuthorPreloadAbsoluteUrlDeduped) {
  const GoogleString input =
      "<head><link rel=\"preload\" as=\"image\" "
      "href=\"http://test.com/hero.jpg\"></head>"
      "<body>"
      "<img src=\"/hero.jpg\">"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" "
      "href=\"http://test.com/hero.jpg\"></head>"
      "<body>"
      "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "</body>";
  Parse("author_preload_absolute_dedup", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

// Author preloads via Link response headers dedup the same way.
TEST_F(PrioritizeLcpImagesFilterTest, AuthorPreloadLinkHeaderDeduped) {
  response_headers_.Add(
      "Link", "</hero.jpg>; rel=\"preload\"; as=\"image\"; "
      "fetchpriority=\"high\"");
  SetHtmlMimetype();
  Parse("author_link_header_dedup",
        "<head></head><body>"
        "<img src=\"/hero.jpg\">"
        "</body>");
  EXPECT_EQ(AddHtmlBody(
                "<head></head><body>"
                "<img src=\"/hero.jpg\" fetchpriority=\"high\" "
                "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
                "</body>"),
            output_);
}

// Mismatched credentials would double-fetch: the img's crossorigin mode is
// copied onto the preload.
TEST_F(PrioritizeLcpImagesFilterTest, CrossoriginCopiedToPreload) {
  const GoogleString input =
      "<head></head><body>"
      "<img crossorigin=\"anonymous\" src=\"/hero.jpg\">"
      "</body>";
  const GoogleString expected =
      "<head><link rel=\"preload\" as=\"image\" href=\"/hero.jpg\" "
      "fetchpriority=\"high\" crossorigin=\"anonymous\"></head><body>"
      "<img crossorigin=\"anonymous\" src=\"/hero.jpg\" "
      "fetchpriority=\"high\" "
      "data-pagespeed-no-defer=\"1\" pagespeed_no_defer=\"1\">"
      "</body>";
  Parse("crossorigin_copied", input);
  EXPECT_EQ(AddHtmlBody(expected), output_);
}

}  // namespace net_instaweb
