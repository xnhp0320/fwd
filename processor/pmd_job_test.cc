#include "processor/pmd_job.h"

#include <gtest/gtest.h>

namespace processor {
namespace {

TEST(PmdJobRunnerTest, RegisterScheduleRunUnscheduleUnregister) {
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

  runner.RunRunnableJobs(100);
  runner.RunRunnableJobs(200);
  EXPECT_EQ(calls, 2);

  EXPECT_TRUE(runner.Unschedule(&job));
  EXPECT_EQ(job.state(), PmdJob::State::kPending);
  EXPECT_EQ(runner.pending_size(), 1u);
  EXPECT_EQ(runner.runner_size(), 0u);

  runner.RunRunnableJobs(300);
  EXPECT_EQ(calls, 2);

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

}  // namespace
}  // namespace processor
