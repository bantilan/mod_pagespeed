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

//
// Unit tests for SystemMessageHandler

#include "pagespeed/system/system_message_handler.h"
#include "pagespeed/kernel/base/gtest.h"
#include "pagespeed/kernel/base/mock_timer.h"
#include "pagespeed/kernel/base/scoped_ptr.h"
#include "pagespeed/kernel/base/string.h"
#include "pagespeed/kernel/base/string_writer.h"
#include "pagespeed/kernel/base/thread_system.h"
#include "pagespeed/kernel/util/platform.h"

namespace net_instaweb {

class SystemMessageHandlerTest : public testing::Test {
 protected:
  SystemMessageHandlerTest()
      : thread_system_(Platform::CreateThreadSystem()),
        timer_(thread_system_->NewMutex(), MockTimer::kApr_5_2010_ms),
        system_message_handler_(&timer_, thread_system_->NewMutex()),
        writer_(&buffer_) {
    system_message_handler_.set_buffer(&writer_);
    system_message_handler_.SetPidString(1234);
  }

  void AddMessage(MessageType type, StringPiece msg) {
    system_message_handler_.AddMessageToBuffer(type, msg);
  }

  void AddMessage(MessageType type, const char* file, int line,
                  StringPiece msg) {
    system_message_handler_.AddMessageToBuffer(type, file, line, msg);
  }

  scoped_ptr<ThreadSystem> thread_system_;
  MockTimer timer_;
  SystemMessageHandler system_message_handler_;
  GoogleString buffer_;
  StringWriter writer_;
};

// Tests that multi-line messages are annotated with the type-code for
// each line.
TEST_F(SystemMessageHandlerTest, WrapLongLinesError) {
  AddMessage(kError, "Now is the time\nfor all good men\nto come to the aid");
  EXPECT_STREQ(
      "E[Mon, 05 Apr 2010 18:51:26 GMT] [Error] [1234] Now is the time\n"
      "Efor all good men\n"
      "Eto come to the aid\n",
      buffer_);
}

TEST_F(SystemMessageHandlerTest, WrapLongLinesInfo) {
  AddMessage(kInfo, "Now is the time\nfor all good men\nto come to the aid");
  EXPECT_STREQ(
      "I[Mon, 05 Apr 2010 18:51:26 GMT] [Info] [1234] Now is the time\n"
      "Ifor all good men\n"
      "Ito come to the aid\n",
      buffer_);
}

TEST_F(SystemMessageHandlerTest, WrapLongLinesWarning) {
  AddMessage(kWarning, "Now is the time\nfor all good men\nto come to the aid");
  EXPECT_STREQ(
      "W[Mon, 05 Apr 2010 18:51:26 GMT] [Warning] [1234] Now is the time\n"
      "Wfor all good men\n"
      "Wto come to the aid\n",
      buffer_);
}

TEST_F(SystemMessageHandlerTest, AddsFileLineInfo) {
  AddMessage(kInfo, "test_file.cc", 4321, "Test message");
  EXPECT_STREQ(
      "I[Mon, 05 Apr 2010 18:51:26 GMT] [Info] [1234] "
      "[test_file.cc:4321] Test message\n",
      buffer_);
}

TEST_F(SystemMessageHandlerTest, AddsFileLineInfoWrapLongLines) {
  AddMessage(kInfo, "test_file.cc", 4321, "Test message with\nnew line.");
  EXPECT_STREQ(
      "I[Mon, 05 Apr 2010 18:51:26 GMT] [Info] [1234] "
      "[test_file.cc:4321] Test message with\n"
      "Inew line.\n",
      buffer_);
}

}  // namespace net_instaweb

// Archive tests reuse the existing message transport; no rewrite-event producers.
#include <cerrno>
#include "pagespeed/kernel/base/null_message_handler.h"
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <future>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include "pagespeed/kernel/sharedmem/inprocess_shared_mem.h"
#include "pagespeed/kernel/sharedmem/shared_circular_buffer.h"
#include "pagespeed/system/history_archive.h"
namespace net_instaweb {
namespace {
class FaultArchive : public HistoryArchive {
 public:
  using HistoryArchive::HistoryArchive;
  int fault = 0;
  bool checkpoint_only = false;
 protected:
  bool Persist(const GoogleString& name, StringPiece data) override {
    if (fault && (!checkpoint_only || name == "checkpoint")) {
      errno = fault;
      return false;
    }
    return HistoryArchive::Persist(name, data);
  }
};
class HistoryArchiveTest : public testing::Test {
 protected:
  HistoryArchiveTest()
      : threads_(Platform::CreateThreadSystem()),
        timer_(threads_->NewMutex(), MockTimer::kApr_5_2010_ms),
        shm_(threads_.get()), buffer_(&shm_, 4096, "history-test", "records", true),
        plain_(&shm_, 4096, "history-test", "plain") {}
  void SetUp() override {
    char name[] = "/tmp/pagespeed-history-test-XXXXXX";
    char* dir = mkdtemp(name);
    ASSERT_NE(nullptr, dir);
    path_ = dir;
    ASSERT_TRUE(buffer_.InitSegment(true, &handler_));
    ASSERT_TRUE(plain_.InitSegment(true, &handler_));
    archive_.reset(new FaultArchive(&buffer_, &timer_, path_, 300, 7, 1));
  }
  void TearDown() override {
    archive_.reset();
    DIR* dir = opendir((path_ + "/history").c_str());
    if (dir) {
      while (dirent* e = readdir(dir)) {
        if (e->d_name[0] == '.' && (e->d_name[1] == '\0' ||
            (e->d_name[1] == '.' && e->d_name[2] == '\0'))) continue;
        unlink((path_ + "/history/" + e->d_name).c_str());
      }
      closedir(dir);
    }
    rmdir((path_ + "/history").c_str());
    rmdir(path_.c_str());
    buffer_.GlobalCleanup(&handler_);
    plain_.GlobalCleanup(&handler_);
  }
  void Write(StringPiece text) {
    GoogleString line = StrCat("I[Mon, 05 Apr 2010 18:51:26 GMT] [Info] [1] ", text, "\n");
    ASSERT_TRUE(buffer_.Write(line, &handler_));
  }
  HistoryArchive::Page Page(StringPiece kind = "messages", StringPiece cursor = "") {
    return HistoryArchive::Browse(path_, kind, 0, 253402300800000LL, cursor);
  }
  SharedCircularBuffer::HistoryHealth Health() {
    SharedCircularBuffer::HistoryHealth h;
    EXPECT_TRUE(buffer_.GetHistoryHealth(&h));
    return h;
  }
  int Count(StringPiece text, StringPiece needle) {
    int n = 0;
    for (size_t p = 0; (p = text.find(needle, p)) != StringPiece::npos; p += needle.size()) ++n;
    return n;
  }
  std::unique_ptr<ThreadSystem> threads_;
  MockTimer timer_;
  InProcessSharedMem shm_;
  SharedCircularBuffer buffer_, plain_;
  NullMessageHandler handler_;
  GoogleString path_;
  std::unique_ptr<FaultArchive> archive_;
};
TEST_F(HistoryArchiveTest, DisabledTrackingDoesNotChangeLiveBuffer) {
  plain_.Write("identical memory log", &handler_);
  SharedCircularBuffer::HistorySnapshot snapshot;
  EXPECT_FALSE(plain_.SnapshotHistory(&snapshot, &handler_));
  EXPECT_EQ("identical memory log", plain_.ToString(&handler_));
  EXPECT_EQ(-1, access((path_ + "/history").c_str(), F_OK));
}
TEST_F(HistoryArchiveTest, StoresAllFiveViewsOnceAndKeepsExactMemoryText) {
  Write("ordinary message");
  Write("PS_IMAGE_DETAIL\t100\t200\t100\timage/webp\thttp://a/x\thttp://a/y");
  Write("PS_HTML_DETAIL\t100\t200\t20\tdesktop\tAVIF capable\tvariant\turl");
  Write("Script http://a/a.js didn't shrink.");
  Write("CSS parsing error in http://a/a.css");
  GoogleString before = buffer_.ToString(&handler_);
  ASSERT_TRUE(archive_->SnapshotOnce());
  ASSERT_TRUE(archive_->SnapshotOnce());
  EXPECT_EQ(before, buffer_.ToString(&handler_));
  EXPECT_EQ("\n" + before, Page().log);
  EXPECT_EQ(5, Health().records);
  const char* kinds[] = {"images", "html_cache", "javascript", "css"};
  for (const char* kind : kinds) EXPECT_EQ(1, Count(Page(kind).log, "[Info]"));
  EXPECT_EQ(5, Health().categories[0]);
}
TEST_F(HistoryArchiveTest, IdenticalMessagesAndTimestampsRemainDistinctEvents) {
  Write("same event text");
  ASSERT_TRUE(archive_->SnapshotOnce());
  Write("same event text");
  ASSERT_TRUE(archive_->SnapshotOnce());
  ASSERT_TRUE(archive_->SnapshotOnce());
  EXPECT_EQ(2, Count(Page().log, "same event text"));
  EXPECT_EQ(2, Health().records);
}
TEST_F(HistoryArchiveTest, OverwriteReportsLostBytesWithoutClearingMemory) {
  Write("first");
  ASSERT_TRUE(archive_->SnapshotOnce());
  for (int i = 0; i < 200; ++i) Write(StrCat("overflow-", IntegerToString(i)));
  GoogleString before = buffer_.ToString(&handler_);
  ASSERT_TRUE(archive_->SnapshotOnce());
  EXPECT_GT(Health().lost_bytes, 0);
  int64 lost = Health().lost_bytes;
  ASSERT_TRUE(archive_->SnapshotOnce());
  EXPECT_EQ(lost, Health().lost_bytes);
  EXPECT_EQ(before, buffer_.ToString(&handler_));
}
TEST_F(HistoryArchiveTest, RestartRecoveryAndIncompleteCheckpointWrite) {
  Write("before restart");
  archive_->fault = ENOSPC;
  archive_->checkpoint_only = true;
  EXPECT_FALSE(archive_->SnapshotOnce());  // Segment committed; checkpoint failed.
  EXPECT_EQ(ENOSPC, Health().error);
  archive_.reset(new FaultArchive(&buffer_, &timer_, path_, 300, 7, 1));
  ASSERT_TRUE(archive_->SnapshotOnce());
  EXPECT_EQ(1, Count(Page().log, "before restart"));
  buffer_.Clear();
  Write("new generation");
  ASSERT_TRUE(archive_->SnapshotOnce());
  EXPECT_EQ(1, Health().restart_gaps);
  EXPECT_EQ(2, Health().records);
}
TEST_F(HistoryArchiveTest, EmptyRestartReadsAllViewsBeforeAnotherSnapshot) {
  Write("archived restart message");
  Write("PS_IMAGE_DETAIL\t100\t200\t100\timage/webp\thttp://a/x\thttp://a/y");
  Write("PS_HTML_DETAIL\t100\t200\t20\tdesktop\tAVIF capable\tvariant\turl");
  Write("Script http://a/a.js didn't shrink.");
  Write("CSS parsing error in http://a/a.css");
  ASSERT_TRUE(archive_->SnapshotOnce());
  const auto saved = Health();
  const GoogleString original = Page().log;
  GoogleString checkpoint;
  { std::ifstream file(path_ + "/history/checkpoint"); std::getline(file, checkpoint); }
  archive_.reset();
  for (const char* mode : {"valid", "missing", "corrupt", "stale"}) {
    SCOPED_TRACE(mode);
    if (GoogleString(mode) == "missing") {
      ASSERT_EQ(0, unlink((path_ + "/history/checkpoint").c_str()));
    } else {
      std::ofstream file(path_ + "/history/checkpoint");
      if (GoogleString(mode) == "corrupt") file << "incomplete checkpoint\n";
      else if (GoogleString(mode) == "stale")
        file << "PSH1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n";
      else file << checkpoint << '\n';
    }
    SharedCircularBuffer restarted(&shm_, 4096, "history-test", mode, true);
    ASSERT_TRUE(restarted.InitSegment(true, &handler_));
    SharedCircularBuffer::HistorySnapshot empty;
    ASSERT_TRUE(restarted.SnapshotHistory(&empty, &handler_));
    EXPECT_TRUE(empty.data.empty());
    std::promise<void> ready;
    auto initialized = ready.get_future();
    HistoryArchive recovered(&restarted, &timer_, path_, 300, 7, 1);
    recovered.Start();
    ASSERT_TRUE(recovered.Schedule([&] { ready.set_value(); }));
    ASSERT_EQ(std::future_status::ready,
              initialized.wait_for(std::chrono::seconds(5)));
    recovered.Stop();
    SharedCircularBuffer::HistoryHealth restored;
    ASSERT_TRUE(restarted.GetHistoryHealth(&restored));
    EXPECT_TRUE(restored.initialized);
    EXPECT_EQ(saved.records, restored.records);
    EXPECT_EQ(5, restored.retained_records);
    EXPECT_EQ(saved.last_success_ms, restored.last_success_ms);
    EXPECT_GT(restored.disk_bytes, 0);
    EXPECT_EQ(0, restored.error);
    const char* kinds[] = {"messages", "images", "html_cache", "javascript", "css"};
    for (const char* kind : kinds) {
      auto page = HistoryArchive::Browse(path_, kind, 0, 253402300800000LL, "", &empty);
      EXPECT_TRUE(page.error.empty()) << page.error;
      EXPECT_EQ(GoogleString(kind) == "messages" ? 5 : 1, Count(page.log, "[Info]"));
    }
    EXPECT_EQ(original, Page().log);
    EXPECT_TRUE(restarted.ToString(&handler_).empty());
    restarted.GlobalCleanup(&handler_);
  }
}
TEST_F(HistoryArchiveTest, DiskAndPermissionFailuresDoNotAdvanceCursor) {
  for (int error : {ENOSPC, EACCES}) {
    Write(StrCat("failure-", IntegerToString(error)));
    archive_->fault = error;
    EXPECT_FALSE(archive_->SnapshotOnce());
    EXPECT_EQ(error, Health().error);
    archive_->fault = 0;
    ASSERT_TRUE(archive_->SnapshotOnce());
    EXPECT_EQ(1, Count(Page().log, StrCat("failure-", IntegerToString(error))));
  }
  EXPECT_EQ(2, Health().records);
}
TEST_F(HistoryArchiveTest, NonblockingWriterElection) {
  ASSERT_TRUE(archive_->SnapshotOnce());
  int fd = open((path_ + "/history/.writer.lock").c_str(), O_RDWR);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(0, flock(fd, LOCK_EX | LOCK_NB));
  Write("concurrent writer");
  EXPECT_FALSE(archive_->SnapshotOnce());
  close(fd);
  ASSERT_TRUE(archive_->SnapshotOnce());
  EXPECT_EQ(1, Count(Page().log, "concurrent writer"));
}
TEST_F(HistoryArchiveTest, PaginationAndDateFilteringAreBounded) {
  for (int batch = 0; batch < 6; ++batch) {
    for (int i = 0; i < 40; ++i) Write(StrCat("record-", IntegerToString(batch * 40 + i)));
    ASSERT_TRUE(archive_->SnapshotOnce());
  }
  auto first = Page();
  EXPECT_EQ(100, Count(first.log, "record-"));
  ASSERT_FALSE(first.next.empty());
  auto second = Page("messages", first.next);
  EXPECT_EQ(100, Count(second.log, "record-"));
  auto third = Page("messages", second.next);
  EXPECT_EQ(40, Count(third.log, "record-"));
  EXPECT_TRUE(third.next.empty());
  EXPECT_EQ(0, Count(HistoryArchive::Browse(path_, "messages",
      MockTimer::kApr_5_2010_ms + 86400000, 253402300800000LL, "").log, "record-"));
  EXPECT_FALSE(Page("messages", "../../escape").error.empty());
}
TEST_F(HistoryArchiveTest, RetentionAndCombinedSizeLimit) {
  GoogleString filler(2900, 'x');
  for (int i = 0; i < 380; ++i) {
    Write(filler);
    ASSERT_TRUE(archive_->SnapshotOnce());
  }
  EXPECT_LE(Health().disk_bytes, 1024 * 1024);
  EXPECT_EQ(380, Health().records);
  EXPECT_LT(Health().retained_records, Health().records);
  DIR* dir = opendir((path_ + "/history").c_str());
  ASSERT_NE(nullptr, dir);
  while (dirent* e = readdir(dir)) {
    if (GoogleString(e->d_name).find("segment-") != 0) continue;
    struct utimbuf old = {1, 1};
    utime((path_ + "/history/" + e->d_name).c_str(), &old);
  }
  closedir(dir);
  ASSERT_TRUE(archive_->SnapshotOnce());
  EXPECT_EQ("\n", Page().log);
  EXPECT_EQ(0, Health().retained_records);
  EXPECT_GT(Health().last_success_ms, 0);
}
TEST_F(HistoryArchiveTest, AttachedWorkersShareSequenceWithoutChangingText) {
  SharedCircularBuffer child(&shm_, 4096, "history-test", "records", true);
  ASSERT_TRUE(child.InitSegment(false, &handler_));
  Write("parent event");
  child.Write("I[Mon, 05 Apr 2010 18:51:26 GMT] [Info] [2] child event\n", &handler_);
  SharedCircularBuffer::HistorySnapshot parent_snapshot, child_snapshot;
  ASSERT_TRUE(buffer_.SnapshotHistory(&parent_snapshot, &handler_));
  ASSERT_TRUE(child.SnapshotHistory(&child_snapshot, &handler_));
  EXPECT_EQ(parent_snapshot.generation, child_snapshot.generation);
  EXPECT_EQ(parent_snapshot.end, child_snapshot.end);
  EXPECT_EQ(parent_snapshot.data, child_snapshot.data);
  ASSERT_TRUE(archive_->SnapshotOnce());
  EXPECT_EQ(2, Health().records);
}
TEST_F(HistoryArchiveTest, MultilineTimestampSurvivesDelayedSnapshot) {
  Write("original timestamp\nIcontinuation diagnostic");
  timer_.AdvanceMs(86400000);
  ASSERT_TRUE(archive_->SnapshotOnce());
  auto page = HistoryArchive::Browse(path_, "messages", MockTimer::kApr_5_2010_ms,
      MockTimer::kApr_5_2010_ms + 1000, "");
  EXPECT_EQ(1, Count(page.log, "continuation diagnostic"));
}
TEST_F(HistoryArchiveTest, StaleTemporaryAndActualPermissionFailureRecover) {
  Write("first event");
  ASSERT_TRUE(archive_->SnapshotOnce());
  std::ofstream(path_ + "/history/.pending") << "incomplete uncommitted write";
  Write("second event");
  ASSERT_TRUE(archive_->SnapshotOnce());
  EXPECT_EQ(GoogleString::npos, Page().log.find("uncommitted write"));
  if (geteuid() != 0) {
    ASSERT_EQ(0, chmod((path_ + "/history").c_str(), 0500));
    Write("permission event");
    bool result = archive_->SnapshotOnce();
    chmod((path_ + "/history").c_str(), 0700);
    EXPECT_FALSE(result);
    EXPECT_EQ(EACCES, Health().error);
    ASSERT_TRUE(archive_->SnapshotOnce());
    EXPECT_EQ(1, Count(Page().log, "permission event"));
  }
}
TEST_F(HistoryArchiveTest, AdminQueueIsBoundedAndDrainsOnShutdown) {
  std::mutex mutex;
  std::condition_variable changed;
  bool running = false, release = false;
  int completed = 0;
  archive_->Start();
  ASSERT_TRUE(archive_->Schedule([&] {
    std::unique_lock<std::mutex> lock(mutex);
    running = true;
    changed.notify_all();
    changed.wait(lock, [&] { return release; });
    ++completed;
  }));
  {
    std::unique_lock<std::mutex> lock(mutex);
    EXPECT_TRUE(changed.wait_for(lock, std::chrono::seconds(2), [&] { return running; }));
  }
  for (int i = 0; i < 4; ++i) EXPECT_TRUE(archive_->Schedule([&] { ++completed; }));
  EXPECT_FALSE(archive_->Schedule([] {}));
  {
    std::lock_guard<std::mutex> lock(mutex);
    release = true;
  }
  changed.notify_all();
  archive_->Stop();
  EXPECT_EQ(5, completed);
  EXPECT_FALSE(archive_->Schedule([] {}));
}
}  // namespace
}  // namespace net_instaweb
