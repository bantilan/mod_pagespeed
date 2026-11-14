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

//
// Decides whether in-place optimization may touch a request: true when the
// server and the module read it alike (see NgxInPlaceUrlAgrees).
//
// Deliberately nginx-free so it can be unit tested standalone.
//
// NOTE (SEC-06 #10): upstream splits this into .h/.cc; here it is header-only
// (inline) so both the nginx module and the gyp unit test share one copy
// with no link changes (see ngx_fetch_body.h).

#ifndef PAGESPEED_NGINX_NGX_INPLACE_URL_AGREEMENT_H_
#define PAGESPEED_NGINX_NGX_INPLACE_URL_AGREEMENT_H_

#include "pagespeed/kernel/base/string_util.h"
#include "pagespeed/kernel/http/google_url.h"

namespace net_instaweb {

// True when the server and the module read the request alike.  `server_uri`
// is the server's normalized request path (percent-decoded, dot segments
// resolved, slash runs merged when the server merges them, no query);
// `server_host` is the host the server matched the request to.
// `module_path` is the path of the URL the module parsed, in its canonical,
// percent-encoded form and without query; `module_host` is that URL's host.
// `merge_slashes` is the server's setting for the request: when set, runs of
// slashes in the module's decoded path are merged as the server merged them.
// The module's path is decoded (and merged) and compared byte for byte with
// the server's; the hosts are compared without regard to case.
inline bool NgxInPlaceUrlAgrees(StringPiece server_uri, StringPiece server_host,
                                StringPiece module_path, StringPiece module_host,
                                bool merge_slashes) {
  if (!StringCaseEqual(server_host, module_host)) {
    return false;
  }
  // The server's path is percent-decoded, the module's canonical path keeps
  // its escapes.  Decoding the escapes of the module's path puts the two in
  // the same form; the one further step the server takes that the module
  // does not is merging runs of slashes, applied here only when the server
  // applied it.  Nothing else -- no separator or dot-segment handling -- so
  // every other normalization on which the two parsers differ shows up as a
  // difference.
  GoogleString path = GoogleUrl::UnescapeIgnorePlus(module_path);
  if (merge_slashes) {
    GoogleString merged;
    merged.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i) {
      const char c = path[i];
      if (c != '/' || merged.empty() || merged[merged.size() - 1] != '/') {
        merged.push_back(c);
      }
    }
    path.swap(merged);
  }
  return path == server_uri;
}

}  // namespace net_instaweb

#endif  // PAGESPEED_NGINX_NGX_INPLACE_URL_AGREEMENT_H_
