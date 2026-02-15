/**
 * @file Affinity.cpp
 * @brief Thread CPU affinity implementation (Linux).
 * @note Uses pthread_getaffinity_np/pthread_setaffinity_np for current thread.
 */

#include "src/cpu/inc/Affinity.hpp"

#include <pthread.h> // pthread_self, pthread_*affinity_np
#include <sched.h>   // cpu_set_t, CPU_*
#include <unistd.h>  // sysconf, _SC_NPROCESSORS_CONF

#include <algorithm> // std::min

namespace seeker {

namespace cpu {

/* ----------------------------- Status Helpers ----------------------------- */

const char* toString(AffinityStatus status) noexcept {
  switch (status) {
  case AffinityStatus::OK:
    return "OK";
  case AffinityStatus::INVALID_ARGUMENT:
    return "INVALID_ARGUMENT";
  case AffinityStatus::SYSCALL_FAILED:
    return "SYSCALL_FAILED";
  }
  return "UNKNOWN";
}

/* ----------------------------- CpuSet Methods ----------------------------- */

bool CpuSet::test(std::size_t cpuId) const noexcept {
  if (cpuId >= MAX_CPUS) {
    return false;
  }
  return mask.test(cpuId);
}

void CpuSet::set(std::size_t cpuId) noexcept {
  if (cpuId < MAX_CPUS) {
    mask.set(cpuId);
  }
}

void CpuSet::clear(std::size_t cpuId) noexcept {
  if (cpuId < MAX_CPUS) {
    mask.reset(cpuId);
  }
}

void CpuSet::reset() noexcept { mask.reset(); }

std::size_t CpuSet::count() const noexcept { return mask.count(); }

bool CpuSet::empty() const noexcept { return mask.none(); }

std::string CpuSet::toString() const {
  std::string out;
  out.reserve(64);
  out.push_back('{');

  bool first = true;
  for (std::size_t i = 0; i < MAX_CPUS; ++i) {
    if (mask.test(i)) {
      if (!first) {
        out.push_back(',');
      }
      out += std::to_string(i);
      first = false;
    }
  }

  out.push_back('}');
  return out;
}

/* ----------------------------- API ----------------------------- */

std::size_t getConfiguredCpuCount() noexcept {
  const long N = ::sysconf(_SC_NPROCESSORS_CONF);
  if (N <= 0) {
    return MAX_CPUS;
  }
  return static_cast<std::size_t>(N);
}

CpuSet getCurrentThreadAffinity() noexcept {
  CpuSet result{};

  cpu_set_t kernelMask;
  CPU_ZERO(&kernelMask);

  const int RC = ::pthread_getaffinity_np(pthread_self(), sizeof(kernelMask), &kernelMask);
  if (RC != 0) {
    return result;
  }

  const std::size_t LIMIT = std::min(getConfiguredCpuCount(), MAX_CPUS);
  for (std::size_t i = 0; i < LIMIT; ++i) {
    if (CPU_ISSET(static_cast<int>(i), &kernelMask)) {
      result.mask.set(i);
    }
  }

  return result;
}

AffinityStatus setCurrentThreadAffinity(const CpuSet& set) noexcept {
  if (set.empty()) {
    return AffinityStatus::INVALID_ARGUMENT;
  }

  cpu_set_t kernelMask;
  CPU_ZERO(&kernelMask);

  const std::size_t LIMIT = std::min(getConfiguredCpuCount(), MAX_CPUS);

  for (std::size_t i = 0; i < LIMIT; ++i) {
    if (set.mask.test(i)) {
      CPU_SET(static_cast<int>(i), &kernelMask);
    }
  }

  const int RC = ::pthread_setaffinity_np(pthread_self(), sizeof(kernelMask), &kernelMask);
  return (RC == 0) ? AffinityStatus::OK : AffinityStatus::SYSCALL_FAILED;
}

} // namespace cpu

} // namespace seeker