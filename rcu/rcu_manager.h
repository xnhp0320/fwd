#ifndef RCU_RCU_MANAGER_H_
#define RCU_RCU_MANAGER_H_

#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_set>

#include "absl/status/status.h"
#include "boost/asio/io_context.hpp"
#include "boost/asio/steady_timer.hpp"
#include "rcu/deferred_work_item.h"
#include "rcu/mpsc_queue.h"

struct rte_rcu_qsbr;  // Forward declaration

namespace rcu {

class RcuManager {
 public:
  struct Config {
    uint32_t max_threads = 64;
    uint32_t poll_interval_ms = 1;
  };

  RcuManager();
  ~RcuManager();

  RcuManager(const RcuManager&) = delete;
  RcuManager& operator=(const RcuManager&) = delete;

  // Initialize the QSBR variable and bind to the io_context.
  // Must be called before Start().
  // Thread safety: call from control plane thread only.
  absl::Status Init(boost::asio::io_context& io_ctx, const Config& config);

  // Register a PMD thread by lcore ID.
  // Thread safety: safe to call from any thread.
  absl::Status RegisterThread(uint32_t lcore_id);

  // Unregister a PMD thread by lcore ID.
  // Thread safety: safe to call from any thread.
  absl::Status UnregisterThread(uint32_t lcore_id);

  // Get the raw QSBR variable pointer for PMD threads.
  // Thread safety: the returned pointer is safe for concurrent use.
  struct rte_rcu_qsbr* GetQsbrVar() const { return qsbr_var_; }

  // Schedule a callback to run after the current grace period completes.
  // Thread safety: call from control plane thread only.
  absl::Status CallAfterGracePeriod(DeferredAction callback);

  // Post a deferred work item from a PMD thread into the MPSC queue.
  // Thread safety: safe to call from any thread (wait-free).
  void PostDeferredWork(DeferredWorkItem* item);

  // Start the poll timer.
  // Thread safety: call from control plane thread only.
  absl::Status Start();

  // Stop the poll timer and discard all pending actions.
  // Thread safety: call from control plane thread only.
  void Stop();

  bool IsRunning() const { return running_; }

 private:
  void OnPollTimer();
  void DrainMpscQueue();
  void ProcessPendingItems();
  void ArmTimer();

  struct rte_rcu_qsbr* qsbr_var_ = nullptr;
  Config config_;
  boost::asio::io_context* io_ctx_ = nullptr;
  std::unique_ptr<boost::asio::steady_timer> timer_;
  MpscQueue mpsc_queue_;
  std::list<std::unique_ptr<DeferredWorkItem>> pending_;
  bool running_ = false;
  std::mutex registration_mu_;
  std::unordered_set<uint32_t> registered_threads_;
};

}  // namespace rcu

#endif  // RCU_RCU_MANAGER_H_
