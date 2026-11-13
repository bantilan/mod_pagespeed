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
#include "pagespeed/kernel/http/http_names.h"
#include "pagespeed/kernel/http/response_headers.h"
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

// True if the ASCII case-insensitive token appears in a space-separated
// relation list such as a link rel attribute.
bool HasRelToken(StringPiece rel_value, const char* token) {
  StringPieceVector tokens;
  SplitStringPieceToVector(rel_value, " \t", &tokens, true);
  for (int i = 0, n = tokens.size(); i < n; ++i) {
    if (StringCaseEqual(tokens[i], token)) {
      return true;
    }
  }
  return false;
}

// Unquotes a Link-header parameter value in place (".." or bare token).
void UnquoteLinkParam(StringPiece* value) {
  TrimWhitespace(value);
  if (value->size() >= 2 && value->starts_with("\"") &&
      value->ends_with("\"")) {
    value->remove_prefix(1);
    value->remove_suffix(1);
  }
}

// Collects the absolutized targets of author image preloads from Link
// response headers (RFC 8288): entries with rel including "preload" and
// as="image". Unknown parameters are ignored; malformed entries are skipped.
void CollectLinkHeaderPreloads(const ResponseHeaders* headers,
                              const GoogleUrl& base_url, StringSet* out) {
  if (headers == NULL) {
    return;
  }
  ConstStringStarVector values;
  if (!headers->Lookup(HttpAttributes::kLink, &values)) {
    return;
  }
  for (int vi = 0, vn = values.size(); vi < vn; ++vi) {
    StringPiece header(*values[vi]);
    // Split top-level link-values on commas, honoring double quotes.
    size_t pos = 0;
    while (pos < header.size()) {
      bool in_quotes = false;
      size_t end = pos;
      for (; end < header.size(); ++end) {
        if (header[end] == '"') {
          in_quotes = !in_quotes;
        } else if (header[end] == ',' && !in_quotes) {
          break;
        }
      }
      StringPiece entry(header.data() + pos, end - pos);
      pos = (end < header.size()) ? end + 1 : end;
      TrimWhitespace(&entry);
      // Target URI-Reference in angle brackets.
      if (!entry.starts_with("<")) {
        continue;
      }
      StringPiece::size_type close = entry.find('>');
      if (close == StringPiece::npos) {
        continue;
      }
      StringPiece target(
          entry.data() + 1, (close > 1) ? close - 1 : 0);
      TrimWhitespace(&target);
      // Scan ;parameters for rel/as.
      bool is_preload = false;
      bool is_image = false;
      StringPiece params(entry.data() + close + 1,
                         entry.size() - close - 1);
      StringPieceVector parts;
      SplitStringPieceToVector(params, ";", &parts, true);
      for (int i = 0, n = parts.size(); i < n; ++i) {
        StringPiece part(parts[i]);
        StringPiece::size_type eq = part.find('=');
        if (eq == StringPiece::npos) {
          continue;
        }
        StringPiece name(part.data(), eq);
        StringPiece value(part.data() + eq + 1, part.size() - eq - 1);
        TrimWhitespace(&name);
        UnquoteLinkParam(&value);
        if (StringCaseEqual(name, "rel") && HasRelToken(value, "preload")) {
          is_preload = true;
        } else if (StringCaseEqual(name, "as") &&
                   StringCaseEqual(value, "image")) {
          is_image = true;
        }
      }
      if (is_preload && is_image && !target.empty() &&
          !IsDataUrl(target)) {
        GoogleUrl absolute(base_url, target);
        if (absolute.IsAnyValid()) {
          out->insert(absolute.Spec().as_string());
        }
      }
    }
  }
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
  author_preloaded_hrefs_.clear();
  link_headers_scanned_ = false;
  picture_sources_.clear();
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
  if (element->keyword() == HtmlName::kSource) {
    NotePictureSource(element);
    return;
  }
  if (element->keyword() == HtmlName::kLink) {
    NoteAuthorPreload(element);
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
      PrioritizeImage(element, src, absolute_url);
    }
    // Learned-critical images past the hero slot are left alone, never lazy.
  } else if (!saw_eligible_image_ && !evidence_available) {
    // No evidence at all: the first eligible image is the heuristic hero.
    PrioritizeImage(element, src, absolute_url);
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

void PrioritizeLcpImagesFilter::NotePictureSource(HtmlElement* element) {
  if (noscript_element() != NULL) {
    return;
  }
  HtmlElement* parent = element->parent();
  if (parent == NULL ||
      !StringCaseEqual(parent->name().value(), "picture")) {
    // Only <picture> sources affect img selection (<video>/<audio> don't).
    return;
  }
  const char* srcset = element->AttributeValue(HtmlName::kSrcset);
  if (srcset == NULL || srcset[0] == '\0') {
    // Sources without srcset contribute no candidate.
    return;
  }
  PictureSources& entry = picture_sources_[parent];
  if (entry.count == 0) {
    entry.srcset = srcset;
    const char* media = element->AttributeValue(HtmlName::kMedia);
    if (media != NULL) {
      entry.media = media;
    }
    const char* type = element->AttributeValue(HtmlName::kType);
    if (type != NULL) {
      entry.type = type;
    }
  }
  ++entry.count;
}

void PrioritizeLcpImagesFilter::NoteAuthorPreload(HtmlElement* element) {
  if (noscript_element() != NULL) {
    return;
  }
  const char* rel = element->AttributeValue(HtmlName::kRel);
  const char* as = element->AttributeValue(HtmlName::kAs);
  const char* href = element->AttributeValue(HtmlName::kHref);
  if (rel == NULL || as == NULL || href == NULL) {
    return;
  }
  if (!HasRelToken(rel, "preload") || !StringCaseEqual(as, "image")) {
    return;
  }
  StringPiece href_value(href);
  if (href_value.empty() || IsDataUrl(href_value)) {
    return;
  }
  GoogleUrl absolute(driver()->base_url(), href_value);
  if (absolute.IsAnyValid()) {
    author_preloaded_hrefs_.insert(absolute.Spec().as_string());
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

bool PrioritizeLcpImagesFilter::IsCoveredByAuthorPreload(
    const GoogleString& absolute_url) {
  if (!link_headers_scanned_) {
    link_headers_scanned_ = true;
    // Link header targets resolve against the request URL, not <base>.
    CollectLinkHeaderPreloads(driver()->response_headers(),
                             driver()->google_url(), &author_preloaded_hrefs_);
  }
  return author_preloaded_hrefs_.find(absolute_url) !=
         author_preloaded_hrefs_.end();
}

void PrioritizeLcpImagesFilter::InsertPreloadHint(
    HtmlElement* element, const GoogleString& src,
    const GoogleString& absolute_url) {
  if (IsCoveredByAuthorPreload(absolute_url)) {
    return;
  }
  // Selection precedence follows the browser: usable <source> candidates
  // win over the img's own srcset, which wins over the bare src fallback.
  GoogleString imagesrcset;
  GoogleString media;
  GoogleString type;
  PictureSourceMap::const_iterator sources =
      picture_sources_.find(element->parent());
  if (sources != picture_sources_.end() && sources->second.count > 1) {
    // Viewport-dependent selection across sources cannot be expressed in a
    // single hint; the element priority set below still applies.
    return;
  } else if (sources != picture_sources_.end()) {
    imagesrcset = sources->second.srcset;
    media = sources->second.media;
    type = sources->second.type;
  } else {
    const char* srcset = element->AttributeValue(HtmlName::kSrcset);
    if (srcset != NULL && srcset[0] != '\0') {
      imagesrcset = srcset;
    }
  }
  if (!preloaded_hrefs_.insert(src).second) {
    return;
  }
  HtmlElement* preload = driver()->NewElement(element, HtmlName::kLink);
  driver()->AddAttribute(preload, HtmlName::kRel, "preload");
  driver()->AddAttribute(preload, HtmlName::kAs, "image");
  driver()->AddAttribute(preload, HtmlName::kHref, src);
  driver()->AddAttribute(preload, HtmlName::kFetchpriority, "high");
  if (!imagesrcset.empty()) {
    driver()->AddAttribute(preload, "imagesrcset", imagesrcset);
    // Sizes always live on the img, even for picture sources.
    HtmlElement::Attribute* sizes_attr = element->FindAttribute("sizes");
    if (sizes_attr != NULL && sizes_attr->DecodedValueOrNull() != NULL &&
        sizes_attr->DecodedValueOrNull()[0] != '\0') {
      driver()->AddAttribute(preload, "imagesizes",
                             sizes_attr->DecodedValueOrNull());
    }
  }
  if (!media.empty()) {
    driver()->AddAttribute(preload, HtmlName::kMedia, media);
  }
  if (!type.empty()) {
    driver()->AddAttribute(preload, HtmlName::kType, type);
  }
  // The fetch credentials must match or the preload and the image load land
  // in different cache entries. A bare attribute means anonymous; an
  // explicit empty value serializes identically.
  HtmlElement::Attribute* crossorigin_attr =
      element->FindAttribute("crossorigin");
  if (crossorigin_attr != NULL) {
    const char* mode = crossorigin_attr->DecodedValueOrNull();
    driver()->AddAttribute(preload, "crossorigin",
                           (mode != NULL) ? mode : "");
  }
  if (head_element_ != NULL && driver()->CanAppendChild(head_element_)) {
    driver()->AppendChild(head_element_, preload);
  } else {
    // Fallback for malformed documents without a writable <head>.
    driver()->InsertNodeBeforeNode(element, preload);
  }
}

void PrioritizeLcpImagesFilter::PrioritizeImage(
    HtmlElement* element, const GoogleString& src,
    const GoogleString& absolute_url) {
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

  InsertPreloadHint(element, src, absolute_url);

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
