/**
 * @file LoopbackBench.cpp
 * @brief Implementation of loopback latency and throughput benchmarks.
 */

#include "src/network/inc/LoopbackBench.hpp"

#include "src/helpers/inc/Cpu.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <thread>

#include <fmt/core.h>

namespace seeker {

namespace network {

namespace {

/* ----------------------------- Constants ----------------------------- */

constexpr const char* LOOPBACK_ADDR = "127.0.0.1";
constexpr int BASE_PORT = 19000; // Base port for benchmarks
constexpr int SOCKET_TIMEOUT_MS = 100;

/* ----------------------------- Socket Helpers ----------------------------- */

/**
 * Set socket receive timeout.
 */
inline bool setSocketTimeout(int fd, int timeoutMs) noexcept {
  struct timeval tv{};
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;
  return ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

/**
 * Set TCP_NODELAY for lower latency.
 */
inline bool setTcpNoDelay(int fd) noexcept {
  const int FLAG = 1;
  return ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &FLAG, sizeof(FLAG)) == 0;
}

/**
 * Enable address reuse.
 */
inline bool setReuseAddr(int fd) noexcept {
  const int FLAG = 1;
  return ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &FLAG, sizeof(FLAG)) == 0;
}

/**
 * Create and bind a server socket.
 * Returns fd on success, -1 on failure.
 */
inline int createServerSocket(int type, int port) noexcept {
  const int FD = ::socket(AF_INET, type, 0);
  if (FD < 0) {
    return -1;
  }

  setReuseAddr(FD);

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (::bind(FD, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(FD);
    return -1;
  }

  return FD;
}

/**
 * Create a client socket connected to loopback.
 */
inline int createClientSocket(int type, int port) noexcept {
  const int FD = ::socket(AF_INET, type, 0);
  if (FD < 0) {
    return -1;
  }

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  ::inet_pton(AF_INET, LOOPBACK_ADDR, &addr.sin_addr);

  if (type == SOCK_STREAM) {
    if (::connect(FD, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
      ::close(FD);
      return -1;
    }
  }

  return FD;
}

/**
 * Get monotonic time in nanoseconds.
 */
inline std::uint64_t getMonotonicNs() noexcept {
  struct timespec ts{};
  ::clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
         static_cast<std::uint64_t>(ts.tv_nsec);
}

/* ----------------------------- Statistics Helpers ----------------------------- */

/**
 * Compute percentile from sorted array.
 */
inline double percentile(const double* sorted, std::size_t count, double pct) noexcept {
  if (count == 0) {
    return 0.0;
  }
  if (count == 1) {
    return sorted[0];
  }

  const double IDX = (pct / 100.0) * static_cast<double>(count - 1);
  const std::size_t LOWER = static_cast<std::size_t>(IDX);
  const std::size_t UPPER = (LOWER + 1 < count) ? LOWER + 1 : LOWER;
  const double FRAC = IDX - static_cast<double>(LOWER);

  return sorted[LOWER] + FRAC * (sorted[UPPER] - sorted[LOWER]);
}

/**
 * Compute statistics from latency samples.
 */
inline void computeStats(double* samples, std::size_t count, LatencyResult& result) noexcept {
  if (count == 0) {
    return;
  }

  result.sampleCount = count;

  // Sort for percentiles
  std::sort(samples, samples + count);

  result.minUs = samples[0];
  result.maxUs = samples[count - 1];

  // Mean
  double sum = 0.0;
  for (std::size_t i = 0; i < count; ++i) {
    sum += samples[i];
  }
  result.meanUs = sum / static_cast<double>(count);

  // Percentiles
  result.medianUs = percentile(samples, count, 50.0);
  result.p50Us = result.medianUs;
  result.p90Us = percentile(samples, count, 90.0);
  result.p95Us = percentile(samples, count, 95.0);
  result.p99Us = percentile(samples, count, 99.0);
  result.p999Us = percentile(samples, count, 99.9);

  // Standard deviation
  double sumSq = 0.0;
  for (std::size_t i = 0; i < count; ++i) {
    const double DIFF = samples[i] - result.meanUs;
    sumSq += DIFF * DIFF;
  }
  result.stddevUs = std::sqrt(sumSq / static_cast<double>(count));

  result.success = true;
}

/* ----------------------------- Port Allocation ----------------------------- */

/**
 * Get unique port for benchmark (thread-safe).
 */
inline int allocatePort() noexcept {
  static std::atomic<int> portCounter{0};
  return BASE_PORT + (portCounter.fetch_add(1) % 1000);
}

} // namespace

/* ----------------------------- LatencyResult Methods ----------------------------- */

std::string LatencyResult::toString() const {
  if (!success) {
    return "Latency: FAILED";
  }

  return fmt::format("Latency: min={:.1f}us mean={:.1f}us p50={:.1f}us p95={:.1f}us "
                     "p99={:.1f}us max={:.1f}us stddev={:.1f}us samples={}",
                     minUs, meanUs, p50Us, p95Us, p99Us, maxUs, stddevUs, sampleCount);
}

/* ----------------------------- ThroughputResult Methods ----------------------------- */

std::string ThroughputResult::toString() const {
  if (!success) {
    return "Throughput: FAILED";
  }

  return fmt::format("Throughput: {:.2f} MiB/s ({:.0f} Mbps) over {:.2f}s", mibPerSec, mbitsPerSec,
                     durationSec);
}

/* ----------------------------- LoopbackBenchResult Methods ----------------------------- */

bool LoopbackBenchResult::anySuccess() const noexcept {
  return tcpLatency.success || udpLatency.success || tcpThroughput.success || udpThroughput.success;
}

bool LoopbackBenchResult::allSuccess() const noexcept {
  return tcpLatency.success && udpLatency.success && tcpThroughput.success && udpThroughput.success;
}

std::string LoopbackBenchResult::toString() const {
  std::string out;
  out += "Loopback Benchmark Results:\n";
  out += "  TCP " + tcpLatency.toString() + "\n";
  out += "  UDP " + udpLatency.toString() + "\n";
  out += "  TCP " + tcpThroughput.toString() + "\n";
  out += "  UDP " + udpThroughput.toString();
  return out;
}

/* ----------------------------- TCP Latency ----------------------------- */

LatencyResult measureTcpLatency(std::chrono::milliseconds budget, std::size_t messageSize,
                                std::size_t maxSamples) noexcept {
  LatencyResult result{};

  // Clamp parameters
  if (messageSize == 0 || messageSize > 65536) {
    messageSize = DEFAULT_LATENCY_MESSAGE_SIZE;
  }
  if (maxSamples == 0 || maxSamples > MAX_LATENCY_SAMPLES) {
    maxSamples = MAX_LATENCY_SAMPLES;
  }

  const int PORT = allocatePort();

  // Fixed-size sample storage (on stack for RT-friendliness in calling context)
  double samples[MAX_LATENCY_SAMPLES];
  std::size_t sampleCount = 0;

  // Server socket
  const int SERVER_FD = createServerSocket(SOCK_STREAM, PORT);
  if (SERVER_FD < 0) {
    return result;
  }

  if (::listen(SERVER_FD, 1) < 0) {
    ::close(SERVER_FD);
    return result;
  }

  // Echo server thread - captures by value for safety
  std::atomic<bool> serverRunning{true};
  std::thread serverThread([SERVER_FD, messageSize, &serverRunning]() {
    struct sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);

    const int CLIENT =
        ::accept(SERVER_FD, reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);
    if (CLIENT < 0) {
      return;
    }

    setTcpNoDelay(CLIENT);

    char buf[65536];
    while (serverRunning.load(std::memory_order_relaxed)) {
      const ssize_t N = ::recv(CLIENT, buf, messageSize, 0);
      if (N <= 0) {
        break;
      }
      ::send(CLIENT, buf, static_cast<size_t>(N), 0);
    }

    ::close(CLIENT);
  });

  // Give server time to accept
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  // Client socket
  const int CLIENT_FD = createClientSocket(SOCK_STREAM, PORT);
  if (CLIENT_FD < 0) {
    serverRunning.store(false, std::memory_order_relaxed);
    ::close(SERVER_FD);
    serverThread.join();
    return result;
  }

  setTcpNoDelay(CLIENT_FD);
  setSocketTimeout(CLIENT_FD, SOCKET_TIMEOUT_MS);

  // Prepare message buffer
  char sendBuf[65536];
  char recvBuf[65536];
  std::memset(sendBuf, 'X', messageSize);

  // Run latency test
  const auto START_TIME = std::chrono::steady_clock::now();
  const auto END_TIME = START_TIME + budget;

  while (sampleCount < maxSamples && std::chrono::steady_clock::now() < END_TIME) {

    const std::uint64_t T0 = getMonotonicNs();

    const ssize_t SENT = ::send(CLIENT_FD, sendBuf, messageSize, 0);
    if (SENT != static_cast<ssize_t>(messageSize)) {
      break;
    }

    const ssize_t RCVD = ::recv(CLIENT_FD, recvBuf, messageSize, MSG_WAITALL);
    if (RCVD != static_cast<ssize_t>(messageSize)) {
      break;
    }

    const std::uint64_t T1 = getMonotonicNs();

    samples[sampleCount] = static_cast<double>(T1 - T0) / 1000.0; // ns to us
    ++sampleCount;
  }

  // Cleanup
  serverRunning.store(false, std::memory_order_relaxed);
  ::close(CLIENT_FD);
  ::close(SERVER_FD);
  serverThread.join();

  // Compute statistics
  computeStats(samples, sampleCount, result);

  return result;
}

/* ----------------------------- UDP Latency ----------------------------- */

LatencyResult measureUdpLatency(std::chrono::milliseconds budget, std::size_t messageSize,
                                std::size_t maxSamples) noexcept {
  LatencyResult result{};

  // Clamp parameters (UDP max practical size)
  if (messageSize == 0 || messageSize > 65000) {
    messageSize = DEFAULT_LATENCY_MESSAGE_SIZE;
  }
  if (maxSamples == 0 || maxSamples > MAX_LATENCY_SAMPLES) {
    maxSamples = MAX_LATENCY_SAMPLES;
  }

  const int PORT = allocatePort();

  double samples[MAX_LATENCY_SAMPLES];
  std::size_t sampleCount = 0;

  // Server socket
  const int SERVER_FD = createServerSocket(SOCK_DGRAM, PORT);
  if (SERVER_FD < 0) {
    return result;
  }

  // Echo server thread
  std::atomic<bool> serverRunning{true};
  std::thread serverThread([SERVER_FD, &serverRunning]() {
    char buf[65536];
    struct sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);

    setSocketTimeout(SERVER_FD, 50);

    while (serverRunning.load(std::memory_order_relaxed)) {
      addrLen = sizeof(clientAddr);
      const ssize_t N = ::recvfrom(SERVER_FD, buf, sizeof(buf), 0,
                                   reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);
      if (N > 0) {
        ::sendto(SERVER_FD, buf, static_cast<size_t>(N), 0,
                 reinterpret_cast<struct sockaddr*>(&clientAddr), addrLen);
      }
    }
  });

  // Give server time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  // Client socket
  const int CLIENT_FD = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (CLIENT_FD < 0) {
    serverRunning.store(false, std::memory_order_relaxed);
    ::close(SERVER_FD);
    serverThread.join();
    return result;
  }

  setSocketTimeout(CLIENT_FD, SOCKET_TIMEOUT_MS);

  struct sockaddr_in serverAddr{};
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(static_cast<uint16_t>(PORT));
  ::inet_pton(AF_INET, LOOPBACK_ADDR, &serverAddr.sin_addr);

  char sendBuf[65536];
  char recvBuf[65536];
  std::memset(sendBuf, 'Y', messageSize);

  const auto START_TIME = std::chrono::steady_clock::now();
  const auto END_TIME = START_TIME + budget;

  while (sampleCount < maxSamples && std::chrono::steady_clock::now() < END_TIME) {

    const std::uint64_t T0 = getMonotonicNs();

    const ssize_t SENT =
        ::sendto(CLIENT_FD, sendBuf, messageSize, 0,
                 reinterpret_cast<struct sockaddr*>(&serverAddr), sizeof(serverAddr));
    if (SENT != static_cast<ssize_t>(messageSize)) {
      continue; // UDP can drop, try again
    }

    struct sockaddr_in fromAddr{};
    socklen_t fromLen = sizeof(fromAddr);
    const ssize_t RCVD = ::recvfrom(CLIENT_FD, recvBuf, messageSize, 0,
                                    reinterpret_cast<struct sockaddr*>(&fromAddr), &fromLen);
    if (RCVD != static_cast<ssize_t>(messageSize)) {
      continue; // Timeout or partial, skip this sample
    }

    const std::uint64_t T1 = getMonotonicNs();

    samples[sampleCount] = static_cast<double>(T1 - T0) / 1000.0;
    ++sampleCount;
  }

  // Cleanup
  serverRunning.store(false, std::memory_order_relaxed);
  ::close(CLIENT_FD);
  ::close(SERVER_FD);
  serverThread.join();

  computeStats(samples, sampleCount, result);

  return result;
}

/* ----------------------------- TCP Throughput ----------------------------- */

ThroughputResult measureTcpThroughput(std::chrono::milliseconds budget,
                                      std::size_t bufferSize) noexcept {
  ThroughputResult result{};

  if (bufferSize == 0 || bufferSize > 1024 * 1024) {
    bufferSize = DEFAULT_THROUGHPUT_BUFFER_SIZE;
  }

  const int PORT = allocatePort();

  const int SERVER_FD = createServerSocket(SOCK_STREAM, PORT);
  if (SERVER_FD < 0) {
    return result;
  }

  if (::listen(SERVER_FD, 1) < 0) {
    ::close(SERVER_FD);
    return result;
  }

  // Sink server - receives and discards
  std::atomic<bool> serverRunning{true};
  std::atomic<std::uint64_t> bytesReceived{0};

  std::thread serverThread([SERVER_FD, bufferSize, &serverRunning, &bytesReceived]() {
    struct sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);

    const int CLIENT =
        ::accept(SERVER_FD, reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);
    if (CLIENT < 0) {
      return;
    }

    char* buf = new (std::nothrow) char[bufferSize];
    if (buf == nullptr) {
      ::close(CLIENT);
      return;
    }

    while (serverRunning.load(std::memory_order_relaxed)) {
      const ssize_t N = ::recv(CLIENT, buf, bufferSize, 0);
      if (N <= 0) {
        break;
      }
      bytesReceived.fetch_add(static_cast<std::uint64_t>(N), std::memory_order_relaxed);
    }

    delete[] buf;
    ::close(CLIENT);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  const int CLIENT_FD = createClientSocket(SOCK_STREAM, PORT);
  if (CLIENT_FD < 0) {
    serverRunning.store(false, std::memory_order_relaxed);
    ::close(SERVER_FD);
    serverThread.join();
    return result;
  }

  char* sendBuf = new (std::nothrow) char[bufferSize];
  if (sendBuf == nullptr) {
    serverRunning.store(false, std::memory_order_relaxed);
    ::close(CLIENT_FD);
    ::close(SERVER_FD);
    serverThread.join();
    return result;
  }
  std::memset(sendBuf, 'Z', bufferSize);

  const auto START_TIME = std::chrono::steady_clock::now();
  const auto END_TIME = START_TIME + budget;

  while (std::chrono::steady_clock::now() < END_TIME) {
    const ssize_t SENT = ::send(CLIENT_FD, sendBuf, bufferSize, 0);
    if (SENT <= 0) {
      break;
    }
  }

  const auto ACTUAL_END = std::chrono::steady_clock::now();

  // Wait briefly for server to drain
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  serverRunning.store(false, std::memory_order_relaxed);
  ::shutdown(CLIENT_FD, SHUT_RDWR);
  ::close(CLIENT_FD);
  ::close(SERVER_FD);
  serverThread.join();

  delete[] sendBuf;

  // Calculate results
  result.durationSec = std::chrono::duration<double>(ACTUAL_END - START_TIME).count();
  result.bytesTransferred = bytesReceived.load(std::memory_order_relaxed);

  if (result.durationSec > 0.0 && result.bytesTransferred > 0) {
    result.mibPerSec =
        static_cast<double>(result.bytesTransferred) / (1024.0 * 1024.0) / result.durationSec;
    result.mbitsPerSec =
        static_cast<double>(result.bytesTransferred) * 8.0 / 1'000'000.0 / result.durationSec;
    result.success = true;
  }

  return result;
}

/* ----------------------------- UDP Throughput ----------------------------- */

ThroughputResult measureUdpThroughput(std::chrono::milliseconds budget,
                                      std::size_t bufferSize) noexcept {
  ThroughputResult result{};

  // UDP practical limit
  if (bufferSize == 0 || bufferSize > 65000) {
    bufferSize = 65000;
  }

  const int PORT = allocatePort();

  const int SERVER_FD = createServerSocket(SOCK_DGRAM, PORT);
  if (SERVER_FD < 0) {
    return result;
  }

  std::atomic<bool> serverRunning{true};
  std::atomic<std::uint64_t> bytesReceived{0};

  std::thread serverThread([SERVER_FD, bufferSize, &serverRunning, &bytesReceived]() {
    char* buf = new (std::nothrow) char[bufferSize];
    if (buf == nullptr) {
      return;
    }

    setSocketTimeout(SERVER_FD, 50);

    while (serverRunning.load(std::memory_order_relaxed)) {
      const ssize_t N = ::recv(SERVER_FD, buf, bufferSize, 0);
      if (N > 0) {
        bytesReceived.fetch_add(static_cast<std::uint64_t>(N), std::memory_order_relaxed);
      }
    }

    delete[] buf;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  const int CLIENT_FD = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (CLIENT_FD < 0) {
    serverRunning.store(false, std::memory_order_relaxed);
    ::close(SERVER_FD);
    serverThread.join();
    return result;
  }

  struct sockaddr_in serverAddr{};
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(static_cast<uint16_t>(PORT));
  ::inet_pton(AF_INET, LOOPBACK_ADDR, &serverAddr.sin_addr);

  char* sendBuf = new (std::nothrow) char[bufferSize];
  if (sendBuf == nullptr) {
    serverRunning.store(false, std::memory_order_relaxed);
    ::close(CLIENT_FD);
    ::close(SERVER_FD);
    serverThread.join();
    return result;
  }
  std::memset(sendBuf, 'W', bufferSize);

  const auto START_TIME = std::chrono::steady_clock::now();
  const auto END_TIME = START_TIME + budget;

  while (std::chrono::steady_clock::now() < END_TIME) {
    ::sendto(CLIENT_FD, sendBuf, bufferSize, 0, reinterpret_cast<struct sockaddr*>(&serverAddr),
             sizeof(serverAddr));
    // UDP sends can fail silently, we just keep going
  }

  const auto ACTUAL_END = std::chrono::steady_clock::now();

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  serverRunning.store(false, std::memory_order_relaxed);
  ::close(CLIENT_FD);
  ::close(SERVER_FD);
  serverThread.join();

  delete[] sendBuf;

  result.durationSec = std::chrono::duration<double>(ACTUAL_END - START_TIME).count();
  result.bytesTransferred = bytesReceived.load(std::memory_order_relaxed);

  if (result.durationSec > 0.0 && result.bytesTransferred > 0) {
    result.mibPerSec =
        static_cast<double>(result.bytesTransferred) / (1024.0 * 1024.0) / result.durationSec;
    result.mbitsPerSec =
        static_cast<double>(result.bytesTransferred) * 8.0 / 1'000'000.0 / result.durationSec;
    result.success = true;
  }

  return result;
}

/* ----------------------------- Combined Benchmark ----------------------------- */

LoopbackBenchResult runLoopbackBench(std::chrono::milliseconds budget) noexcept {
  LoopbackBenchConfig config{};
  config.totalBudget = budget;
  return runLoopbackBench(config);
}

LoopbackBenchResult runLoopbackBench(const LoopbackBenchConfig& config) noexcept {
  LoopbackBenchResult result{};

  // Count enabled tests
  int enabledTests = 0;
  if (config.runTcpLatency) {
    ++enabledTests;
  }
  if (config.runUdpLatency) {
    ++enabledTests;
  }
  if (config.runTcpThroughput) {
    ++enabledTests;
  }
  if (config.runUdpThroughput) {
    ++enabledTests;
  }

  if (enabledTests == 0) {
    return result;
  }

  // Divide budget among tests
  const auto PER_TEST = config.totalBudget / enabledTests;

  if (config.runTcpLatency) {
    result.tcpLatency =
        measureTcpLatency(PER_TEST, config.latencyMessageSize, config.maxLatencySamples);
  }

  if (config.runUdpLatency) {
    result.udpLatency =
        measureUdpLatency(PER_TEST, config.latencyMessageSize, config.maxLatencySamples);
  }

  if (config.runTcpThroughput) {
    result.tcpThroughput = measureTcpThroughput(PER_TEST, config.throughputBufferSize);
  }

  if (config.runUdpThroughput) {
    result.udpThroughput = measureUdpThroughput(PER_TEST, config.throughputBufferSize);
  }

  return result;
}

} // namespace network

} // namespace seeker
