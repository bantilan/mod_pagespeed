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

#include "pagespeed/kernel/sharedmem/shared_circular_buffer.h"

#include <cstddef>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <new>

#include "pagespeed/kernel/base/abstract_mutex.h"
#include "pagespeed/kernel/base/abstract_shared_mem.h"
#include "pagespeed/kernel/base/circular_buffer.h"
#include "pagespeed/kernel/base/message_handler.h"
#include "pagespeed/kernel/base/scoped_ptr.h"
#include "pagespeed/kernel/base/string.h"
#include "pagespeed/kernel/base/string_util.h"
#include "pagespeed/kernel/base/writer.h"

namespace {
const char kSharedCircularBufferObjName[] = "SharedCircularBuffer";
}  // namespace

namespace net_instaweb {

struct SharedCircularBuffer::HistoryState {
  uint64 generation;
  int64 end;
  HistoryHealth health;
};

SharedCircularBuffer::SharedCircularBuffer(AbstractSharedMem* shm_runtime,
                                           const int buffer_capacity,
                                           const GoogleString& filename_prefix,
                                           const GoogleString& filename_suffix,
                                           bool history_tracking)
    : shm_runtime_(shm_runtime),
      buffer_capacity_(buffer_capacity),
      buffer_(nullptr),
      filename_prefix_(filename_prefix),
      filename_suffix_(filename_suffix), history_tracking_(history_tracking),
      history_state_(nullptr) {}

SharedCircularBuffer::~SharedCircularBuffer() {}

// Initialize shared mutex.
bool SharedCircularBuffer::InitMutex(MessageHandler* handler) {
  if (!segment_->InitializeSharedMutex(0, handler)) {
    handler->Message(
        kError, "Unable to create mutex for shared memory circular buffer");
    return false;
  }
  return true;
}

bool SharedCircularBuffer::InitSegment(bool parent, MessageHandler* handler) {
  // A capacity that cannot produce a usable buffer is refused before any
  // size is computed or any segment created, so an out-of-range value is
  // never narrowed into a bogus segment size and no segment is left behind
  // on the failure path.
  if (!CircularBuffer::IsValidCapacity(buffer_capacity_)) {
    handler->Message(kError, "Invalid buffer capacity: %d", buffer_capacity_);
    return false;
  }
  // Size of segment includes mutex and circular buffer.
  int buffer_size = CircularBuffer::Sizeof(buffer_capacity_);
  const size_t history_size = history_tracking_ ? sizeof(HistoryState) : 0;
  size_t total = shm_runtime_->SharedMutexSize() + history_size + buffer_size;
  if (parent) {
    // In root process -> initialize the shared memory.
    segment_.reset(shm_runtime_->CreateSegment(SegmentName(), total, handler));
    if (segment_.get() == nullptr) {
      return false;
    }
    // Initialize mutex.
    if (!InitMutex(handler)) {
      segment_.reset(nullptr);
      shm_runtime_->DestroySegment(SegmentName(), handler);
      return false;
    }
  } else {
    // In child process -> attach to existing segment.
    segment_.reset(
        shm_runtime_->AttachToSegment(SegmentName(), total, handler));
    if (segment_.get() == nullptr) {
      return false;
    }
  }
  // Attach Mutex.
  mutex_.reset(segment_->AttachToSharedMutex(0));
  // Initialize the circular buffer.
  int pos = shm_runtime_->SharedMutexSize();
  if (history_tracking_) {
    history_state_ = reinterpret_cast<HistoryState*>(
        const_cast<char*>(segment_->Base() + pos));
    if (parent) {
      uint64 generation = 0;
      int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
      bool ok = fd >= 0 && read(fd, &generation, sizeof(generation)) ==
          sizeof(generation);
      if (fd >= 0) close(fd);
      if (!ok || generation == 0) return false;
      new (history_state_) HistoryState();
      history_state_->generation = generation;
    }
    pos += history_size;
  }
  buffer_ = CircularBuffer::Init(
      parent, static_cast<void*>(const_cast<char*>(segment_->Base() + pos)),
      buffer_size, buffer_capacity_);
  if (buffer_ == nullptr) {
    // Unreachable for a validated capacity, but if the buffer cannot be
    // initialized, do not leave a parent-created segment registered under
    // our name: destroy what was created so the name stays clean.
    if (parent) {
      segment_.reset(nullptr);
      shm_runtime_->DestroySegment(SegmentName(), handler);
    } else {
      segment_.reset(nullptr);
    }
    return false;
  }
  return true;
}

void SharedCircularBuffer::Clear() {
  ScopedMutex hold_lock(mutex_.get());
  buffer_->Clear();
  if (history_state_ != nullptr) {
    ++history_state_->generation;
    history_state_->end = 0;
  }
}

bool SharedCircularBuffer::Write(const StringPiece& message,
                                 MessageHandler* handler) {
  ScopedMutex hold_lock(mutex_.get());
  bool ok = buffer_->Write(message);
  if (ok && history_state_ != nullptr) history_state_->end += message.size();
  return ok;
}

bool SharedCircularBuffer::SnapshotHistory(HistorySnapshot* snapshot,
                                           MessageHandler* handler) {
  if (history_state_ == nullptr || buffer_capacity_ > 16 * 1024 * 1024)
    return false;
  ScopedMutex lock(mutex_.get());
  snapshot->generation = std::to_string(history_state_->generation);
  snapshot->end = history_state_->end;
  snapshot->data = buffer_->ToString(handler);
  snapshot->begin = snapshot->end - snapshot->data.size();
  return true;
}

bool SharedCircularBuffer::GetHistoryHealth(HistoryHealth* health) {
  if (history_state_ == nullptr) return false;
  ScopedMutex lock(mutex_.get());
  *health = history_state_->health;
  return true;
}

void SharedCircularBuffer::SetHistoryHealth(const HistoryHealth& health) {
  if (history_state_ == nullptr) return;
  ScopedMutex lock(mutex_.get());
  history_state_->health = health;
}

bool SharedCircularBuffer::Dump(Writer* writer, MessageHandler* handler) {
  ScopedMutex hold_lock(mutex_.get());
  return (writer->Write(buffer_->ToString(handler), handler));
}

GoogleString SharedCircularBuffer::ToString(MessageHandler* handler) {
  ScopedMutex hold_lock(mutex_.get());
  return buffer_->ToString(handler);
}

void SharedCircularBuffer::GlobalCleanup(MessageHandler* handler) {
  if (segment_.get() != nullptr) {
    shm_runtime_->DestroySegment(SegmentName(), handler);
  }
}

GoogleString SharedCircularBuffer::SegmentName() const {
  return StrCat(filename_prefix_, kSharedCircularBufferObjName, ".",
                filename_suffix_);
}

}  // namespace net_instaweb
