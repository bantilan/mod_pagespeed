/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements. See the NOTICE file for details.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by law or agreed in writing, distributed on an "AS IS"
 * BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, express or implied. */
#include "pagespeed/system/history_archive.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>
#include "pagespeed/kernel/base/null_message_handler.h"
#include "pagespeed/kernel/base/time_util.h"
#include "pagespeed/kernel/base/timer.h"
namespace net_instaweb {
namespace {
const int64 kChunkBytes = 256 * 1024;
const int64 kMaxSnapshot = 16 * 1024 * 1024;
struct File { GoogleString name; int64 size, allocated, time; };
struct ScopedClose {
  int* fd;
  ~ScopedClose() { int saved = errno; if (*fd >= 0) close(*fd); errno = saved; }
};
struct ScopedDirectory {
  DIR* dir;
  ~ScopedDirectory() { int saved = errno; closedir(dir); errno = saved; }
};
bool Files(const GoogleString& path, std::vector<File>* files) {
  DIR* dir = opendir(path.c_str());
  if (!dir) return false;
  ScopedDirectory cleanup{dir};
  bool ok = true;
  for (;;) {
    errno = 0;
    dirent* entry = readdir(dir);
    if (entry == nullptr) {
      if (errno != 0) ok = false;
      break;
    }
    GoogleString name(entry->d_name);
    if (name.size() != 32 || name.substr(0, 8) != "segment-" ||
        name.substr(28) != ".psh" ||
        name.substr(8, 20).find_first_not_of("0123456789") != GoogleString::npos)
      continue;
    struct stat st;
    if (lstat((path + "/" + name).c_str(), &st) != 0) {
      if (errno == ENOENT) continue;
      ok = false; break;
    }
    if (!S_ISREG(st.st_mode) || st.st_size > kMaxSnapshot + 4096) {
      errno = EINVAL; ok = false; break;
    }
    files->push_back({name, st.st_size, st.st_blocks * 512, st.st_mtime});
    if (files->size() > 65536) { errno = EOVERFLOW; ok = false; break; }
  }
  std::sort(files->begin(), files->end(),
      [](const File& a, const File& b) { return a.name < b.name; });
  return ok;
}
bool Read(const GoogleString& path, GoogleString* data, int64 limit) {
  int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return false;
  ScopedClose cleanup{&fd};
  struct stat st;
  bool ok = fstat(fd, &st) == 0;
  if (ok && (!S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > limit)) {
    errno = EINVAL;
    ok = false;
  }
  data->clear();
  char buf[8192];
  while (ok) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) { ok = false; break; }
    if (!n) break;
    if (data->size() + n > static_cast<size_t>(limit)) { ok = false; break; }
    data->append(buf, n);
  }
  if (!ok && !errno) errno = EINVAL;
  return ok;
}
bool Atomic(const GoogleString& dir, const GoogleString& name,
            StringPiece data) {
  GoogleString temp = dir + "/.pending";
  // The caller holds the lifetime flock. A stale temporary is never committed.
  if (unlink(temp.c_str()) != 0 && errno != ENOENT) return false;
  int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
                O_NOFOLLOW, 0600);
  if (fd < 0) return false;
  ScopedClose cleanup{&fd};
  bool ok = true;
  while (!data.empty()) {
    ssize_t n = write(fd, data.data(), data.size());
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { ok = false; break; }
    data.remove_prefix(n);
  }
  if (ok) ok = fsync(fd) == 0;
  int error = errno;
  if (close(fd) != 0 && ok) { ok = false; error = errno; }
  fd = -1;
  if (ok && rename(temp.c_str(), (dir + "/" + name).c_str()) != 0) {
    ok = false; error = errno;
  }
  if (ok) {
    int d = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    ok = d >= 0 && fsync(d) == 0;
    error = errno;
    if (d >= 0) close(d);
  }
  if (!ok) unlink(temp.c_str());
  errno = error;
  return ok;
}
GoogleString Name(int64 sequence) {
  return StringPrintf("segment-%020lld.psh", static_cast<long long>(sequence));
}
int64 MessageTime(StringPiece line, int64 fallback) {
  size_t end = line.find("]");
  int64 result;
  if (line.size() > 2 && line[1] == '[' && end != StringPiece::npos &&
      ConvertStringToTime(line.substr(2, end - 2), &result)) return result;
  return fallback;
}
}  // namespace
struct HistoryArchive::State {
  int64 sequence = 0, begin = 0, end = 0, time = 0;
  int64 first_time = 0, last_time = 0;
  GoogleString generation = "0";
  Health health;
  GoogleString Header() const {
    std::ostringstream s;
    s << "PSH1 " << sequence << ' ' << generation << ' ' << begin << ' '
      << end << ' ' << time << ' ' << health.lost_bytes << ' '
      << health.restart_gaps << ' ' << health.records << ' ' << health.failures
      << ' ' << first_time << ' ' << last_time;
    for (int i = 0; i < 5; ++i) s << ' ' << health.categories[i];
    s << '\n';
    return s.str();
  }
  bool Parse(StringPiece data, bool segment) {
    size_t newline = data.find('\n');
    if (newline == StringPiece::npos || newline > 1024) return false;
    std::istringstream s(data.substr(0, newline).as_string());
    GoogleString magic;
    if (!(s >> magic >> sequence >> generation >> begin >> end >> time >>
          health.lost_bytes >> health.restart_gaps >> health.records >> health.failures >>
          first_time >> last_time) ||
        magic != "PSH1" || sequence < 0 || begin < 0 || end < begin || time < 0 ||
        generation.empty() || generation.size() > 20 ||
        generation.find_first_not_of("0123456789") != GoogleString::npos ||
        health.lost_bytes < 0 || health.restart_gaps < 0 || health.records < 0 ||
        health.failures < 0 || first_time < 0 || last_time < 0 ||
        (segment && (sequence == 0 || generation == "0")))
      return false;
    for (int i = 0; i < 5; ++i)
      if (!(s >> health.categories[i]) || health.categories[i] < 0) return false;
    if (segment && (end - begin != data.size() - newline - 1 ||
        data.empty() || data[data.size() - 1] != '\n')) return false;
    health.last_success_ms = time;
    return true;
  }
};
HistoryArchive::HistoryArchive(SharedCircularBuffer* buffer, Timer* timer,
    const GoogleString& cache_path, int interval, int days, int mb)
    : buffer_(buffer), timer_(timer), path_(cache_path + "/history"),
      interval_(interval), retention_days_(days), max_size_mb_(mb) {}
HistoryArchive::~HistoryArchive() { Stop(); }
void HistoryArchive::Start() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (started_) return;
  try { thread_ = std::thread(&HistoryArchive::Run, this); started_ = true; }
  catch (...) { Fail(EAGAIN); }
}
bool HistoryArchive::Schedule(std::function<void()> job) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!started_ || stopped_ || jobs_.size() >= 4) return false;
  jobs_.push_back(std::move(job));
  wake_.notify_one();
  return true;
}
void HistoryArchive::Stop() {
  { std::lock_guard<std::mutex> lock(mutex_); stopped_ = true; }
  wake_.notify_all();
  if (thread_.joinable()) thread_.join();
}
bool HistoryArchive::Fail(int error) {
  health_.error = error ? error : EIO;
  ++health_.failures;
  buffer_->SetHistoryHealth(health_);
  return false;
}
bool HistoryArchive::Prepare() {
  if (interval_ < 1 || interval_ > 86400 || retention_days_ < 1 ||
      retention_days_ > 3650 || max_size_mb_ < 1 || max_size_mb_ > 102400)
    return Fail(EINVAL);
  if (mkdir(path_.c_str(), 0700) != 0 && errno != EEXIST) return Fail(errno);
  struct stat st;
  if (lstat(path_.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return Fail(EINVAL);
  return true;
}
void HistoryArchive::Run() {
  int fd = -1;
  auto delay = std::chrono::seconds(interval_ > 0 ? interval_ : 300);
  auto due = std::chrono::steady_clock::now() + delay;
  for (;;) {
    std::function<void()> job;
    bool stopping;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait_until(lock, due, [this] { return stopped_ || !jobs_.empty(); });
      stopping = stopped_;
      if (stopping && jobs_.empty()) break;
      if (!jobs_.empty()) { job = std::move(jobs_.front()); jobs_.pop_front(); }
    }
    if (!stopping && std::chrono::steady_clock::now() >= due) {
      try {
        if (fd < 0 && Prepare()) {
          fd = open((path_ + "/.writer.lock").c_str(), O_CREAT | O_RDWR |
                    O_CLOEXEC | O_NOFOLLOW, 0600);
          if (fd < 0) Fail(errno);
          else if (flock(fd, LOCK_EX | LOCK_NB) != 0) { close(fd); fd = -1; }
        }
        if (fd >= 0) SnapshotLocked();
      } catch (...) { Fail(ENOMEM); }
      due = std::chrono::steady_clock::now() + delay;
    }
    // Production jobs finish their own AsyncFetch even on rendering failure.
    if (job) { try { job(); } catch (...) {} }
  }
  if (fd >= 0) close(fd);
}
bool HistoryArchive::SnapshotOnce() {
  if (!Prepare()) return false;
  int fd = open((path_ + "/.writer.lock").c_str(), O_CREAT | O_RDWR |
                O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) return Fail(errno);
  if (flock(fd, LOCK_EX | LOCK_NB) != 0) { close(fd); return false; }
  bool ok = SnapshotLocked();
  close(fd);
  return ok;
}
bool HistoryArchive::Load(State* state) {
  GoogleString data;
  if (Read(path_ + "/checkpoint", &data, 4096)) {
    if (!state->Parse(data, false)) return Fail(EINVAL);
  } else if (errno != ENOENT) return Fail(errno);
  std::vector<File> files;
  if (!Files(path_, &files)) return Fail(errno);
  // Recover a committed segment whose checkpoint rename was interrupted.
  if (!files.empty()) {
    int64 last_sequence;
    if (!StringToInt64(StringPiece(files.back().name).substr(8, 20),
                       &last_sequence)) return Fail(EINVAL);
    if (last_sequence <= state->sequence) return true;
    State last;
    if (!Read(path_ + "/" + files.back().name, &data, kMaxSnapshot + 4096) ||
        !last.Parse(data, true) || Name(last.sequence) != files.back().name)
      return Fail(EINVAL);
    if (last.sequence > state->sequence) *state = last;
  }
  return true;
}
bool HistoryArchive::Save(const State& state) {
  return Persist("checkpoint", state.Header()) || Fail(errno);
}
bool HistoryArchive::Persist(const GoogleString& name, StringPiece data) {
  return Atomic(path_, name, data);
}
bool HistoryArchive::Prune(int64 reserve, int64* bytes) {
  std::vector<File> files;
  if (!Files(path_, &files)) return Fail(errno);
  *bytes = 0;
  for (const File& f : files) *bytes += f.allocated;
  for (const char* name : {"", "/checkpoint", "/.writer.lock"}) {
    struct stat st;
    if (lstat((path_ + name).c_str(), &st) == 0) *bytes += st.st_blocks * 512;
  }
  struct statvfs fs;
  if (statvfs(path_.c_str(), &fs) != 0) return Fail(errno);
  int64 unit = fs.f_frsize ? fs.f_frsize : 4096;
  if (reserve) reserve = ((reserve + unit - 1) / unit) * unit + 2 * unit;
  const int64 limit = static_cast<int64>(max_size_mb_) * 1024 * 1024;
  if (reserve > limit) return Fail(EFBIG);
  const int64 cutoff = timer_->NowMs() / 1000 -
      static_cast<int64>(retention_days_) * 86400;
  for (const File& f : files) {
    if (f.time < cutoff || *bytes + reserve > limit) {
      if (unlink((path_ + "/" + f.name).c_str()) != 0 && errno != ENOENT)
        return Fail(errno);
      *bytes -= f.allocated;
    }
  }
  if (*bytes + reserve > limit) return Fail(EFBIG);
  return true;
}
bool HistoryArchive::SnapshotLocked() {
  if (unlink((path_ + "/.pending").c_str()) != 0 && errno != ENOENT)
    return Fail(errno);
  State state;
  if (!Load(&state)) return false;
  const int64 failures = health_.failures;
  health_ = state.health;
  health_.failures = std::max(failures, state.health.failures);
  SharedCircularBuffer::HistorySnapshot snapshot;
  NullMessageHandler handler;
  if (!buffer_->SnapshotHistory(&snapshot, &handler)) return Fail(EINVAL);
  int64 start = state.generation == snapshot.generation ? state.end : 0;
  if (state.generation != snapshot.generation) state.last_time = 0;
  if (state.generation != "0" && state.generation != snapshot.generation)
    ++health_.restart_gaps;  // Unknown tail loss across shared-memory generations.
  if (start > snapshot.end) return Fail(EINVAL);
  if (start < snapshot.begin) {
    state.last_time = 0;
    health_.lost_bytes += snapshot.begin - start;
    start = snapshot.begin;
    // The ring may start halfway through a line; never archive it as a record.
    size_t newline = snapshot.data.find('\n');
    if (newline != GoogleString::npos) {
      health_.lost_bytes += newline + 1;
      start += newline + 1;
    } else {
      health_.lost_bytes += snapshot.data.size();
      start = snapshot.end;
    }
  }
  state.generation = snapshot.generation;
  state.end = start;
  size_t position = start - snapshot.begin;
  const char* kinds[] = {"messages", "images", "html_cache", "javascript", "css"};
  while (position < snapshot.data.size()) {
    size_t target = std::min(snapshot.data.size(), position + kChunkBytes);
    size_t newline = snapshot.data.rfind('\n', target - 1);
    if (newline == GoogleString::npos || newline < position)
      newline = snapshot.data.find('\n', target);
    if (newline == GoogleString::npos) break;
    GoogleString body = snapshot.data.substr(position, newline + 1 - position);
    Health committed = health_;
    state.first_time = state.last_time;
    for (size_t begin = 0; begin < body.size();) {
      size_t end = body.find('\n', begin);
      StringPiece line(body.data() + begin, end - begin);
      state.last_time = MessageTime(line, state.last_time);
      ++health_.records;
      for (int k = 0; k < 5; ++k)
        if (Matches(kinds[k], line)) ++health_.categories[k];
      begin = end + 1;
    }
    if (state.sequence == 9223372036854775807LL) return Fail(EOVERFLOW);
    ++state.sequence;
    state.begin = snapshot.begin + position;
    state.end = snapshot.begin + newline + 1;
    state.time = timer_->NowMs();
    state.health = health_;
    GoogleString data = state.Header() + body;
    int64 bytes;
    if (!Prune(data.size(), &bytes)) {
      health_.records = committed.records;
      for (int i = 0; i < 5; ++i) health_.categories[i] = committed.categories[i];
      buffer_->SetHistoryHealth(health_);
      return false;
    }
    if (!Persist(Name(state.sequence), data)) {
      int error = errno;
      health_ = committed;
      return Fail(error);
    }
    // Persist a recoverable cursor before any subsequent retention pass.
    if (!Save(state)) return false;
    position = newline + 1;
  }
  state.begin = state.end;
  state.time = timer_->NowMs();
  state.health = health_;
  if (!Save(state)) return false;
  int64 bytes;
  if (!Prune(0, &bytes)) return false;
  health_.last_success_ms = state.time;
  health_.disk_bytes = bytes;
  health_.error = 0;
  buffer_->SetHistoryHealth(health_);
  return true;
}
bool HistoryArchive::Matches(StringPiece kind, StringPiece message) {
  if (kind == "messages") return true;
  if (kind == "images") return message.find("PS_IMAGE_DETAIL\t") != StringPiece::npos;
  if (kind == "html_cache") return message.find("PS_HTML_DETAIL\t") != StringPiece::npos;
  if (kind == "javascript") return
      message.find("Javascript minification failed.") != StringPiece::npos ||
      message.find("Canonical script ") != StringPiece::npos ||
      (message.find("Script ") != StringPiece::npos &&
       (message.find(" didn't shrink.") != StringPiece::npos ||
        message.find(" is unsafe to replace.") != StringPiece::npos)) ||
      message.find("javascript_filter.cc:") != StringPiece::npos ||
      message.find("javascript_code_block.cc:") != StringPiece::npos;
  if (kind == "css") return message.find("CSS ") != StringPiece::npos ||
      message.find("css_filter.cc:") != StringPiece::npos;
  return false;
}
HistoryArchive::Page HistoryArchive::Browse(const GoogleString& cache_path,
    StringPiece kind, int64 from_ms, int64 until_ms, StringPiece cursor) {
  Page page;
  GoogleString path = cache_path + "/history";
  std::vector<File> files;
  if (!Files(path, &files)) {
    page.error = errno == ENOENT ? "No snapshots have been created yet" : "Archive unavailable";
    return page;
  }
  int64 before_sequence = 0, before_offset = 0;
  if (!cursor.empty()) {
    StringPieceVector parts;
    SplitStringPieceToVector(cursor, ":", &parts, false);
    if (parts.size() != 2 || !StringToInt64(parts[0], &before_sequence) ||
        !StringToInt64(parts[1], &before_offset) || before_sequence <= 0 ||
        before_offset < 0) { page.error = "Invalid page cursor"; return page; }
  }
  // Prefix preserves the existing parser's first-potentially-partial-line rule.
  page.log = "\n";
  std::vector<GoogleString> selected;
  size_t selected_bytes = 0;
  int scanned = 0;
  for (auto it = files.rbegin(); it != files.rend(); ++it) {
    if (before_sequence && it->name > Name(before_sequence)) continue;
    GoogleString data;
    if (!Read(path + "/" + it->name, &data, kMaxSnapshot + 4096)) {
      if (errno == ENOENT) continue;  // Concurrent retention.
      page.error = "Archive read failed"; return page;
    }
    State state;
    if (!state.Parse(data, true) || Name(state.sequence) != it->name) {
      page.error = "Invalid archive segment"; return page;
    }
    size_t header = data.find('\n') + 1;
    size_t end = data.size();
    size_t time_boundary = data.size();
    int64 record_time = state.first_time;
    if (before_sequence == state.sequence) {
      if (before_offset < state.begin || before_offset > state.end) {
        page.error = "Invalid page cursor"; return page;
      }
      end = header + before_offset - state.begin;
      if (end > header && data[end - 1] != '\n') {
        page.error = "Invalid page cursor"; return page;
      }
    }
    while (end > header) {
      size_t previous = end > header + 1 ? data.rfind('\n', end - 2) : GoogleString::npos;
      size_t begin = previous == GoogleString::npos || previous < header ? header : previous + 1;
      StringPiece line(data.data() + begin, end - begin - 1);
      // Reverse pagination inherits the original timestamp of multiline
      // continuations, never the time of the archival snapshot.
      if (begin < time_boundary) {
        record_time = state.first_time;
        time_boundary = header;
        size_t search = begin;
        for (;;) {
          size_t stop = data.find('\n', search);
          int64 found = MessageTime(StringPiece(data.data() + search,
                                               stop - search), -1);
          if (found >= 0) { record_time = found; time_boundary = search; break; }
          if (search == header) break;
          size_t p = search > header + 1 ? data.rfind('\n', search - 2) : GoogleString::npos;
          search = p == GoogleString::npos || p < header ? header : p + 1;
        }
      }
      if ((record_time > 0 || (from_ms == 0 && until_ms == 253402300800000LL)) &&
          record_time >= from_ms && record_time < until_ms && Matches(kind, line)) {
        if (selected.size() >= 100 || (!selected.empty() &&
            selected_bytes + line.size() > 256 * 1024)) {
          page.next = StrCat(Integer64ToString(state.sequence), ":",
              Integer64ToString(state.begin + end - header));
          goto done;
        }
        selected.push_back(line.as_string() + "\n");
        selected_bytes += line.size();
      }
      end = begin;
    }
    if (++scanned >= 256 && it + 1 != files.rend()) {
      page.next = StrCat(Integer64ToString(state.sequence), ":",
                         Integer64ToString(state.begin));
      break;
    }
  }
 done:
  for (auto it = selected.rbegin(); it != selected.rend(); ++it) page.log += *it;
  return page;
}
}  // namespace net_instaweb
