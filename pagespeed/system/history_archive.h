/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements. See the NOTICE file for details.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by law or agreed in writing, distributed on an "AS IS"
 * BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, express or implied. */
#ifndef PAGESPEED_SYSTEM_HISTORY_ARCHIVE_H_
#define PAGESPEED_SYSTEM_HISTORY_ARCHIVE_H_
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include "pagespeed/kernel/sharedmem/shared_circular_buffer.h"
namespace net_instaweb {
class Timer;
class HistoryArchive {
 public:
  using Health = SharedCircularBuffer::HistoryHealth;
  struct Page {
    GoogleString log, next, error;
  };
  HistoryArchive(SharedCircularBuffer* buffer, Timer* timer,
                 const GoogleString& cache_path, int interval,
                 int retention_days, int max_size_mb);
  virtual ~HistoryArchive();
  void Start();
  void Stop();
  // Admin reads share this background thread, never the HTTP event loop.
  bool Schedule(std::function<void()> job);
  // Tests and standalone maintenance: nonblocking single-writer lock.
  bool SnapshotOnce();
  static bool Matches(StringPiece kind, StringPiece message);
  static GoogleString FilterLog(StringPiece log, int64 from_ms, int64 until_ms);
  static Page Browse(const GoogleString& cache_path, StringPiece kind,
                     int64 from_ms, int64 until_ms, StringPiece cursor,
                     const SharedCircularBuffer::HistorySnapshot* live = nullptr);
 protected:
  virtual bool Persist(const GoogleString& name, StringPiece data);
 private:
  struct State;
  void Run();
  bool Prepare();
  bool SnapshotLocked();
  bool Fail(int error);
  bool Load(State* state);
  bool RestoreHealth();
  bool Save(const State& state);
  bool Prune(int64 reserve, int64* bytes);
  SharedCircularBuffer* buffer_;
  Timer* timer_;
  GoogleString path_;
  int interval_, retention_days_, max_size_mb_;
  Health health_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stopped_ = false;
  bool started_ = false;
  std::deque<std::function<void()>> jobs_;
  std::thread thread_;
};
}  // namespace net_instaweb
#endif
