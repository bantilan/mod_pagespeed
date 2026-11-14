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
// Loading a client request body that nginx spooled to a temporary file into
// one contiguous buffer, for the beacon and admin POST handlers.
//
// Deliberately nginx-free so it can be unit tested standalone: the file read
// and the pool allocation are passed in.
//
// NOTE (SEC-06 #5): upstream splits this into .h/.cc; here it is header-only
// (inline) so both the nginx module and the gyp unit test share one copy
// with no link changes (see ngx_fetch_body.h).

#ifndef PAGESPEED_NGINX_NGX_REQUEST_BODY_H_
#define PAGESPEED_NGINX_NGX_REQUEST_BODY_H_

#include <cstddef>
#include <cstring>
#include <functional>

#include "pagespeed/kernel/base/basictypes.h"
#include "pagespeed/kernel/base/string.h"
#include "pagespeed/kernel/base/string_util.h"

namespace net_instaweb {

// The largest request body the module loads into memory, matching the
// Apache port's limit for the same handlers.
const size_t kNgxMaxRequestBodyBytes = 131072;

enum NgxRequestBodyStatus {
  kNgxRequestBodyOk,
  kNgxRequestBodyReadError,
  kNgxRequestBodyTooLarge,
};

// Reads up to `size` bytes at `offset` into `buf`: returns the number of
// bytes read, 0 at the end of the body, or a negative value on error.
typedef std::function<int64(char* buf, size_t size, int64 offset)>
    NgxRequestBodyReader;

// Reads the whole body through `read` into `*body`, never holding more than
// `max_bytes` of it.
inline NgxRequestBodyStatus NgxReadRequestBody(const NgxRequestBodyReader& read,
                                               size_t max_bytes,
                                               GoogleString* body) {
  const size_t kReadChunkBytes = 65536;
  char buf[65536];
  int64 offset = 0;
  int64 ret;
  while ((ret = read(buf, kReadChunkBytes, offset)) > 0) {
    if (static_cast<uint64>(ret) > kReadChunkBytes) {
      return kNgxRequestBodyReadError;
    }
    // Stop before holding more than max_bytes, rather than spooling the
    // whole file into memory first.
    if (static_cast<uint64>(ret) > max_bytes - body->size()) {
      return kNgxRequestBodyTooLarge;
    }
    body->append(buf, static_cast<size_t>(ret));
    offset += ret;
  }
  if (ret < 0) {
    return kNgxRequestBodyReadError;
  }
  return kNgxRequestBodyOk;
}

// Copies `body` into memory from `alloc` and points `*out` at it.  Returns
// false, leaving `*out` untouched, when the allocation fails.
inline bool NgxCopyRequestBody(StringPiece body,
                               const std::function<void*(size_t)>& alloc,
                               StringPiece* out) {
  char* data = static_cast<char*>(alloc(body.size()));
  if (data == nullptr) {
    return false;
  }
  if (!body.empty()) {
    memcpy(data, body.data(), body.size());
  }
  *out = StringPiece(data, body.size());
  return true;
}

}  // namespace net_instaweb

#endif  // PAGESPEED_NGINX_NGX_REQUEST_BODY_H_
