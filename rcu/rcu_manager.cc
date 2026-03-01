#include "rcu/rcu_manager.h"

#include <chrono>

#include <rte_malloc.h>
#include <rte_rcu_qsbr.h>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "boost/system/error_code.hpp"

namespace rcu {

RcuManager::RcuManager() = default;

RcuManager::~RcuManager() {
  Stop();
  if (qsbr_var_ != nullptr) {
    rte_free(qsbr_var_);
    qsbr_var_ = nullptr;
  }
}

absl::Status RcuManager::Init(boost::asio::io_context& io_ctx,
                               const Config& config) {
  ssize_t sz = rte_rcu_qsbr_get_memsize(config.max_threads);
  if (sz < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("rte_rcu_qsbr_get_memsize failed for max_threads=",
                     config.max_threads));
  }

  qsbr_var_ = static_cast<struct rte_rcu_qsbr*>(
      rte_zmalloc(nullptr, sz, RTE_CACHE_LINE_SIZE));
  if (qsbr_var_ == nullptr) {
    return absl::ResourceExhaustedError("Failed to allocate QSBR variable");
  }

  int ret = rte_rcu_qsbr_init(qsbr_var_, config.max_threads);
  if (ret != 0) {
    rte_free(qsbr_var_);
    qsbr_var_ = nullptr;
    return absl::InternalError(
        absl::StrCat("rte_rcu_qsbr_init failed: ", ret));
  }

  io_ctx_ = &io_ctx;
  config_ = config;
  timer_ = std::make_unique<boost::asio::steady_timer>(io_ctx);
  return absl::OkStatus();
}

absl::Status RcuManager::RegisterThread(uint32_t lcore_id) {
  std::lock_guard<std::mutex> lock(registration_mu_);

  if (lcore_id >= config_.max_threads) {
    return absl::InvalidArgumentError(
        absl::StrCat("lcore_id ", lcore_id,
                     " exceeds max_threads ", config_.max_threads));
  }

  if (registered_threads_.count(lcore_id) != 0) {
    return absl::AlreadyExistsError(
        absl::StrCat("Thread ", lcore_id, " is already registered"));
  }

  int ret = rte_rcu_qsbr_thread_register(qsbr_var_, lcore_id);
  if (ret != 0) {
    return absl::InternalError(
        absl::StrCat("rte_rcu_qsbr_thread_register failed for lcore ",
                     lcore_id, ": ", ret));
  }

  rte_rcu_qsbr_thread_online(qsbr_var_, lcore_id);
  registered_threads_.insert(lcore_id);
  return absl::OkStatus();
}

absl::Status RcuManager::UnregisterThread(uint32_t lcore_id) {
  std::lock_guard<std::mutex> lock(registration_mu_);

  if (registered_threads_.count(lcore_id) == 0) {
    return absl::NotFoundError(
        absl::StrCat("Thread ", lcore_id, " is not registered"));
  }

  rte_rcu_qsbr_thread_offline(qsbr_var_, lcore_id);
  rte_rcu_qsbr_thread_unregister(qsbr_var_, lcore_id);
  registered_threads_.erase(lcore_id);
  return absl::OkStatus();
}

absl::Status RcuManager::CallAfterGracePeriod(DeferredAction callback) {
  if (!running_) {
    return absl::FailedPreconditionError("RcuManager is not running");
  }

  uint64_t token = rte_rcu_qsbr_start(qsbr_var_);
  auto item = std::make_unique<DeferredWorkItem>();
  item->token = token;
  item->callback = std::move(callback);
  pending_.push_back(std::move(item));

  ArmTimer();
  return absl::OkStatus();
}

void RcuManager::PostDeferredWork(DeferredWorkItem* item) {
  mpsc_queue_.Push(item);
}

absl::Status RcuManager::Start() {
  if (qsbr_var_ == nullptr) {
    return absl::FailedPreconditionError(
        "RcuManager not initialized. Call Init() first.");
  }

  running_ = true;
  if (!pending_.empty()) {
    ArmTimer();
  }
  return absl::OkStatus();
}

void RcuManager::Stop() {
  if (!running_) {
    return;
  }
  running_ = false;

  timer_->cancel();

  DrainMpscQueue();
  pending_.clear();
}

void RcuManager::OnPollTimer() {
  if (!running_) {
    return;
  }

  DrainMpscQueue();
  ProcessPendingItems();

  if (!pending_.empty()) {
    ArmTimer();
  }
}

void RcuManager::DrainMpscQueue() {
  for (;;) {
    DeferredWorkItem* item = mpsc_queue_.Pop();
    if (item == nullptr) {
      break;
    }
    pending_.push_back(std::unique_ptr<DeferredWorkItem>(item));
  }
}

void RcuManager::ProcessPendingItems() {
  auto it = pending_.begin();
  while (it != pending_.end()) {
    if (rte_rcu_qsbr_check(qsbr_var_, (*it)->token, 0)) {
      (*it)->callback();
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
}

void RcuManager::ArmTimer() {
  timer_->expires_after(
      std::chrono::milliseconds(config_.poll_interval_ms));
  timer_->async_wait([this](boost::system::error_code ec) {
    if (!ec) {
      OnPollTimer();
    }
  });
}

}  // namespace rcu
