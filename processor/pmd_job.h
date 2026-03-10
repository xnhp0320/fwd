#ifndef PROCESSOR_PMD_JOB_H_
#define PROCESSOR_PMD_JOB_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

#include <boost/intrusive/slist.hpp>

namespace processor {

class PmdJob {
 public:
  using Hook = boost::intrusive::slist_member_hook<
      boost::intrusive::link_mode<boost::intrusive::safe_link>>;
  using Callback = std::function<void(uint64_t)>;

  enum class State {
    kIdle,
    kPending,
    kRunner,
  };

  explicit PmdJob(Callback callback = nullptr)
      : callback_(std::move(callback)) {}

  PmdJob(const PmdJob&) = delete;
  PmdJob& operator=(const PmdJob&) = delete;

  void SetCallback(Callback callback) { callback_ = std::move(callback); }
  State state() const { return state_; }

  Hook hook;

 private:
  friend class PmdJobRunner;

  void Run(uint64_t now_tsc) {
    if (callback_) {
      callback_(now_tsc);
    }
  }

  Callback callback_;
  State state_ = State::kIdle;
};

class PmdJobRunner {
 public:
  using JobList = boost::intrusive::slist<
      PmdJob,
      boost::intrusive::member_hook<PmdJob, PmdJob::Hook, &PmdJob::hook>,
      boost::intrusive::constant_time_size<true>,
      boost::intrusive::cache_last<true>>;

  bool Register(PmdJob* job) {
    if (job == nullptr || job->state_ != PmdJob::State::kIdle ||
        job->hook.is_linked()) {
      return false;
    }
    pending_jobs_.push_back(*job);
    job->state_ = PmdJob::State::kPending;
    return true;
  }

  bool Schedule(PmdJob* job) {
    if (job == nullptr || job->state_ != PmdJob::State::kPending) {
      return false;
    }
    if (!RemoveFromList(pending_jobs_, job)) {
      return false;
    }
    runner_jobs_.push_back(*job);
    job->state_ = PmdJob::State::kRunner;
    return true;
  }

  bool Unschedule(PmdJob* job) {
    if (job == nullptr || job->state_ != PmdJob::State::kRunner) {
      return false;
    }
    if (!RemoveFromList(runner_jobs_, job)) {
      return false;
    }
    pending_jobs_.push_back(*job);
    job->state_ = PmdJob::State::kPending;
    return true;
  }

  bool Unregister(PmdJob* job) {
    if (job == nullptr) return false;
    if (job->state_ == PmdJob::State::kIdle) return true;

    bool removed = false;
    if (job->state_ == PmdJob::State::kPending) {
      removed = RemoveFromList(pending_jobs_, job);
    } else if (job->state_ == PmdJob::State::kRunner) {
      removed = RemoveFromList(runner_jobs_, job);
    }

    if (!removed) return false;
    job->state_ = PmdJob::State::kIdle;
    return true;
  }

  void RunRunnableJobs(uint64_t now_tsc) {
    // Execute all runner jobs.
    for (auto it = runner_jobs_.begin(); it != runner_jobs_.end(); ++it) {
      it->Run(now_tsc);
    }
    // Auto-return: move all executed jobs back to pending.
    while (!runner_jobs_.empty()) {
      PmdJob& job = runner_jobs_.front();
      runner_jobs_.pop_front();
      job.state_ = PmdJob::State::kPending;
      pending_jobs_.push_back(job);
    }
  }

  bool HasRunnableJobs() const { return !runner_jobs_.empty(); }
  std::size_t pending_size() const { return pending_jobs_.size(); }
  std::size_t runner_size() const { return runner_jobs_.size(); }

 private:
  bool RemoveFromList(JobList& list, PmdJob* job) {
    auto prev = list.before_begin();
    for (auto it = list.begin(); it != list.end(); ++it) {
      if (&*it == job) {
        list.erase_after(prev);
        return true;
      }
      prev = it;
    }
    return false;
  }

  JobList pending_jobs_;
  JobList runner_jobs_;
};

}  // namespace processor

#endif  // PROCESSOR_PMD_JOB_H_
