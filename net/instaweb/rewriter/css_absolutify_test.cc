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

// Unit-test CssAbsolutify: url()s inside CSS functions resolve against the
// same base as ones standing on their own, and fragment-only references
// stay as written.

#include "net/instaweb/rewriter/public/css_absolutify.h"

#include <memory>

#include "net/instaweb/rewriter/public/domain_lawyer.h"
#include "net/instaweb/rewriter/public/rewrite_driver.h"
#include "net/instaweb/rewriter/public/rewrite_options.h"
#include "net/instaweb/rewriter/public/rewrite_test_base.h"
#include "pagespeed/kernel/base/gtest.h"
#include "pagespeed/kernel/base/message_handler.h"
#include "pagespeed/kernel/base/mock_message_handler.h"
#include "pagespeed/kernel/base/string.h"
#include "pagespeed/kernel/http/google_url.h"
#include "third_party/css_parser/src/util/utf8/public/unicodetext.h"
#include "third_party/css_parser/src/webutil/css/parser.h"
#include "third_party/css_parser/src/webutil/css/property.h"
#include "third_party/css_parser/src/webutil/css/value.h"

namespace net_instaweb {

namespace {

class CssAbsolutifyTest : public RewriteTestBase {
 protected:
  void SetUp() override {
    RewriteTestBase::SetUp();
    // Map test.com onto the sharded cdn.com origin so absolutification is
    // observable. This mirrors the domain setup of the CssFilter absolutify
    // tests, so url(a.png) shards to cdn2.com and url(sub/c.png) to
    // cdn1.com.
    DomainLawyer* domain_lawyer = options()->WriteableDomainLawyer();
    MessageHandler* handler = message_handler();
    ASSERT_TRUE(domain_lawyer->AddDomain("http://cdn.com/", handler));
    ASSERT_TRUE(domain_lawyer->AddDomain("http://test.com/", handler));
    ASSERT_TRUE(
        domain_lawyer->AddShard("cdn.com", "cdn1.com,cdn2.com", handler));
    ASSERT_TRUE(domain_lawyer->AddRewriteDomainMapping("http://cdn.com",
                                                       "http://test.com",
                                                       handler));
  }

  // Parses css in preservation mode (no parse errors expected) and runs
  // CssAbsolutify::AbsolutifyUrls over it like the CssFilter
  // proxy/domain-mapping path does, with the stylesheet served from
  // http://test.com/foo.css.
  std::unique_ptr<Css::Stylesheet> Absolutify(
      const char* css, bool handle_parseable_sections,
      bool handle_unparseable_sections) {
    Css::Parser parser(css);
    parser.set_preservation_mode(true);
    parser.set_quirks_mode(false);
    std::unique_ptr<Css::Stylesheet> stylesheet(parser.ParseRawStylesheet());
    EXPECT_TRUE(parser.errors_seen_mask() == Css::Parser::kNoError);
    GoogleUrl base("http://test.com/foo.css");
    CssAbsolutify::AbsolutifyUrls(stylesheet.get(), base,
                                  handle_parseable_sections,
                                  handle_unparseable_sections,
                                  rewrite_driver(), message_handler());
    return stylesheet;
  }

};

TEST_F(CssAbsolutifyTest, UrlInsideFunctionAbsolutified) {
  std::unique_ptr<Css::Stylesheet> stylesheet =
      Absolutify("body { background-image: image-set(url(a.png) 1x) }",
                 true /* handle_parseable_sections */,
                 true /* handle_unparseable_sections */);
  ASSERT_EQ(1, stylesheet->rulesets().size());
  const Css::Ruleset* ruleset = stylesheet->rulesets()[0];
  ASSERT_EQ(Css::Ruleset::RULESET, ruleset->type());
  ASSERT_EQ(1, ruleset->declarations().size());
  const Css::Values* values = ruleset->declaration(0).values();
  ASSERT_EQ(1, values->size());
  const Css::Value* function = values->at(0);
  ASSERT_EQ(Css::Value::FUNCTION, function->GetLexicalUnitType());
  EXPECT_EQ("image-set", UnicodeTextToUTF8(function->GetFunctionName()));
  const Css::FunctionParameters* params =
      function->GetParametersWithSeparators();
  ASSERT_EQ(2, params->size());
  ASSERT_EQ(Css::Value::URI, params->value(0)->GetLexicalUnitType());
  EXPECT_EQ("http://cdn2.com/a.png",
            UnicodeTextToUTF8(params->value(0)->GetStringValue()));
  // The other parameter and the separator before it are kept.
  EXPECT_EQ(Css::FunctionParameters::SPACE_SEPARATED, params->separator(1));
  EXPECT_EQ(Css::Value::NUMBER, params->value(1)->GetLexicalUnitType());
}

// The same holds for a url() in a function nested in another function.
TEST_F(CssAbsolutifyTest, UrlInsideNestedFunctionAbsolutified) {
  std::unique_ptr<Css::Stylesheet> stylesheet =
      Absolutify("body { background-image: image-set(image(url(a.png)) 1x) }",
                 true /* handle_parseable_sections */,
                 true /* handle_unparseable_sections */);
  ASSERT_EQ(1, stylesheet->rulesets().size());
  const Css::Values* values =
      stylesheet->rulesets()[0]->declaration(0).values();
  ASSERT_EQ(1, values->size());
  const Css::Value* outer = values->at(0);
  ASSERT_EQ(Css::Value::FUNCTION, outer->GetLexicalUnitType());
  const Css::Value* inner = outer->GetParametersWithSeparators()->value(0);
  ASSERT_EQ(Css::Value::FUNCTION, inner->GetLexicalUnitType());
  const Css::Value* uri = inner->GetParametersWithSeparators()->value(0);
  ASSERT_EQ(Css::Value::URI, uri->GetLexicalUnitType());
  EXPECT_EQ("http://cdn2.com/a.png", UnicodeTextToUTF8(uri->GetStringValue()));
}

// A fragment-only reference names an element of the document that uses the
// stylesheet, so it stays as written, standing on its own and inside a
// function alike.
TEST_F(CssAbsolutifyTest, FragmentReferenceUnchanged) {
  std::unique_ptr<Css::Stylesheet> stylesheet = Absolutify(
      "body { background-image: url(#g) }"
      " div { background-image: image-set(url(#h) 1x) }",
      true /* handle_parseable_sections */,
      true /* handle_unparseable_sections */);
  ASSERT_EQ(2, stylesheet->rulesets().size());
  const Css::Values* plain =
      stylesheet->rulesets()[0]->declaration(0).values();
  ASSERT_EQ(1, plain->size());
  ASSERT_EQ(Css::Value::URI, plain->at(0)->GetLexicalUnitType());
  EXPECT_EQ("#g", UnicodeTextToUTF8(plain->at(0)->GetStringValue()));
  const Css::Values* values =
      stylesheet->rulesets()[1]->declaration(0).values();
  ASSERT_EQ(1, values->size());
  ASSERT_EQ(Css::Value::FUNCTION, values->at(0)->GetLexicalUnitType());
  const Css::Value* uri =
      values->at(0)->GetParametersWithSeparators()->value(0);
  ASSERT_EQ(Css::Value::URI, uri->GetLexicalUnitType());
  EXPECT_EQ("#h", UnicodeTextToUTF8(uri->GetStringValue()));
}

}  // namespace

}  // namespace net_instaweb
