/**
 * @file LatencyBench.cpp
 * @brief Implementation of timer latency and sleep jitter benchmarks.
 */

#include "src/timing/inc/LatencyBench.hpp"

#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

#include <fmt/core.h>

namespace seeker {

namespace timing {

/* ----------------------------- Internal Types ----------------------------- */

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::duration_cast;
using std::chrono::nanoseconds;

/// Convert nanoseconds to double.
inline double toDouble(nanoseconds ns) noexcept { return static_cast<double>(ns.count()); }

/// RAII helper for RT priority elevation.
class RtPriorityGuard {
public:
  RtPriorityGuard(int priority) noexcept : elevated_{false} {
    if (priority <= 0 || priority > 99) {
      return;
    }

    // Save original policy and priority
    originalPolicy_ = ::sched_getscheduler(0);
    if (originalPolicy_ < 0) {
      return;
    }

    struct sched_param origParam{};
    if (::sched_getparam(0, &origParam) != 0) {
      return;
    }
    originalPriority_ = origParam.sched_priority;

    // Elevate to SCHED_FIFO
    struct sched_param param{};
    param.sched_priority = priority;
    if (::sched_setscheduler(0, SCHED_FIFO, &param) == 0) {
      elevated_ = true;
      elevatedPriority_ = priority;
    }
  }

  ~RtPriorityGuard() noexcept {
    if (elevated_) {
      struct sched_param param{};
      param.sched_priority = originalPriority_;
      ::sched_setscheduler(0, originalPolicy_, &param);
    }
  }

  [[nodiscard]] bool elevated() const noexcept { return elevated_; }
  [[nodiscard]] int priority() const noexcept { return elevatedPriority_; }

  // Non-copyable
  RtPriorityGuard(const RtPriorityGuard&) = delete;
  RtPriorityGuard& operator=(const RtPriorityGuard&) = delete;

private:
  bool elevated_{false};
  int originalPolicy_{0};
  int originalPriority_{0};
  int elevatedPriority_{0};
};

/// Compute statistics from sorted samples.
struct Statistics {
  double min{0.0};
  double max{0.0};
  double mean{0.0};
  double median{0.0};
  double p90{0.0};
  double p95{0.0};
  double p99{0.0};
  double p999{0.0};
  double stdDev{0.0};
};

Statistics computeStats(std::vector<double>& samples) noexcept {
  Statistics stats;
  const std::size_t N = samples.size();

  if (N == 0) {
    return stats;
  }

  // Sort for percentile calculations
  std::sort(samples.begin(), samples.end());

  stats.min = samples.front();
  stats.max = samples.back();

  // Mean
  double sum = 0.0;
  for (double v : samples) {
    sum += v;
  }
  stats.mean = sum / static_cast<double>(N);

  // Median
  if (N % 2 == 0) {
    stats.median = (samples[N / 2 - 1] + samples[N / 2]) / 2.0;
  } else {
    stats.median = samples[N / 2];
  }

  // Percentiles
  auto percentile = [&](double p) -> double {
    const double INDEX = (static_cast<double>(N) - 1.0) * p;
    const std::size_t LOWER = static_cast<std::size_t>(INDEX);
    const std::size_t UPPER = LOWER + 1;
    const double FRAC = INDEX - static_cast<double>(LOWER);

    if (UPPER >= N) {
      return samples[N - 1];
    }
    return samples[LOWER] * (1.0 - FRAC) + samples[UPPER] * FRAC;
  };

  stats.p90 = percentile(0.90);
  stats.p95 = percentile(0.95);
  stats.p99 = percentile(0.99);
  stats.p999 = percentile(0.999);

  // Standard deviation
  double sumSq = 0.0;
  for (double v : samples) {
    const double DIFF = v - stats.mean;
    sumSq += DIFF * DIFF;
  }
  stats.stdDev = std::sqrt(sumSq / static_cast<double>(N));

  return stats;
}

/// Sleep using clock_nanosleep with optional TIMER_ABSTIME.
void preciseSleep(nanoseconds duration, bool absolute) noexcept {
  if (absolute) {
    // Compute absolute wakeup time
    struct timespec now{};
    ::clock_gettime(CLOCK_MONOTONIC, &now);

    const std::int64_t WAKEUP_NS = static_cast<std::int64_t>(now.tv_sec) * 1'000'000'000LL +
                                   static_cast<std::int64_t>(now.tv_nsec) + duration.count();

    struct timespec wakeup{};
    wakeup.tv_sec = static_cast<time_t>(WAKEUP_NS / 1'000'000'000LL);
    wakeup.tv_nsec = static_cast<long>(WAKEUP_NS % 1'000'000'000LL);

    ::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wakeup, nullptr);
  } else {
    std::this_thread::sleep_for(duration);
  }
}

} // namespace

/* ----------------------------- LatencyStats Methods ----------------------------- */

double LatencyStats::jitterMeanNs() const noexcept { return meanNs - targetNs; }

double LatencyStats::jitterP95Ns() const noexcept { return p95Ns - targetNs; }

double LatencyStats::jitterP99Ns() const noexcept { return p99Ns - targetNs; }

double LatencyStats::jitterMaxNs() const noexcept { return maxNs - targetNs; }

double LatencyStats::undershootNs() const noexcept { return targetNs - minNs; }

bool LatencyStats::isGoodForRt() const noexcept {
  // Good RT: p99 jitter < 100us
  return jitterP99Ns() < 100'000.0;
}

int LatencyStats::rtScore() const noexcept {
  const double JITTER_P99 = jitterP99Ns();
  const double JITTER_MAX = jitterMaxNs();

  // Score based on p99 jitter
  int score = 0;

  if (JITTER_P99 < 10'000) { // < 10us
    score = 100;
  } else if (JITTER_P99 < 50'000) { // < 50us
    score = 90;
  } else if (JITTER_P99 < 100'000) { // < 100us
    score = 75;
  } else if (JITTER_P99 < 500'000) { // < 500us
    score = 50;
  } else if (JITTER_P99 < 1'000'000) { // < 1ms
    score = 25;
  } else {
    score = 10;
  }

  // Penalize large max jitter even if p99 is good
  if (JITTER_MAX > JITTER_P99 * 10.0) {
    score -= 20;
  } else if (JITTER_MAX > JITTER_P99 * 5.0) {
    score -= 10;
  }

  // Ensure bounds
  if (score < 0) {
    score = 0;
  }
  if (score > 100) {
    score = 100;
  }

  return score;
}

std::string LatencyStats::toString() const {
  std::string out;
  out.reserve(1024);

  out += "Latency Benchmark Results:\n";

  // Configuration
  out += fmt::format("  Samples: {}  |  Target: {:.0f} us\n", sampleCount, targetNs / 1000.0);
  out += fmt::format("  Mode: {}  |  RT Priority: {}\n",
                     usedAbsoluteTime ? "TIMER_ABSTIME" : "sleep_for",
                     usedRtPriority ? std::to_string(rtPriorityUsed) : "none");

  // Timer overhead
  out += fmt::format("  now() overhead: {:.1f} ns\n", nowOverheadNs);

  // Sleep statistics
  out += "\n  Sleep Duration Statistics:\n";
  out += fmt::format("    Min:    {:>10.1f} us\n", minNs / 1000.0);
  out += fmt::format("    Mean:   {:>10.1f} us\n", meanNs / 1000.0);
  out += fmt::format("    Median: {:>10.1f} us\n", medianNs / 1000.0);
  out += fmt::format("    p90:    {:>10.1f} us\n", p90Ns / 1000.0);
  out += fmt::format("    p95:    {:>10.1f} us\n", p95Ns / 1000.0);
  out += fmt::format("    p99:    {:>10.1f} us\n", p99Ns / 1000.0);
  out += fmt::format("    p99.9:  {:>10.1f} us\n", p999Ns / 1000.0);
  out += fmt::format("    Max:    {:>10.1f} us\n", maxNs / 1000.0);
  out += fmt::format("    StdDev: {:>10.1f} us\n", stdDevNs / 1000.0);

  // Jitter analysis
  out += "\n  Jitter Analysis (actual - target):\n";
  out += fmt::format("    Mean jitter:  {:>+10.1f} us\n", jitterMeanNs() / 1000.0);
  out += fmt::format("    p95 jitter:   {:>+10.1f} us\n", jitterP95Ns() / 1000.0);
  out += fmt::format("    p99 jitter:   {:>+10.1f} us\n", jitterP99Ns() / 1000.0);
  out += fmt::format("    Max jitter:   {:>+10.1f} us\n", jitterMaxNs() / 1000.0);

  if (undershootNs() > 0) {
    out += fmt::format("    Early wakeup: {:>10.1f} us (undershoot)\n", undershootNs() / 1000.0);
  }

  // Assessment
  out += fmt::format("\n  RT Score: {}/100", rtScore());
  if (isGoodForRt()) {
    out += " [GOOD]\n";
  } else {
    out += " [NEEDS TUNING]\n";
  }

  return out;
}

/* ----------------------------- BenchConfig Methods ----------------------------- */

BenchConfig BenchConfig::quick() noexcept {
  BenchConfig cfg;
  cfg.budget = std::chrono::milliseconds{250};
  cfg.sleepTarget = std::chrono::microseconds{1000};
  return cfg;
}

BenchConfig BenchConfig::thorough() noexcept {
  BenchConfig cfg;
  cfg.budget = std::chrono::milliseconds{5000};
  cfg.sleepTarget = std::chrono::microseconds{1000};
  return cfg;
}

BenchConfig BenchConfig::rtCharacterization() noexcept {
  BenchConfig cfg;
  cfg.budget = std::chrono::milliseconds{2000};
  cfg.sleepTarget = std::chrono::microseconds{100}; // Smaller target for RT
  cfg.useAbsoluteTime = true;
  cfg.rtPriority = 90;
  return cfg;
}

/* ----------------------------- API ----------------------------- */

LatencyStats measureLatency(const BenchConfig& config) noexcept {
  LatencyStats result;

  // Enforce minimum budget
  std::chrono::milliseconds budget = config.budget;
  if (budget < MIN_BENCH_BUDGET) {
    budget = MIN_BENCH_BUDGET;
  }

  // Elevate RT priority if requested
  RtPriorityGuard rtGuard(config.rtPriority);
  result.usedRtPriority = rtGuard.elevated();
  result.rtPriorityUsed = rtGuard.elevated() ? rtGuard.priority() : 0;
  result.usedAbsoluteTime = config.useAbsoluteTime;

  // Measure now() overhead
  result.nowOverheadNs = measureNowOverhead(10000);

  // Set up sleep target
  const nanoseconds TARGET = duration_cast<nanoseconds>(config.sleepTarget);
  result.targetNs = toDouble(TARGET);

  // Collect samples
  std::vector<double> samples;
  samples.reserve(MAX_LATENCY_SAMPLES);

  const auto DEADLINE = Clock::now() + budget;
  while (Clock::now() < DEADLINE && samples.size() < MAX_LATENCY_SAMPLES) {
    const auto T0 = Clock::now();
    preciseSleep(TARGET, config.useAbsoluteTime);
    const auto T1 = Clock::now();

    const nanoseconds ACTUAL = duration_cast<nanoseconds>(T1 - T0);
    samples.push_back(toDouble(ACTUAL));
  }

  if (samples.empty()) {
    return result;
  }

  result.sampleCount = samples.size();

  // Compute statistics
  const Statistics STATS = computeStats(samples);

  result.minNs = STATS.min;
  result.maxNs = STATS.max;
  result.meanNs = STATS.mean;
  result.medianNs = STATS.median;
  result.p90Ns = STATS.p90;
  result.p95Ns = STATS.p95;
  result.p99Ns = STATS.p99;
  result.p999Ns = STATS.p999;
  result.stdDevNs = STATS.stdDev;

  return result;
}

LatencyStats measureLatency(std::chrono::milliseconds budget) noexcept {
  BenchConfig config;
  config.budget = budget;
  return measureLatency(config);
}

double measureNowOverhead(std::size_t iterations) noexcept {
  if (iterations == 0) {
    iterations = 10000;
  }

  // Warmup
  for (std::size_t i = 0; i < 100; ++i) {
    (void)Clock::now();
  }

  // Measure
  const auto T0 = Clock::now();
  for (std::size_t i = 0; i < iterations; ++i) {
    (void)Clock::now();
  }
  const auto T1 = Clock::now();

  const nanoseconds TOTAL = duration_cast<nanoseconds>(T1 - T0);
  return toDouble(TOTAL) / static_cast<double>(iterations);
}

} // namespace timing

} // namespace seeker
