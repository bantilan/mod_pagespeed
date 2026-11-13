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

#include "net/instaweb/rewriter/public/prioritize_lcp_images_filter.h"

#include "net/instaweb/rewriter/public/critical_images_finder.h"
#include "net/instaweb/rewriter/public/rewrite_driver.h"
#include "net/instaweb/rewriter/public/rewrite_options.h"
#include "net/instaweb/rewriter/public/server_context.h"
#include "pagespeed/kernel/base/string.h"
#include "pagespeed/kernel/base/string_util.h"
#include "pagespeed/kernel/html/html_element.h"
#include "pagespeed/kernel/html/html_name.h"
#include "pagespeed/kernel/http/data_url.h"
#include "pagespeed/kernel/http/google_url.h"
#include "pagespeed/opt/logging/enums.pb.h"
#include "pagespeed/opt/logging/log_record.h"

namespace {

const int kMaxPrioritizedImages = 1;

}  // namespace

namespace net_instaweb {

namespace {

// Case-insensitive ASCII whitespace-tolerant check for hiding declarations.
// Matches e.g. "display:none", "display: none", "VISIBILITY : HIDDEN", with
// optional "!important". Only display/visibility are considered; hiding via
// stylesheets cannot be resolved server-side.
bool InlineStyleHidesElement(StringPiece style_value) {
  StringPieceVector declarations;
  SplitStringPieceToVector(style_value, ";", &declarations, true);
  for (int i = 0, n = declarations.size(); i < n; ++i) {
    StringPiece declaration(declarations[i]);
    StringPiece::size_type colon = declaration.find(':');
    if (colon == StringPiece::npos) {
      continue;
    }
    StringPiece name(declaration.data(), colon);
    StringPiece value(declaration.data() + colon + 1,
                      declaration.size() - colon - 1);
    TrimWhitespace(&name);
    TrimWhitespace(&value);
    // Strip a trailing "!important", which does not change hiding.
    static const char kImportant[] = "!important";
    if (value.size() >= sizeof(kImportant) - 1 &&
        StringCaseEqual(value.substr(value.size() - (sizeof(kImportant) - 1)),
                        kImportant)) {
      value.remove_suffix(sizeof(kImportant) - 1);
      TrimWhitespace(&value);
    }
    if ((StringCaseEqual(name, "display") &&
         StringCaseEqual(value, "none")) ||
        (StringCaseEqual(name, "visibility") &&
         StringCaseEqual(value, "hidden"))) {
      return true;
    }
  }
  return false;
}

}  // namespace

PrioritizeLcpImagesFilter::PrioritizeLcpImagesFilter(RewriteDriver* driver)
    : CommonFilter(driver) {
  StartDocumentImpl();
}

PrioritizeLcpImagesFilter::~PrioritizeLcpImagesFilter() {}

void PrioritizeLcpImagesFilter::DetermineEnabled(GoogleString* disabled_reason) {
  set_is_enabled(true);
}

void PrioritizeLcpImagesFilter::StartDocumentImpl() {
  head_element_ = NULL;
  in_head_ = false;
  in_body_ = false;
  prioritized_count_ = 0;
  saw_eligible_image_ = false;
  preloaded_hrefs_.clear();
}

void PrioritizeLcpImagesFilter::StartElementImpl(HtmlElement* element) {
  if (element->keyword() == HtmlName::kHead) {
    head_element_ = element;
    in_head_ = true;
    return;
  }
  if (element->keyword() == HtmlName::kBody) {
    in_body_ = true;
    return;
  }
  if (noscript_element() != NULL || !in_body_) {
    return;
  }
  if (element->keyword() != HtmlName::kImg || !driver()->IsRewritable(element)) {
    return;
  }

  GoogleString src;
  GoogleString absolute_url;
  if (!IsEligibleImage(element, &src, &absolute_url)) {
    return;
  }
  // Prefer accepted LCP evidence over document position. The finder data is
  // memoized per request, so these lookups are cheap after the first image.
  CriticalImagesFinder* finder =
      driver()->server_context()->critical_images_finder();
  const bool evidence_available =
      finder != NULL &&
      finder->Available(driver()) == CriticalImagesFinder::kAvailable;
  const bool learned_critical =
      evidence_available &&
      finder->IsHtmlCriticalImage(absolute_url, driver());
  if (learned_critical) {
    if (prioritized_count_ < kMaxPrioritizedImages) {
      PrioritizeImage(element, src);
    }
    // Learned-critical images past the hero slot are left alone, never lazy.
  } else if (!saw_eligible_image_ && !evidence_available) {
    // No evidence at all: the first eligible image is the heuristic hero.
    PrioritizeImage(element, src);
  } else if (!saw_eligible_image_) {
    // Evidence exists but names other images (e.g. a changed variant): leave
    // the first image alone rather than spending the single preload on a
    // guess the evidence contradicts, and rather than lazy-loading a
    // possibly-visible image.
  } else {
    AddLazyLoadingHint(element);
  }
  saw_eligible_image_ = true;
}

void PrioritizeLcpImagesFilter::EndElementImpl(HtmlElement* element) {
  if (element->keyword() == HtmlName::kHead) {
    in_head_ = false;
  } else if (element->keyword() == HtmlName::kBody) {
    in_body_ = false;
  }
}

bool PrioritizeLcpImagesFilter::IsHidden(HtmlElement* element) const {
  // Hiding propagates from ancestors. Only the hidden attribute and inline
  // styles are considered: hiding via stylesheets cannot be resolved
  // server-side.
  for (HtmlElement* node = element; node != NULL;
       node = node->parent()) {
    // The hidden attribute applies unless overridden, which only stylesheets
    // or an un-hiding inline style could do. Stylesheets are invisible here;
    // hidden-plus-unhiding-inline-style on an LCP candidate is pathological,
    // and skipping it is fail-safe (no wrong preload, and hidden images are
    // never lazy-loaded either).
    if (node->FindAttribute("hidden") != NULL) {
      return true;
    }
    const HtmlElement::Attribute* style_attr =
        node->FindAttribute(HtmlName::kStyle);
    if (style_attr != NULL && style_attr->DecodedValueOrNull() != NULL &&
        InlineStyleHidesElement(style_attr->DecodedValueOrNull())) {
      return true;
    }
  }
  return false;
}

bool PrioritizeLcpImagesFilter::LooksLikeTinyTracker(HtmlElement* element) const {
  int width = 0;
  int height = 0;
  const char* width_str = element->AttributeValue(HtmlName::kWidth);
  const char* height_str = element->AttributeValue(HtmlName::kHeight);
  if (width_str != NULL && height_str != NULL &&
      StringToInt(width_str, &width) && StringToInt(height_str, &height)) {
    return width <= 1 && height <= 1;
  }
  return false;
}

bool PrioritizeLcpImagesFilter::IsEligibleImage(
    HtmlElement* element, GoogleString* src_out,
    GoogleString* absolute_url_out) const {
  if (IsHidden(element) || LooksLikeTinyTracker(element)) {
    return false;
  }
  HtmlElement::Attribute* src_attr = element->FindAttribute(HtmlName::kSrc);
  if (src_attr == NULL || src_attr->DecodedValueOrNull() == NULL) {
    return false;
  }

  StringPiece src(src_attr->DecodedValueOrNull());
  if (src.empty() || IsDataUrl(src)) {
    return false;
  }

  GoogleUrl gurl(driver()->base_url(), src);
  if (!gurl.IsAnyValid()) {
    return false;
  }
  src.CopyToString(src_out);
  gurl.Spec().CopyToString(absolute_url_out);
  return true;
}

void PrioritizeLcpImagesFilter::InsertPreloadHint(
    HtmlElement* element, const GoogleString& src) {
  if (!preloaded_hrefs_.insert(src).second) {
    return;
  }
  HtmlElement* preload = driver()->NewElement(element, HtmlName::kLink);
  driver()->AddAttribute(preload, HtmlName::kRel, "preload");
  driver()->AddAttribute(preload, HtmlName::kAs, "image");
  driver()->AddAttribute(preload, HtmlName::kHref, src);
  driver()->AddAttribute(preload, HtmlName::kFetchpriority, "high");
  if (head_element_ != NULL && driver()->CanAppendChild(head_element_)) {
    driver()->AppendChild(head_element_, preload);
  } else {
    // Fallback for malformed documents without a writable <head>.
    driver()->InsertNodeBeforeNode(element, preload);
  }
}

void PrioritizeLcpImagesFilter::PrioritizeImage(
    HtmlElement* element, const GoogleString& src) {
  // Ensure this candidate is not lazy/deferred by browser or other filters.
  HtmlElement::Attribute* loading_attr = element->FindAttribute("loading");
  if (loading_attr != NULL &&
      loading_attr->DecodedValueOrNull() != NULL &&
      StringCaseEqual(loading_attr->DecodedValueOrNull(), "lazy")) {
    element->DeleteAttribute("loading");
  }

  HtmlElement::Attribute* fetchpriority_attr =
      element->FindAttribute("fetchpriority");
  if (fetchpriority_attr != NULL) {
    fetchpriority_attr->SetValue("high");
  } else {
    driver()->AddAttribute(element, HtmlName::kFetchpriority, "high");
  }

  if (element->FindAttribute(HtmlName::kDataPagespeedNoDefer) == NULL) {
    driver()->AddAttribute(element, HtmlName::kDataPagespeedNoDefer, "1");
  }
  if (element->FindAttribute(HtmlName::kPagespeedNoDefer) == NULL) {
    driver()->AddAttribute(element, HtmlName::kPagespeedNoDefer, "1");
  }

  InsertPreloadHint(element, src);

  ++prioritized_count_;
  driver()->log_record()->SetRewriterLoggingStatus(
      RewriteOptions::FilterId(RewriteOptions::kPrioritizeLcpImages),
      RewriterApplication::APPLIED_OK);
}

void PrioritizeLcpImagesFilter::AddLazyLoadingHint(HtmlElement* element) {
  // Respect an explicit author priority signal: lazy-loading an image the
  // author marked high-priority contradicts their intent.
  const HtmlElement::Attribute* fetchpriority_attr =
      element->FindAttribute(HtmlName::kFetchpriority);
  if (fetchpriority_attr != NULL &&
      fetchpriority_attr->DecodedValueOrNull() != NULL &&
      StringCaseEqual(fetchpriority_attr->DecodedValueOrNull(), "high")) {
    return;
  }
  HtmlElement::Attribute* loading_attr = element->FindAttribute("loading");
  if (loading_attr != NULL && loading_attr->DecodedValueOrNull() != NULL) {
    // Respect explicit eager loading from origin/app code.
    if (StringCaseEqual(loading_attr->DecodedValueOrNull(), "eager")) {
      return;
    }
    if (!StringCaseEqual(loading_attr->DecodedValueOrNull(), "lazy")) {
      loading_attr->SetValue("lazy");
    }
    return;
  }
  driver()->AddAttribute(element, "loading", "lazy");
}

}  // namespace net_instaweb
