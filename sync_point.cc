#include "sync_point.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#ifdef UNIT_TEST
namespace utils {

/************************************************************************/
/* SyncPoint::Impl */
/************************************************************************/
class SyncPoint::Impl {
 private:
  std::atomic<bool> enabled_ = false;
  int num_callbacks_running_ = 0;

  std::unordered_map<std::string, std::vector<std::string>> successors_;
  std::unordered_map<std::string, std::vector<std::string>> predecessors_;
  std::unordered_map<std::string, std::function<void(const std::vector<void*>&)>> callbacks_;
  std::unordered_map<std::string, std::vector<std::string>> markers_;
  std::unordered_map<std::string, std::thread::id> marked_thread_id_;

  std::mutex mutex_;
  std::condition_variable cv_;
  // sync points that have been passed through
  std::unordered_set<std::string> cleared_points_;

 public:
  void EnableProcessing() { enabled_ = true; }

  void DisableProcessing() { enabled_ = false; }

  void LoadDependencyAndMarkers(const std::vector<SyncPointPair>& dependencies,
                                const std::vector<SyncPointPair>& markers = {}) {
    std::lock_guard lock(mutex_);
    successors_.clear();
    predecessors_.clear();
    cleared_points_.clear();
    markers_.clear();
    marked_thread_id_.clear();
    for (const auto& dependency : dependencies) {
      successors_[dependency.predecessor].push_back(dependency.successor);
      predecessors_[dependency.successor].push_back(dependency.predecessor);
    }
    for (const auto& marker : markers) {
      successors_[marker.predecessor].push_back(marker.successor);
      predecessors_[marker.successor].push_back(marker.predecessor);
      markers_[marker.predecessor].push_back(marker.successor);
    }
    cv_.notify_all();
  }

  void SetCallBack(const std::string& point, const std::function<void(const std::vector<void*>&)>& callback) {
    std::lock_guard lock(mutex_);
    callbacks_[point] = callback;
  }

  void ClearCallBack(const std::string& point) {
    std::unique_lock lock(mutex_);
    while (num_callbacks_running_ > 0) {
      cv_.wait(lock);
    }
    callbacks_.erase(point);
  }

  void ClearAllCallBacks() {
    std::unique_lock lock(mutex_);
    while (num_callbacks_running_ > 0) {
      cv_.wait(lock);
    }
    callbacks_.clear();
  }

  void ClearTrace() {
    std::lock_guard lock(mutex_);
    cleared_points_.clear();
  }

  void Process(const std::string& point, const std::vector<void*>& cb_args) {
    if (!enabled_) {
      return;
    }
    std::unique_lock lock(mutex_);
    auto thread_id = std::this_thread::get_id();
    auto marker_iter = markers_.find(point);
    if (marker_iter != markers_.end()) {
      for (auto& marked_point : marker_iter->second) {
        marked_thread_id_.emplace(marked_point, thread_id);
      }
    }

    if (DisabledByMarker(point, thread_id)) {
      return;
    }

    while (!PredecessorsAllCleared(point)) {
      cv_.wait(lock);
      if (DisabledByMarker(point, thread_id)) {
        return;
      }
    }

    const auto& callback_pair = callbacks_.find(point);
    if (callback_pair != callbacks_.end()) {
      num_callbacks_running_++;
      mutex_.unlock();
      callback_pair->second(cb_args);
      mutex_.lock();
      num_callbacks_running_--;
    }
    cleared_points_.insert(point);
    cv_.notify_all();
  }

 private:
  bool PredecessorsAllCleared(const std::string& point) {
    for (const auto& pred : predecessors_[point]) {
      if (cleared_points_.count(pred) == 0) {
        return false;
      }
    }
    return true;
  }

  bool DisabledByMarker(const std::string& point, std::thread::id thread_id) {
    auto marked_point_iter = marked_thread_id_.find(point);
    return marked_point_iter != marked_thread_id_.end() && thread_id != marked_point_iter->second;
  }
};

/************************************************************************/
/* SyncPoint */
/************************************************************************/
SyncPoint* SyncPoint::GetInstance() {
  static SyncPoint sync_point;
  return &sync_point;
}

SyncPoint::SyncPoint() : impl_(std::make_unique<Impl>()) {}

SyncPoint::~SyncPoint() = default;

void SyncPoint::EnableProcessing() { impl_->EnableProcessing(); }

void SyncPoint::DisableProcessing() { impl_->DisableProcessing(); }

void SyncPoint::LoadDependencyAndMarkers(const std::vector<SyncPointPair>& dependencies,
                                         const std::vector<SyncPointPair>& markers) {
  impl_->LoadDependencyAndMarkers(dependencies, markers);
}

void SyncPoint::SetCallBack(const std::string& point, const std::function<void(const std::vector<void*>&)>& callback) {
  impl_->SetCallBack(point, callback);
}

void SyncPoint::ClearCallBack(const std::string& point) { impl_->ClearCallBack(point); }

void SyncPoint::ClearAllCallBacks() { impl_->ClearAllCallBacks(); }

void SyncPoint::ClearTrace() { impl_->ClearTrace(); }

void SyncPoint::Process(const std::string& point, const std::vector<void*>& cb_args) { impl_->Process(point, cb_args); }

}  // namespace utils

#endif  // UNIT_TEST
