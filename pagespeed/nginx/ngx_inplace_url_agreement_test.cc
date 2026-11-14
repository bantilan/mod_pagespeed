/* Licensed to the Apache Software Foundation (ASF) under one
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

#include "pagespeed/nginx/ngx_inplace_url_agreement.h"

#include "pagespeed/kernel/base/gtest.h"

namespace net_instaweb {
namespace {

const char kHost[] = "www.example.com";

// The server's reading is the decoded, normalized path; the module's is the
// canonical, escaped path.  An ordinary path reads the same both ways.
TEST(NgxInPlaceUrlAgreementTest, PlainPathIsOptimizedInPlace) {
  EXPECT_TRUE(NgxInPlaceUrlAgrees("/styles/site.css", kHost, "/styles/site.css",
                                  kHost, true));
  EXPECT_TRUE(NgxInPlaceUrlAgrees("/images/Puzzle.jpg", kHost,
                                  "/images/Puzzle.jpg", kHost, true));
  EXPECT_TRUE(NgxInPlaceUrlAgrees("/", kHost, "/", kHost, true));
  EXPECT_TRUE(
      NgxInPlaceUrlAgrees("/a+b/c.d.js", kHost, "/a+b/c.d.js", kHost, true));
}

// The server decodes a percent-encoded space or a UTF-8 character; the
// module keeps the escapes.  Both still name the same file.
TEST(NgxInPlaceUrlAgreementTest, PercentEncodedPathIsOptimizedInPlace) {
  EXPECT_TRUE(NgxInPlaceUrlAgrees("/images/a b.png", kHost, "/images/a%20b.png",
                                  kHost, true));
  EXPECT_TRUE(NgxInPlaceUrlAgrees("/caf\xC3\xA9/menu.css", kHost,
                                  "/caf%C3%A9/menu.css", kHost, true));
  EXPECT_TRUE(NgxInPlaceUrlAgrees("/\xE6\x97\xA5\xE6\x9C\xAC.js", kHost,
                                  "/%E6%97%A5%E6%9C%AC.js", kHost, true));
  // Reserved characters the module keeps escaped and the server decodes.
  EXPECT_TRUE(
      NgxInPlaceUrlAgrees("/100%.css", kHost, "/100%25.css", kHost, true));
  EXPECT_TRUE(NgxInPlaceUrlAgrees("/q?.css", kHost, "/q%3F.css", kHost, true));
  EXPECT_TRUE(
      NgxInPlaceUrlAgrees("/a\"b.css", kHost, "/a%22b.css", kHost, true));
}

// A query string takes no part in the decision: both readings are paths
// without their query, and the same path with any query is the same path.
TEST(NgxInPlaceUrlAgreementTest, PathWithQueryStringIsOptimizedInPlace) {
  EXPECT_TRUE(NgxInPlaceUrlAgrees("/styles/site.css", kHost, "/styles/site.css",
                                  kHost, true));
  EXPECT_TRUE(NgxInPlaceUrlAgrees("/images/a b.png", kHost, "/images/a%20b.png",
                                  kHost, true));
}

// A separator the module reads into the path and the server does not, or a
// dot segment the server resolves and the module does not: different paths,
// left to the server.
TEST(NgxInPlaceUrlAgreementTest, PathTheServerReadsDifferentlyIsLeftToIt) {
  EXPECT_FALSE(
      NgxInPlaceUrlAgrees("/a\\b.css", kHost, "/a/b.css", kHost, true));
  EXPECT_FALSE(NgxInPlaceUrlAgrees("/b.css", kHost, "/x/b.css", kHost, true));
  EXPECT_FALSE(
      NgxInPlaceUrlAgrees("/b.css", kHost, "/a%2F..%2Fb.css", kHost, true));
  EXPECT_FALSE(NgxInPlaceUrlAgrees("/a/b.css", kHost, "/a/B.css", kHost, true));
  EXPECT_FALSE(NgxInPlaceUrlAgrees("", kHost, "/a.css", kHost, true));
}

// The server merges runs of slashes (merge_slashes, on by default) and the
// module keeps them; with the server's setting known, the module's decoded
// path is merged the same way, and the two readings agree.  Off, they do not.
// A slash run ahead of a dot segment reads differently either way: the server
// merges before it resolves the segment, the module resolves it first.
TEST(NgxInPlaceUrlAgreementTest, SlashRunsAreMergedAsTheServerMergesThem) {
  EXPECT_TRUE(NgxInPlaceUrlAgrees("/a/b.css", kHost, "/a//b.css", kHost, true));
  EXPECT_TRUE(
      NgxInPlaceUrlAgrees("/a/b.css", kHost, "/a%2F%2Fb.css", kHost, true));
  EXPECT_TRUE(NgxInPlaceUrlAgrees("/a/b/", kHost, "//a///b//", kHost, true));
  EXPECT_FALSE(
      NgxInPlaceUrlAgrees("/a/b.css", kHost, "/a//b.css", kHost, false));
  EXPECT_FALSE(NgxInPlaceUrlAgrees("/b.css", kHost, "/a/b.css", kHost, true));
  EXPECT_FALSE(NgxInPlaceUrlAgrees("/b.css", kHost, "/a/b.css", kHost, false));
}

// An escaped slash is a separator to the server and an ordinary escape to
// the module; decoded, both name the same bytes, and the module's key keeps
// the spelling apart from the slash spelling.
TEST(NgxInPlaceUrlAgreementTest, EscapedSlashReadsAlikeOnceDecoded) {
  EXPECT_TRUE(
      NgxInPlaceUrlAgrees("/a/b.css", kHost, "/a%2Fb.css", kHost, true));
}

// The server matches its server blocks on the host bytes as sent; the module
// decodes and normalizes the host.  A host that reads differently is left to
// the server, in whatever block it chose.
TEST(NgxInPlaceUrlAgreementTest, HostTheServerReadsDifferentlyIsLeftToIt) {
  EXPECT_FALSE(NgxInPlaceUrlAgrees("/a.css", "st%61tic.example.com", "/a.css",
                                   "static.example.com", true));
  EXPECT_FALSE(
      NgxInPlaceUrlAgrees("/a.css", "0x7f000001", "/a.css", "127.0.0.1", true));
  EXPECT_FALSE(NgxInPlaceUrlAgrees("/a.css", "a.example.com", "/a.css",
                                   "b.example.com", true));
  EXPECT_FALSE(NgxInPlaceUrlAgrees("/a.css", "", "/a.css", kHost, true));
}

TEST(NgxInPlaceUrlAgreementTest, HostCaseDoesNotMatter) {
  EXPECT_TRUE(
      NgxInPlaceUrlAgrees("/a.css", "WWW.Example.com", "/a.css", kHost, true));
}

TEST(NgxInPlaceUrlAgreementTest, LiteralAddressHostsAgree) {
  EXPECT_TRUE(
      NgxInPlaceUrlAgrees("/a.css", "127.0.0.1", "/a.css", "127.0.0.1", true));
  EXPECT_TRUE(NgxInPlaceUrlAgrees("/a.css", "[::1]", "/a.css", "[::1]", true));
}

// NOTE (SEC-06 #10): upstream's TheInPlaceBranchConsultsTheDecision pin test
// reads the nginx wrapper source, which lives outside this gyp tree here; the
// wiring is covered by integration (a dot-segment/traversal URL must not be
// IPRO-optimized) instead.

}  // namespace
}  // namespace net_instaweb
