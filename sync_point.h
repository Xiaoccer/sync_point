// Most code of this file is copied and modified from rocksdb SyncPoint.
// And modified by Xiaoccer (github.com/Xiaoccer).

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

// #define UNIT_TEST

#ifdef UNIT_TEST
namespace utils {

/************************************************************************/
/* SyncPoint */
/************************************************************************/
class SyncPoint {
 private:
  class Impl;
  std::unique_ptr<Impl> impl_;

 public:
  struct SyncPointPair {
    std::string predecessor;
    std::string successor;
  };

 private:
  SyncPoint();
  ~SyncPoint();

 public:
  SyncPoint(const SyncPoint&) = delete;
  SyncPoint(SyncPoint&&) = delete;
  SyncPoint& operator=(const SyncPoint&) = delete;
  SyncPoint& operator=(SyncPoint&&) = delete;

 public:
  static SyncPoint* GetInstance();

 public:
  // enable sync point processing (disabled on startup)
  void EnableProcessing();

  // disable sync point processing
  void DisableProcessing();

  // call once at the beginning of a test to setup the dependency between
  // sync points and setup markers indicating the successor is only enabled
  // when it is processed on the same thread as the predecessor.
  // When adding a marker, it implicitly adds a dependency for the marker pair.
  void LoadDependencyAndMarkers(const std::vector<SyncPointPair>& dependencies,
                                const std::vector<SyncPointPair>& markers = {});

  // The argument to the callback is passed through from
  // TEST_SYNC_POINT_CALLBACK(); nullptr if TEST_SYNC_POINT or
  // TEST_IDX_SYNC_POINT was used.
  void SetCallBack(const std::string& point, const std::function<void(const std::vector<void*>&)>& callback);

  // Clear callback function by point
  void ClearCallBack(const std::string& point);

  // Clear all call back functions.
  void ClearAllCallBacks();

  // remove the execution trace of all sync points
  void ClearTrace();

  // triggered by TEST_SYNC_POINT, blocking execution until all predecessors
  // are executed.
  // And/or call registered callback function, with argument `cb_arg`
  // void Process(const std::string& point, void* cb_arg = nullptr);
  void Process(const std::string& point, const std::vector<void*>& cb_args = {});
};

}  // namespace utils

// Use TEST_SYNC_POINT to specify sync points inside code base.
// Sync points can have happens-after dependency on other sync points,
// configured at runtime via SyncPoint::LoadDependency. This could be
// utilized to re-produce race conditions between threads.
// TEST_SYNC_POINT is no op in release build.
#define TEST_SYNC_POINT(x) utils::SyncPoint::GetInstance()->Process(x)
#define TEST_IDX_SYNC_POINT(x, index) utils::SyncPoint::GetInstance()->Process(x + std::to_string(index))
// #define TEST_SYNC_POINT_CALLBACK(x, y)
// utils::SyncPoint::GetInstance()->Process(x, y)
#define TEST_SYNC_POINT_ARGS(x, ...) utils::SyncPoint::GetInstance()->Process(x, std::vector<void*>{__VA_ARGS__})
#define TEST_SYNC_POINT_RETURN_VOID(x) \
  {                                    \
    bool flag = false;                 \
    TEST_SYNC_POINT_ARGS(x, &flag);    \
    if (flag) return;                  \
  }
#define TEST_SYNC_POINT_RETURN_VALUE(x, val_ptr)                                                    \
  {                                                                                                 \
    static_assert(!std::is_same_v<decltype(val_ptr), std::nullptr_t>, "val_ptr cannot be nullptr"); \
    bool flag = false;                                                                              \
    TEST_SYNC_POINT_ARGS(x, &flag, val_ptr);                                                        \
    if (flag) return *val_ptr;                                                                      \
  }
#define INIT_SYNC_POINT_SINGLETONS() (void)utils::SyncPoint::GetInstance();
#else
#define TEST_SYNC_POINT(x)
#define TEST_IDX_SYNC_POINT(x, index)
#define TEST_SYNC_POINT_ARGS(x, ...)
#define TEST_SYNC_POINT_RETURN_VOID(x)
#define TEST_SYNC_POINT_RETURN_VALUE(x, val_ptr)
#define INIT_SYNC_POINT_SINGLETONS()
#endif  // UNIT_TEST
