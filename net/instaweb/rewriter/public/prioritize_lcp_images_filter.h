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

#ifndef NET_INSTAWEB_REWRITER_PUBLIC_PRIORITIZE_LCP_IMAGES_FILTER_H_
#define NET_INSTAWEB_REWRITER_PUBLIC_PRIORITIZE_LCP_IMAGES_FILTER_H_

#include <set>

#include "net/instaweb/rewriter/public/common_filter.h"
#include "pagespeed/kernel/base/basictypes.h"
#include "pagespeed/kernel/base/string.h"
#include "pagespeed/kernel/html/html_element.h"

namespace net_instaweb {

// Rewriter that prioritizes the likely LCP image and applies native lazy
// loading for later images. Hero selection prefers accepted LCP evidence
// (the critical-images finder, fed by the LCP beacon) over document position:
// 1) a learned-critical image takes the single hero slot wherever it appears
// 2) with no evidence at all, the first eligible image is the heuristic hero
// 3) with evidence that matches nothing (e.g. a changed variant), the first
//    image is left alone rather than guess-preloaded
// The hero gets loading="lazy" removed, fetchpriority="high", a preload hint
// in <head>, and pagespeed_no_defer/data-pagespeed-no-defer to avoid defer
// filters. Later images get loading="lazy" except learned-critical images
// and explicit author eager/high-priority choices, which are never lazy.
// Hidden images, tiny trackers, and data URLs are skipped entirely.
class PrioritizeLcpImagesFilter : public CommonFilter {
 public:
  explicit PrioritizeLcpImagesFilter(RewriteDriver* driver);
  ~PrioritizeLcpImagesFilter() override;

  const char* Name() const override { return "PrioritizeLcpImages"; }

 private:
  void DetermineEnabled(GoogleString* disabled_reason) override;
  void StartDocumentImpl() override;
  void StartElementImpl(HtmlElement* element) override;
  void EndElementImpl(HtmlElement* element) override;

  bool IsEligibleImage(HtmlElement* element, GoogleString* src_out,
                       GoogleString* absolute_url_out) const;
  void InsertPreloadHint(HtmlElement* element, const GoogleString& src);
  void PrioritizeImage(HtmlElement* element, const GoogleString& src);
  void AddLazyLoadingHint(HtmlElement* element);
  bool IsHidden(HtmlElement* element) const;
  bool LooksLikeTinyTracker(HtmlElement* element) const;

  HtmlElement* head_element_;
  bool in_head_;
  bool in_body_;
  int prioritized_count_;
  bool saw_eligible_image_;
  std::set<GoogleString> preloaded_hrefs_;

  DISALLOW_COPY_AND_ASSIGN(PrioritizeLcpImagesFilter);
};

}  // namespace net_instaweb

#endif  // NET_INSTAWEB_REWRITER_PUBLIC_PRIORITIZE_LCP_IMAGES_FILTER_H_
