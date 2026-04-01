#ifndef RCU_PMD_RETIRE_STATE_H_
#define RCU_PMD_RETIRE_STATE_H_

#include <cassert>
#include <cstdint>
#include <functional>
#include <vector>

#include <rte_rcu_qsbr.h>

#include "processor/pmd_job.h"
#include "rcu/rcu_manager.h"

namespace rcu {

// ---------------------------------------------------------------------------
// PmdRetireState — type-erased, non-template retire context for PMD threads.
//
// A PMD thread creates one PmdRetireState and shares it with all
// IntrusiveRcuList instances that use RemoveAndRetirePmdJob.  The state
// manages a single PmdJob that periodically checks whether pending retires
// have passed their QSBR grace period.
//
// Usage (in PMD loop):
//   pmd_retire_state.RefreshScheduling();   // manage the retire-check job
//   runner.RunRunnableJobs(now_tsc);
// ---------------------------------------------------------------------------
class PmdRetireState {
 public:
  PmdRetireState(RcuManager* rcu_manager, processor::PmdJobRunner* runner)
      : rcu_manager_(rcu_manager),
        runner_(runner),
        job_([this](uint64_t now_tsc) { OnRetireJobRun(now_tsc); }) {
    assert(rcu_manager != nullptr);
    assert(runner != nullptr);
  }

  ~PmdRetireState() {
    if (registered_) {
      (void)runner_->Unregister(&job_);
    }
  }

  PmdRetireState(const PmdRetireState&) = delete;
  PmdRetireState& operator=(const PmdRetireState&) = delete;

  // Add a type-erased pending retire.  Captures the current QSBR token so
  // the callback is invoked only after all readers have passed through a
  // quiescent state.
  void AddPendingRetire(std::function<void()> callback) {
    assert(callback);
    EnsureRegistered();

    struct rte_rcu_qsbr* qsbr = rcu_manager_->GetQsbrVar();
    assert(qsbr != nullptr);

    pending_.push_back({rte_rcu_qsbr_start(qsbr), std::move(callback)});
  }

  // Call from the PMD loop each iteration to manage the retire-check job's
  // scheduling.  Same pattern as RefreshGcScheduling() in
  // FiveTupleForwardingProcessor.
  void RefreshScheduling() {
    if (!registered_) return;

    // Sync with auto-return.
    scheduled_ = (job_.state() == processor::PmdJob::State::kRunner);

    bool should_run = !pending_.empty();
    if (should_run && !scheduled_) {
      scheduled_ = runner_->Schedule(&job_);
    } else if (!should_run && scheduled_) {
      if (runner_->Unschedule(&job_)) {
        scheduled_ = false;
      }
    }
  }

  bool HasPending() const { return !pending_.empty(); }

 private:
  struct PendingRetire {
    uint64_t token;
    std::function<void()> callback;
  };

  void EnsureRegistered() {
    if (!registered_) {
      registered_ = runner_->Register(&job_);
      assert(registered_);
    }
  }

  void OnRetireJobRun(uint64_t /*now_tsc*/) {
    struct rte_rcu_qsbr* qsbr = rcu_manager_->GetQsbrVar();
    auto it = pending_.begin();
    while (it != pending_.end()) {
      if (rte_rcu_qsbr_check(qsbr, it->token, 0)) {
        it->callback();
        it = pending_.erase(it);
      } else {
        ++it;
      }
    }
  }

  RcuManager* rcu_manager_;
  processor::PmdJobRunner* runner_;
  processor::PmdJob job_;
  bool registered_ = false;
  bool scheduled_ = false;
  std::vector<PendingRetire> pending_;
};

}  // namespace rcu

#endif  // RCU_PMD_RETIRE_STATE_H_
