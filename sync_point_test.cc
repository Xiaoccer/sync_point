#include "sync_point.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// NOLINTNEXTLINE
using namespace utils;

namespace {

void DummySyncPoint() { TEST_SYNC_POINT("SyncPointTest::DummySyncPoint"); }

void DummyIdxSyncPoint(int index) { TEST_IDX_SYNC_POINT("SyncPointTest::DummyIdxSyncPoint:", index); }

void DummyArgsSyncPoint(int& num, std::string& str) {
  TEST_SYNC_POINT_ARGS("SyncPointTest::DummyArgsSyncPoint", &num, &str);
}

void DummyCommonSyncPoint(int& num) { TEST_SYNC_POINT_ARGS("SyncPointTest::DummyCommonSyncPoint", &num); }

void DummyPlusOneSyncPoint(int& num) {
  TEST_SYNC_POINT_RETURN_VOID("SyncPointTest::DummyPlusOneSyncPoint");
  ++num;
  return;
}

std::string DummyReturnHelloSyncPoint() {
  std::string str = "Hello";
  TEST_SYNC_POINT_RETURN_VALUE("SyncPointTest::DummyReturnHelloSyncPoint", &str);
  return str;
}

}  // namespace

/************************************************************************/
/* SyncPointTest */
/************************************************************************/
class SyncPointTest : public testing::Test {};

TEST_F(SyncPointTest, basic) {
  {
    int a = 1000;
    SyncPoint::GetInstance()->SetCallBack("SyncPointTest::DummySyncPoint",
                                          [&](const std::vector<void*>&) { a = 10086; });
    SyncPoint::GetInstance()->EnableProcessing();
    DummySyncPoint();
    ASSERT_EQ(a, 10086);
    SyncPoint::GetInstance()->DisableProcessing();
  }

  {
    int a = 1000;
    SyncPoint::GetInstance()->SetCallBack("SyncPointTest::DummyIdxSyncPoint:1",
                                          [&](const std::vector<void*>&) { a = 10086; });
    SyncPoint::GetInstance()->EnableProcessing();
    DummyIdxSyncPoint(1);
    ASSERT_EQ(a, 10086);
    SyncPoint::GetInstance()->DisableProcessing();
  }

  {
    int num = 1234;
    std::string str = "Hello";
    DummyArgsSyncPoint(num, str);
    ASSERT_EQ(num, 1234);
    ASSERT_EQ(str, "Hello");
    SyncPoint::GetInstance()->SetCallBack("SyncPointTest::DummyArgsSyncPoint", [](const std::vector<void*>& args) {
      int* num = (int*)args[0];
      *num = 10086;
      auto* str = (std::string*)args[1];
      *str = "World";
    });
    SyncPoint::GetInstance()->EnableProcessing();
    DummyArgsSyncPoint(num, str);
    ASSERT_EQ(num, 10086);
    ASSERT_EQ(str, "World");
    SyncPoint::GetInstance()->DisableProcessing();
  }
}

TEST_F(SyncPointTest, Dependency) {
  std::mutex m;
  std::ostringstream buf;
  SyncPoint::GetInstance()->LoadDependencyAndMarkers({
      {"SyncPointTest::Step:1", "SyncPointTest::Step:2"},
      {"SyncPointTest::Step:3", "SyncPointTest::Step:4"},
      {"SyncPointTest::Step:5", "SyncPointTest::Step:6"},
  });

  SyncPoint::GetInstance()->SetCallBack("SyncPointTest::Step:6", [&](const std::vector<void*>&) {
    std::lock_guard lock(m);
    buf << "End";
  });

  SyncPoint::GetInstance()->EnableProcessing();

  std::function<void()> func1 = [&]() {
    TEST_SYNC_POINT("SyncPointTest::Step:4");
    std::lock_guard lock(m);
    buf << "Thread1->";
    TEST_SYNC_POINT("SyncPointTest::Step:5");
  };

  std::function<void()> func2 = [&]() {
    TEST_SYNC_POINT("SyncPointTest::Step:2");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::lock_guard lock(m);
    buf << "Thread2->";
    TEST_SYNC_POINT("SyncPointTest::Step:3");
  };

  std::function<void()> func3 = [&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    std::lock_guard lock(m);
    buf << "Thread3->";
    TEST_SYNC_POINT("SyncPointTest::Step:1");
  };

  std::thread thread1(func1);
  std::thread thread2(func2);
  std::thread thread3(func3);

  TEST_SYNC_POINT("SyncPointTest::Step:6");

  thread1.join();
  thread2.join();
  thread3.join();

  ASSERT_EQ(buf.str(), "Thread3->Thread2->Thread1->End");
  SyncPoint::GetInstance()->DisableProcessing();
}

TEST_F(SyncPointTest, DependencyAndMark1) {
  SyncPoint::GetInstance()->SetCallBack(      //
      "SyncPointTest::DummyCommonSyncPoint",  //
      [&](const std::vector<void*>& args) {
        int* num = (int*)args[0];
        *num = 1000;
      });

  SyncPoint::GetInstance()->LoadDependencyAndMarkers(
      {}, {{"SyncPointTest::DependencyAndMark1:Thread1", "SyncPointTest::DummyCommonSyncPoint"}});

  SyncPoint::GetInstance()->EnableProcessing();

  int thread1_num = 1;
  std::function<void()> func1 = [&]() {
    TEST_SYNC_POINT("SyncPointTest::DependencyAndMark1:Thread1");
    DummyCommonSyncPoint(thread1_num);
  };

  int thread2_num = 2;
  std::function<void()> func2 = [&]() { DummyCommonSyncPoint(thread2_num); };

  std::thread thread1(func1);
  std::thread thread2(func2);
  thread1.join();
  thread2.join();

  ASSERT_EQ(thread1_num, 1000);
  ASSERT_EQ(thread2_num, 2);
  SyncPoint::GetInstance()->DisableProcessing();
}

// Similar to `TEST_F(DBTest2, SyncPointMarker)` in RocksDB testing
namespace {

void CountSyncPoint() { TEST_SYNC_POINT_ARGS("SyncPointTest::MarkedPoint", nullptr /* arg */); }

}  // namespace

TEST_F(SyncPointTest, DependencyAndMark2) {
  std::atomic<int> sync_point_called(0);
  SyncPoint::GetInstance()->SetCallBack("SyncPointTest::MarkedPoint",
                                        [&](const std::vector<void*>& /*args*/) { sync_point_called.fetch_add(1); });

  // The first dependency enforces Marker can be loaded before MarkedPoint.
  // The second checks that thread 1's MarkedPoint should be disabled here.
  // Execution order:
  // |   Thread 1    |  Thread 2   |
  // |               |   Marker    |
  // |  MarkedPoint  |             |
  // | Thread1First  |             |
  // |               | MarkedPoint |
  SyncPoint::GetInstance()->LoadDependencyAndMarkers(
      {{"SyncPointTest::SyncPointMarker:Thread1First", "SyncPointTest::MarkedPoint"}},
      {{"SyncPointTest::SyncPointMarker:Marker", "SyncPointTest::MarkedPoint"}});

  SyncPoint::GetInstance()->EnableProcessing();

  std::function<void()> func1 = [&]() {
    CountSyncPoint();
    TEST_SYNC_POINT("SyncPointTest::SyncPointMarker:Thread1First");
  };

  std::function<void()> func2 = [&]() {
    TEST_SYNC_POINT("SyncPointTest::SyncPointMarker:Marker");
    CountSyncPoint();
  };

  std::thread thread1(func1);
  std::thread thread2(func2);
  thread1.join();
  thread2.join();

  // Callback is only executed once
  ASSERT_EQ(sync_point_called.load(), 1);
  SyncPoint::GetInstance()->DisableProcessing();
}

TEST_F(SyncPointTest, Return) {
  {
    int num = 12;
    DummyPlusOneSyncPoint(num);
    ASSERT_EQ(num, 13);
    SyncPoint::GetInstance()->SetCallBack("SyncPointTest::DummyPlusOneSyncPoint", [&](const std::vector<void*>& args) {
      bool* flag = (bool*)args[0];
      *flag = true;
    });
    SyncPoint::GetInstance()->EnableProcessing();
    DummyPlusOneSyncPoint(num);
    ASSERT_EQ(num, 13);
    SyncPoint::GetInstance()->DisableProcessing();
  }
  {
    ASSERT_EQ(DummyReturnHelloSyncPoint(), "Hello");
    SyncPoint::GetInstance()->SetCallBack(           //
        "SyncPointTest::DummyReturnHelloSyncPoint",  //
        [&](const std::vector<void*>& args) {
          bool* flag = (bool*)args[0];
          auto str = (std::string*)args[1];
          *flag = true;
          *str = "Word";
        });
    SyncPoint::GetInstance()->EnableProcessing();
    ASSERT_EQ(DummyReturnHelloSyncPoint(), "Word");
    SyncPoint::GetInstance()->DisableProcessing();
  }
}
