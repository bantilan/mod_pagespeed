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
// The record of the gzip configuration NgxGZipSetter wrote, so it can undo
// it when the user configured gzip explicitly.
//
// The setup writes into one configuration cycle's memory and rolls back out
// of it, so the record is per-cycle: BeginCycle() starts it empty, the
// Record*() calls log every slot the setup writes, and RollBack() resets
// every logged slot through nginx callbacks and empties the record.  A cycle
// whose setup rolled back stays disabled; a new cycle re-arms the setup when
// the gzip module is available in it.  Without the per-cycle reset, slots
// recorded during one configuration would be written again (through freed
// memory) when a later reload rolls back.
//
// Deliberately nginx-free so it can be unit tested standalone.
//
// NOTE (SEC-06 #8): upstream splits this into .h/.cc; here it is header-only
// (inline) so both the nginx module and the gyp unit test share one copy
// with no link changes (see ngx_fetch_body.h).

#ifndef PAGESPEED_NGINX_NGX_GZIP_ROLLBACK_H_
#define PAGESPEED_NGINX_NGX_GZIP_ROLLBACK_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace net_instaweb {

class NgxGZipRollbackLog {
 public:
  NgxGZipRollbackLog() : enabled_(false) {}

  // Starts the record for a configuration cycle, dropping whatever an
  // earlier cycle recorded.  The automatic setup is armed when the gzip
  // module is available in the new cycle.
  void BeginCycle(bool gzip_available) {
    // Slots recorded earlier point into a configuration that is being
    // replaced (and is freed once the new one is in place).
    flags_.clear();
    uints_.clear();
    types_.clear();
    enabled_ = gzip_available;
  }

  // Whether the automatic setup may write in this cycle: available, and not
  // rolled back after an explicit gzip directive.
  bool enabled() const { return enabled_; }

  void RecordFlag(intptr_t* slot) { flags_.push_back(slot); }
  void RecordUint(uintptr_t* slot) { uints_.push_back(slot); }
  void RecordTypes(void* slot) { types_.push_back(slot); }

  // How many slots are recorded.  Tests only.
  size_t recorded() const {
    return flags_.size() + uints_.size() + types_.size();
  }

  // Resets every recorded slot through the given callbacks and empties the
  // record.  Disables the automatic setup for the rest of the cycle.
  void RollBack(const std::function<void(intptr_t*)>& reset_flag,
                const std::function<void(uintptr_t*)>& reset_uint,
                const std::function<void(void*)>& reset_types) {
    for (size_t i = 0; i < flags_.size(); ++i) {
      reset_flag(flags_[i]);
    }
    for (size_t i = 0; i < uints_.size(); ++i) {
      reset_uint(uints_[i]);
    }
    for (size_t i = 0; i < types_.size(); ++i) {
      reset_types(types_[i]);
    }
    flags_.clear();
    uints_.clear();
    types_.clear();
    enabled_ = false;
  }

 private:
  // Slots the automatic setup wrote in this cycle.
  std::vector<intptr_t*> flags_;
  std::vector<uintptr_t*> uints_;
  std::vector<void*> types_;
  bool enabled_;
};

}  // namespace net_instaweb

#endif  // PAGESPEED_NGINX_NGX_GZIP_ROLLBACK_H_
