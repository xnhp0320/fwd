#include "processor/pmd_job.h"

#include <gtest/gtest.h>

namespace processor {
namespace {

TEST(PmdJobRunnerTest, RegisterScheduleRunUnregister) {
  int calls = 0;
  PmdJob job([&calls](uint64_t) { ++calls; });
  PmdJobRunner runner;

  EXPECT_TRUE(runner.Register(&job));
  EXPECT_EQ(job.state(), PmdJob::State::kPending);
  EXPECT_EQ(runner.pending_size(), 1u);
  EXPECT_EQ(runner.runner_size(), 0u);

  EXPECT_TRUE(runner.Schedule(&job));
  EXPECT_EQ(job.state(), PmdJob::State::kRunner);
  EXPECT_EQ(runner.pending_size(), 0u);
  EXPECT_EQ(runner.runner_size(), 1u);
  EXPECT_TRUE(runner.HasRunnableJobs());

  // RunRunnableJobs executes the callback and auto-returns the job to pending.
  runner.RunRunnableJobs(100);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(job.state(), PmdJob::State::kPending);
  EXPECT_EQ(runner.pending_size(), 1u);
  EXPECT_EQ(runner.runner_size(), 0u);
  EXPECT_FALSE(runner.HasRunnableJobs());

  // Second RunRunnableJobs on empty runner list is a no-op.
  runner.RunRunnableJobs(200);
  EXPECT_EQ(calls, 1);

  // Re-schedule succeeds since job is back in kPending after auto-return.
  EXPECT_TRUE(runner.Schedule(&job));
  EXPECT_EQ(job.state(), PmdJob::State::kRunner);

  runner.RunRunnableJobs(300);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(job.state(), PmdJob::State::kPending);

  EXPECT_TRUE(runner.Unregister(&job));
  EXPECT_EQ(job.state(), PmdJob::State::kIdle);
  EXPECT_EQ(runner.pending_size(), 0u);
  EXPECT_EQ(runner.runner_size(), 0u);
}

TEST(PmdJobRunnerTest, RejectsInvalidTransitions) {
  PmdJob job([](uint64_t) {});
  PmdJobRunner runner;

  EXPECT_FALSE(runner.Schedule(&job));
  EXPECT_FALSE(runner.Unschedule(&job));

  EXPECT_TRUE(runner.Register(&job));
  EXPECT_FALSE(runner.Register(&job));
  EXPECT_FALSE(runner.Unschedule(&job));

  EXPECT_TRUE(runner.Schedule(&job));
  EXPECT_FALSE(runner.Schedule(&job));
  EXPECT_FALSE(runner.Register(&job));

  EXPECT_TRUE(runner.Unregister(&job));
  EXPECT_EQ(job.state(), PmdJob::State::kIdle);
}

// Validates: Requirements 1.4
// RunRunnableJobs() on empty runner list is a no-op — pending jobs unaffected.
TEST(PmdJobRunnerTest, RunRunnableJobsOnEmptyRunnerIsNoop) {
  int calls = 0;
  PmdJob job([&calls](uint64_t) { ++calls; });
  PmdJobRunner runner;

  EXPECT_TRUE(runner.Register(&job));
  EXPECT_EQ(runner.pending_size(), 1u);
  EXPECT_EQ(runner.runner_size(), 0u);

  // Run with nothing on the runner list.
  runner.RunRunnableJobs(100);

  // Pending job is unaffected, callback was not invoked.
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(job.state(), PmdJob::State::kPending);
  EXPECT_EQ(runner.pending_size(), 1u);
  EXPECT_EQ(runner.runner_size(), 0u);

  EXPECT_TRUE(runner.Unregister(&job));
}

// Validates: Requirements 1.1, 1.2, 1.3
// Single job auto-return cycle: schedule, run, verify kPending, re-schedule.
TEST(PmdJobRunnerTest, SingleJobAutoReturnCycle) {
  int calls = 0;
  PmdJob job([&calls](uint64_t) { ++calls; });
  PmdJobRunner runner;

  EXPECT_TRUE(runner.Register(&job));
  EXPECT_TRUE(runner.Schedule(&job));
  EXPECT_EQ(job.state(), PmdJob::State::kRunner);

  runner.RunRunnableJobs(100);

  // After run, job is back in kPending.
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(job.state(), PmdJob::State::kPending);
  EXPECT_EQ(runner.pending_size(), 1u);
  EXPECT_EQ(runner.runner_size(), 0u);

  // Re-schedule succeeds because job is kPending.
  EXPECT_TRUE(runner.Schedule(&job));
  EXPECT_EQ(job.state(), PmdJob::State::kRunner);
  EXPECT_EQ(runner.runner_size(), 1u);

  EXPECT_TRUE(runner.Unregister(&job));
}

// Validates: Requirements 1.1, 1.2
// Multiple jobs all return to pending after a single RunRunnableJobs() call.
TEST(PmdJobRunnerTest, MultipleJobsAutoReturn) {
  constexpr int kNumJobs = 5;
  int calls[kNumJobs] = {};
  PmdJob jobs[kNumJobs] = {
      PmdJob([&calls](uint64_t) { ++calls[0]; }),
      PmdJob([&calls](uint64_t) { ++calls[1]; }),
      PmdJob([&calls](uint64_t) { ++calls[2]; }),
      PmdJob([&calls](uint64_t) { ++calls[3]; }),
      PmdJob([&calls](uint64_t) { ++calls[4]; }),
  };
  PmdJobRunner runner;

  for (int i = 0; i < kNumJobs; ++i) {
    EXPECT_TRUE(runner.Register(&jobs[i]));
    EXPECT_TRUE(runner.Schedule(&jobs[i]));
  }
  EXPECT_EQ(runner.runner_size(), static_cast<std::size_t>(kNumJobs));
  EXPECT_EQ(runner.pending_size(), 0u);

  runner.RunRunnableJobs(100);

  // All jobs auto-returned to pending.
  EXPECT_EQ(runner.runner_size(), 0u);
  EXPECT_EQ(runner.pending_size(), static_cast<std::size_t>(kNumJobs));
  for (int i = 0; i < kNumJobs; ++i) {
    EXPECT_EQ(jobs[i].state(), PmdJob::State::kPending);
    EXPECT_EQ(calls[i], 1);
  }

  for (int i = 0; i < kNumJobs; ++i) {
    EXPECT_TRUE(runner.Unregister(&jobs[i]));
  }
}

// Validates: Requirements 1.5 (Property 3)
// Callback counter increments exactly once per RunRunnableJobs() invocation.
TEST(PmdJobRunnerTest, CallbackExactlyOncePerInvocation) {
  int calls = 0;
  PmdJob job([&calls](uint64_t) { ++calls; });
  PmdJobRunner runner;

  EXPECT_TRUE(runner.Register(&job));

  // First cycle.
  EXPECT_TRUE(runner.Schedule(&job));
  runner.RunRunnableJobs(100);
  EXPECT_EQ(calls, 1);

  // Second cycle.
  EXPECT_TRUE(runner.Schedule(&job));
  runner.RunRunnableJobs(200);
  EXPECT_EQ(calls, 2);

  // Third cycle.
  EXPECT_TRUE(runner.Schedule(&job));
  runner.RunRunnableJobs(300);
  EXPECT_EQ(calls, 3);

  // Not scheduled — counter stays at 3.
  runner.RunRunnableJobs(400);
  EXPECT_EQ(calls, 3);

  EXPECT_TRUE(runner.Unregister(&job));
}

// Validates: Requirements 1.3 (Property 2)
// Re-schedule after auto-return succeeds and job runs again.
TEST(PmdJobRunnerTest, ReScheduleAfterAutoReturnRunsAgain) {
  int calls = 0;
  PmdJob job([&calls](uint64_t) { ++calls; });
  PmdJobRunner runner;

  EXPECT_TRUE(runner.Register(&job));

  // First cycle: schedule → run → auto-return.
  EXPECT_TRUE(runner.Schedule(&job));
  runner.RunRunnableJobs(100);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(job.state(), PmdJob::State::kPending);

  // Re-schedule after auto-return.
  EXPECT_TRUE(runner.Schedule(&job));
  EXPECT_EQ(job.state(), PmdJob::State::kRunner);
  EXPECT_EQ(runner.runner_size(), 1u);

  // Second cycle: runs again.
  runner.RunRunnableJobs(200);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(job.state(), PmdJob::State::kPending);
  EXPECT_EQ(runner.runner_size(), 0u);
  EXPECT_EQ(runner.pending_size(), 1u);

  EXPECT_TRUE(runner.Unregister(&job));
}

}  // namespace
}  // namespace processor
