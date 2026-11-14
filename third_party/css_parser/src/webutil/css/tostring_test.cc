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



#include "webutil/css/tostring.h"

#include <string>

#include <memory>
#include "base/scoped_ptr.h"

#include "base/stringprintf.h"
#include "testing/base/public/googletest.h"
#include "testing/base/public/gunit.h"
#include "webutil/css/parser.h"
#include "webutil/css/string.h"

namespace {

#define TESTDECLARATIONS(css) TestDeclarations(css, __LINE__)
#define TESTSTYLESHEET(css) TestStylesheet(css, __LINE__)

class ToStringTest : public testing::Test {
 protected:
  // Checks if ParseDeclarations()->ToString() returns identity.
  void TestDeclarations(const string& css, int line) {
    SCOPED_TRACE(StringPrintf("at line %d", line));
    Css::Parser parser(css);
    scoped_ptr<Css::Declarations> decls(parser.ParseDeclarations());
    EXPECT_EQ(css, decls->ToString());
  }

  // Checks if ParseStylesheet()->ToString() returns identity.
  void TestStylesheet(const string& css, int line) {
    SCOPED_TRACE(StringPrintf("at line %d", line));
    Css::Parser parser(css);
    scoped_ptr<Css::Stylesheet> stylesheet(parser.ParseStylesheet());
    EXPECT_EQ(css, stylesheet->ToString());
  }
};

TEST_F(ToStringTest, declarations) {
  TESTDECLARATIONS("left: inherit; "
                   "color: #abcdef; "
                   "content: \"text\"; "
                   "top: 1; right: 2px !important; "
                   "background-image: url(link\\(a,b,\\\"c\\\"\\).html)");
  TESTDECLARATIONS("content: counter(); clip: rect(auto 1px 2em auto)");
  TESTDECLARATIONS("font-family: arial,serif,\"Courier New\"");

  // FONT is special
  Css::Parser parser("font: 3px/1.1 Arial");
  scoped_ptr<Css::Declarations> decls(parser.ParseDeclarations());
  EXPECT_EQ("font: 3px/1.1 Arial; "
            "font-style: normal; font-variant: normal; font-weight: normal; "
            "font-size: 3px; line-height: 1.1; font-family: Arial",
            decls->ToString());
}

TEST_F(ToStringTest, selectors) {
  TESTSTYLESHEET("/* AUTHOR */\n\n\n\n"
                 "a, *, b#id, c.class, :hover:focus {top: 1}\n");
  TESTSTYLESHEET("/* AUTHOR */\n\n\n\n"
                 "table[width], [disable=\"no\"], [x~=\"y\"], [lang|=\"fr\"] "
                 "{top: 1}\n");
  TESTSTYLESHEET("/* AUTHOR */\n\n\n\n"
                 "img[height=\"1\"] {display: block}\n"
                 "[class^=\"icon-\"], [class*=\" icon-\"] {top: 1}\n");
  TESTSTYLESHEET("/* AUTHOR */\n\n\n\n"
                 "a > b, a + b, a b + c > d > e f {top: 1}\n");
}

TEST_F(ToStringTest, misc) {
  TESTSTYLESHEET("/* AUTHOR */\n\n"
                 "@import url(\"a.html\") ;\n"
                 "@import url(\"b.html\") print;\n"
                 "\n"
                 "@media print, screen { a {top: 1} }\n"
                 "b {color: #ff0000}\n");

  scoped_ptr<Css::Parser> parser(new Css::Parser("a {top: 1}"));
  scoped_ptr<Css::Stylesheet> stylesheet(parser->ParseStylesheet());
  stylesheet->set_type(Css::Stylesheet::SYSTEM);
  EXPECT_EQ("/* SYSTEM */\n\n\n\n"
            "a {top: 1}\n",
            stylesheet->ToString());

  // Make sure we correctly deal with escaped newline.
  parser.reset(new Css::Parser("a { content: 'line 1\\\n"
                               "line 2'; }"));
  stylesheet.reset(parser->ParseStylesheet());
  EXPECT_EQ("/* AUTHOR */\n\n\n\n"
            "a {content: \"line 1line 2\"}\n", stylesheet->ToString());
}

TEST_F(ToStringTest, SpecialChars) {
  Css::Parser parser("content: \"Special chars: \\n\\r\\t\\A \\D \\9\"");
  scoped_ptr<Css::Declarations> decls(parser.ParseDeclarations());
  EXPECT_EQ("content: \"Special chars: nrt\\A \\D \\9 \"", decls->ToString());
}

TEST_F(ToStringTest, MediaQueries) {
  TESTSTYLESHEET("/* AUTHOR */\n\n"
                 "@import url(\"a.css\") not screen;\n"
                 "@import url(\"b.css\") (color) and (max-width: 38px);\n"
                 "\n"
                 "@media only print and (color) { .a {right: 1} }\n");
}

TEST_F(ToStringTest, EscapeIdentifier) {
  // We should escape all special chars, but not UTF8.
  EXPECT_EQ("\\*Hello\\,\\ दुनिया\\!", Css::EscapeIdentifier("*Hello, दुनिया!"));
}

// An identifier cannot start with a digit, or with '-' followed by a digit,
// so such a character is written as a hex escape. The escape ends in a space
// so that a following hex digit is not read as part of it.
TEST_F(ToStringTest, EscapeIdentifierNameLeadingDigit) {
  EXPECT_EQ("\\32 xl\\:p-4", Css::EscapeIdentifierName("2xl:p-4"));
  EXPECT_EQ("\\39 ", Css::EscapeIdentifierName("9"));
  EXPECT_EQ("\\31 a", Css::EscapeIdentifierName("1a"));
  EXPECT_EQ("-\\31 x", Css::EscapeIdentifierName("-1x"));
  // Digits elsewhere are plain ident characters.
  EXPECT_EQ("a1b", Css::EscapeIdentifierName("a1b"));
  EXPECT_EQ("x2", Css::EscapeIdentifierName("x2"));
  EXPECT_EQ("--1x", Css::EscapeIdentifierName("--1x"));
  // A name that is exactly '-' is not an identifier unless the '-' is
  // escaped.
  EXPECT_EQ("\\-", Css::EscapeIdentifierName("-"));
  EXPECT_EQ("", Css::EscapeIdentifierName(""));
  EXPECT_EQ("\\32 \\.", Css::EscapeIdentifierName("2."));
  // EscapeIdentifier also writes names that are not identifiers in the
  // source (property names, keyword values), so it leaves digits alone.
  EXPECT_EQ("2xl\\:p-4", Css::EscapeIdentifier("2xl:p-4"));
  EXPECT_EQ("-045F", Css::EscapeIdentifier("-045F"));
}

// Selectors with escaped characters serialize to an equivalent selector.
TEST_F(ToStringTest, SelectorEscapesRoundTrip) {
  struct Case {
    const char* in;
    const char* out;
  };
  const Case kCases[] = {
      // Tailwind responsive utilities (leading digit, escaped colon).
      {".\\32 xl\\:p-4", ".\\32 xl\\:p-4"},
      {".\\32 xl\\:col-\\(--col-span-2xl\\)",
       ".\\32 xl\\:col-\\(--col-span-2xl\\)"},
      {".sm\\:p-4 > .\\32 xl\\:p-4", ".sm\\:p-4 > .\\32 xl\\:p-4"},
      // A digit after a leading '-'.
      {".-\\31 x", ".-\\31 x"},
      // A 6-digit escape is written in its shortest form.
      {".\\000032 xl", ".\\32 xl"},
      // The escape is followed by a hex digit: the space must stay.
      {".\\31 b", ".\\31 b"},
      {".\\31 0", ".\\31 0"},
      // An escaped digit that is not the first character needs no escape.
      {".a\\31 b", ".a1b"},
      // Escape at the end of the selector.
      {".foo\\32 ", ".foo2"},
      {".\\32 ", ".\\32 "},
      // Id selectors follow the same rule.
      {"#\\31 23", "#\\31 23"},
      // A name that is an escaped '-' alone.
      {".\\2d ", ".\\-"},
      {"#\\-", "#\\-"},
      // An escaped colon alone.
      {".hover\\:x", ".hover\\:x"},
  };
  for (const Case& c : kCases) {
    SCOPED_TRACE(c.in);
    // The parser keeps a view of its input, so the input outlives it.
    const string css = StringPrintf("%s {}", c.in);
    Css::Parser parser(css);
    std::unique_ptr<Css::Selectors> selectors(parser.ParseSelectors());
    EXPECT_EQ(Css::Parser::kNoError, parser.errors_seen_mask());
    if (selectors.get() == NULL) {
      ADD_FAILURE() << "not parsed";
      continue;
    }
    EXPECT_EQ(c.out, selectors->ToString());
  }
}

// A class or id name that starts with an unescaped digit is not a valid
// selector; browsers ignore the rule, so the parser reports a selector error.
TEST_F(ToStringTest, SelectorUnescapedLeadingDigitIsError) {
  const char* const kCases[] = {".1a", "#123", ".-1x", "a.2xl\\:p-4",
                                ".-",  "#-",   ".- .b"};
  for (const char* in : kCases) {
    SCOPED_TRACE(in);
    const string css = StringPrintf("%s {}", in);
    Css::Parser parser(css);
    std::unique_ptr<Css::Selectors> selectors(parser.ParseSelectors());
    EXPECT_TRUE(selectors.get() == NULL);
    EXPECT_NE(0, parser.errors_seen_mask() & Css::Parser::kSelectorError);
  }
}

}  // namespace
