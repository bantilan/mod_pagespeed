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

#include "net/instaweb/rewriter/public/strip_legacy_polyfills_filter.h"
#include "net/instaweb/rewriter/public/script_tag_scanner.h"
#include "net/instaweb/rewriter/public/rewrite_options.h"

#include "pagespeed/kernel/base/string_util.h"
#include "pagespeed/kernel/html/html_element.h"
#include "pagespeed/kernel/html/html_name.h"
#include "pagespeed/kernel/html/html_parse.h"
#include "pagespeed/kernel/http/google_url.h"

namespace net_instaweb {

StripLegacyPolyfillsFilter::StripLegacyPolyfillsFilter(
    HtmlParse* html_parse, const RewriteOptions* options)
    : html_parse_(html_parse), options_(options) {}

StripLegacyPolyfillsFilter::~StripLegacyPolyfillsFilter() {}

void StripLegacyPolyfillsFilter::EndElement(HtmlElement* element) {
  if (element->keyword() != HtmlName::kScript) {
    return;
  }
  const HtmlElement::Attribute* marker =
      element->FindAttribute("data-pagespeed-remove-polyfill");
  const char* authorization = marker == nullptr ? nullptr : marker->DecodedValueOrNull();
  if (element->FindAttribute(HtmlName::kDataPagespeedNoTransform) != nullptr ||
      element->FindAttribute("nomodule") != nullptr) return;

  bool approved = authorization != nullptr && StringCaseEqual(authorization, "true");
  bool have_filenames = options_ != nullptr && options_->HasApprovedPolyfillFilenames();
  if (!approved && !have_filenames) return;

  // Approval can be supplied by the app marker or the operator's exact list.
  // Neither path authorizes removal of modules, fallbacks or data blocks.
  ScriptTagScanner scanner(html_parse_);
  HtmlElement::Attribute* src = nullptr;
  if (scanner.ParseScriptElement(element, &src) != ScriptTagScanner::kJavaScript ||
      src == nullptr || src->DecodedValueOrNull() == nullptr) return;
  StringPiece url(src->DecodedValueOrNull());
  TrimWhitespace(&url);
  if (url.empty()) return;
  if (!approved) {
    StringPiece path = url.substr(0, url.find_first_of("?#"));
    size_t slash = path.rfind('/');
    StringPiece filename = path.substr(slash == StringPiece::npos ? 0 : slash + 1);
    if (!options_->IsApprovedPolyfillFilename(filename)) return;
    // A fixed base handles relative src values without using the containing
    // page's filename when src is only a query or fragment. Only HTTP(S) URLs
    // participate. Require the raw basename too: URL canonicalization can
    // decode percent escapes and must not broaden the explicit allowlist.
    GoogleUrl parsed(GoogleUrl("https://pagespeed.invalid/"), url);
    approved = parsed.IsWebValid() && parsed.LeafSansQuery() == filename;
  }
  if (approved) html_parse_->DeleteNode(element);
}

}  // namespace net_instaweb
