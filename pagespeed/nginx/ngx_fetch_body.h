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
// Body accounting for the native fetcher (NgxFetch): how many newly read
// bytes belong to the response, and whether a response whose connection
// closed is complete.
//
// Deliberately nginx-free so it can be unit tested standalone.
//
// NOTE (SEC-06 #4): upstream splits this into .h/.cc; here it is header-only
// (inline) so both the nginx module (which compiles wrapper sources from
// ngx_pagespeed/src and cannot gain a new translation unit without build
// surgery) and the gyp unit test can share one copy with no link changes.

#ifndef PAGESPEED_NGINX_NGX_FETCH_BODY_H_
#define PAGESPEED_NGINX_NGX_FETCH_BODY_H_

#include <cstddef>

#include "pagespeed/kernel/base/basictypes.h"

namespace net_instaweb {

struct NgxFetchBodyStep {
  // How many of the newly read bytes are part of the response body.
  size_t accept;
  // True when the body is now complete.
  bool complete;
  // True when more bytes arrived than the response announced.
  bool overflow;
};

// Accounts for `available` newly read body bytes, given the bytes already
// received.  When the content length is unknown, every byte is accepted
// and the body completes only when the connection closes.
inline NgxFetchBodyStep NgxFetchAcceptBody(bool content_length_known,
                                           int64 content_length,
                                           int64 bytes_received,
                                           size_t available) {
  NgxFetchBodyStep step;
  step.accept = available;
  step.complete = false;
  step.overflow = false;
  if (!content_length_known) {
    return step;
  }
  // Bytes past the announced length are never part of the response.
  int64 remaining = content_length - bytes_received;
  if (remaining < 0) {
    remaining = 0;
  }
  if (static_cast<uint64>(remaining) < available) {
    step.accept = static_cast<size_t>(remaining);
    step.overflow = true;
  }
  step.complete =
      bytes_received + static_cast<int64>(step.accept) == content_length;
  return step;
}

// Whether a response to `status_code` (to a request that may have been a
// HEAD) carries no body: a HEAD response announces the length of a body it
// does not send, so it is complete with its headers, like a 204 or 304.
inline bool NgxFetchResponseHasNoBody(int status_code, bool head_request) {
  return head_request || status_code == 304 || status_code == 204;
}

// Whether a response is complete when the origin closes the connection.
// With an announced content length the body must be exactly that long;
// without one, a response whose headers parsed is complete at close.
inline bool NgxFetchCompleteOnClose(bool headers_complete,
                                    bool content_length_known,
                                    int64 content_length,
                                    int64 bytes_received) {
  // An announced length is a promise: a body that closes short of it (or
  // ran past it) is not a complete response.
  if (content_length_known) {
    return headers_complete && content_length == bytes_received;
  }
  return headers_complete;
}

}  // namespace net_instaweb

#endif  // PAGESPEED_NGINX_NGX_FETCH_BODY_H_
