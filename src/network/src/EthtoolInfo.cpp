/**
 * @file EthtoolInfo.cpp
 * @brief Implementation of NIC ethtool information queries.
 */

#include "src/network/inc/EthtoolInfo.hpp"
#include "src/network/inc/InterfaceInfo.hpp"
#include "src/helpers/inc/Files.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace network {

using seeker::helpers::files::pathExists;
using seeker::helpers::files::readFileInt;
using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::strings::copyToFixedArray;

namespace {

/* ----------------------------- Constants ----------------------------- */

constexpr const char* NET_SYS_PATH = "/sys/class/net";

/* ----------------------------- Socket Helper ----------------------------- */

/**
 * Create a socket for ioctl operations.
 * Returns -1 on failure.
 */
inline int createSocket() noexcept { return ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0); }

/* ----------------------------- Ethtool Ioctl Helpers ----------------------------- */

/**
 * Perform ethtool ioctl.
 * Returns true on success.
 */
inline bool ethtoolIoctl(int sock, const char* ifname, void* data) noexcept {
  struct ifreq ifr{};
  std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
  ifr.ifr_data = static_cast<char*>(data);
  return ::ioctl(sock, SIOCETHTOOL, &ifr) == 0;
}

/**
 * Query ring buffer parameters.
 */
inline RingBufferConfig queryRingParams(int sock, const char* ifname) noexcept {
  RingBufferConfig cfg{};

  struct ethtool_ringparam ring{};
  ring.cmd = ETHTOOL_GRINGPARAM;

  if (ethtoolIoctl(sock, ifname, &ring)) {
    cfg.rxPending = ring.rx_pending;
    cfg.rxMax = ring.rx_max_pending;
    cfg.txPending = ring.tx_pending;
    cfg.txMax = ring.tx_max_pending;
    cfg.rxMiniPending = ring.rx_mini_pending;
    cfg.rxMiniMax = ring.rx_mini_max_pending;
    cfg.rxJumboPending = ring.rx_jumbo_pending;
    cfg.rxJumboMax = ring.rx_jumbo_max_pending;
  }

  return cfg;
}

/**
 * Query coalescing parameters.
 */
inline CoalesceConfig queryCoalesce(int sock, const char* ifname) noexcept {
  CoalesceConfig cfg{};

  struct ethtool_coalesce coal{};
  coal.cmd = ETHTOOL_GCOALESCE;

  if (ethtoolIoctl(sock, ifname, &coal)) {
    cfg.rxUsecs = coal.rx_coalesce_usecs;
    cfg.rxMaxFrames = coal.rx_max_coalesced_frames;
    cfg.txUsecs = coal.tx_coalesce_usecs;
    cfg.txMaxFrames = coal.tx_max_coalesced_frames;
    cfg.rxUsecsIrq = coal.rx_coalesce_usecs_irq;
    cfg.rxMaxFramesIrq = coal.rx_max_coalesced_frames_irq;
    cfg.txUsecsIrq = coal.tx_coalesce_usecs_irq;
    cfg.txMaxFramesIrq = coal.tx_max_coalesced_frames_irq;
    cfg.statsBlockUsecs = coal.stats_block_coalesce_usecs;
    cfg.useAdaptiveRx = (coal.use_adaptive_rx_coalesce != 0);
    cfg.useAdaptiveTx = (coal.use_adaptive_tx_coalesce != 0);
    cfg.pktRateLow = coal.pkt_rate_low;
    cfg.pktRateHigh = coal.pkt_rate_high;
    cfg.rxUsecsLow = coal.rx_coalesce_usecs_low;
    cfg.rxUsecsHigh = coal.rx_coalesce_usecs_high;
    cfg.txUsecsLow = coal.tx_coalesce_usecs_low;
    cfg.txUsecsHigh = coal.tx_coalesce_usecs_high;
  }

  return cfg;
}

/**
 * Query pause frame parameters.
 */
inline PauseConfig queryPause(int sock, const char* ifname) noexcept {
  PauseConfig cfg{};

  struct ethtool_pauseparam pause{};
  pause.cmd = ETHTOOL_GPAUSEPARAM;

  if (ethtoolIoctl(sock, ifname, &pause)) {
    cfg.autoneg = (pause.autoneg != 0);
    cfg.rxPause = (pause.rx_pause != 0);
    cfg.txPause = (pause.tx_pause != 0);
  }

  return cfg;
}

/**
 * Query NIC features using ETHTOOL_GFEATURES.
 * This is more complex as it requires querying string sets first.
 */
inline NicFeatures queryFeatures(int sock, const char* ifname) noexcept {
  NicFeatures result{};

  // First, get the number of features via ETHTOOL_GSSET_INFO.
  // Use a byte buffer to avoid GCC flexible-array-member constraints.
  alignas(struct ethtool_sset_info)
      std::uint8_t ssetBuf[sizeof(struct ethtool_sset_info) + sizeof(std::uint32_t)]{};
  auto* ssetInfo = reinterpret_cast<struct ethtool_sset_info*>(ssetBuf);

  ssetInfo->cmd = ETHTOOL_GSSET_INFO;
  ssetInfo->sset_mask = (1ULL << ETH_SS_FEATURES);

  if (!ethtoolIoctl(sock, ifname, ssetInfo)) {
    return result;
  }

  if ((ssetInfo->sset_mask & (1ULL << ETH_SS_FEATURES)) == 0) {
    return result;
  }

  const std::uint32_t FEATURE_COUNT = ssetInfo->data[0];
  if (FEATURE_COUNT == 0 || FEATURE_COUNT > 1024) {
    return result;
  }

  // Calculate sizes for feature query
  const std::size_t FEATURE_WORDS = (FEATURE_COUNT + 31) / 32;

  // Query feature strings (names)
  // Buffer: ethtool_gstrings header + string data
  constexpr std::size_t STRING_LEN = ETH_GSTRING_LEN; // 32 bytes per string
  const std::size_t STRINGS_BUF_SIZE = sizeof(struct ethtool_gstrings) + FEATURE_COUNT * STRING_LEN;

  // Use stack buffer with reasonable limit
  if (STRINGS_BUF_SIZE > 32768) {
    return result; // Too many features, skip
  }

  alignas(struct ethtool_gstrings) char stringsBuf[32768]{};
  auto* strings = reinterpret_cast<struct ethtool_gstrings*>(stringsBuf);
  strings->cmd = ETHTOOL_GSTRINGS;
  strings->string_set = ETH_SS_FEATURES;
  strings->len = FEATURE_COUNT;

  if (!ethtoolIoctl(sock, ifname, strings)) {
    return result;
  }

  // Query feature states
  const std::size_t FEATURES_BUF_SIZE =
      sizeof(struct ethtool_gfeatures) + FEATURE_WORDS * sizeof(struct ethtool_get_features_block);

  if (FEATURES_BUF_SIZE > 4096) {
    return result;
  }

  alignas(struct ethtool_gfeatures) char featuresBuf[4096]{};
  auto* features = reinterpret_cast<struct ethtool_gfeatures*>(featuresBuf);
  features->cmd = ETHTOOL_GFEATURES;
  features->size = static_cast<std::uint32_t>(FEATURE_WORDS);

  if (!ethtoolIoctl(sock, ifname, features)) {
    return result;
  }

  // Parse features into our structure
  const std::size_t MAX_TO_COPY = (FEATURE_COUNT < MAX_FEATURES) ? FEATURE_COUNT : MAX_FEATURES;

  for (std::size_t i = 0; i < MAX_TO_COPY; ++i) {
    NicFeature& feat = result.features[result.count];

    // Copy feature name
    const char* namePtr = reinterpret_cast<const char*>(strings->data) + i * STRING_LEN;
    copyToFixedArray(feat.name, namePtr);

    // Get feature state from bitfields
    const std::size_t WORD_IDX = i / 32;
    const std::uint32_t BIT_MASK = 1U << (i % 32);

    const struct ethtool_get_features_block& block = features->features[WORD_IDX];

    feat.available = (block.available & BIT_MASK) != 0;
    feat.enabled = (block.active & BIT_MASK) != 0;
    feat.requested = (block.requested & BIT_MASK) != 0;
    feat.fixed = (block.never_changed & BIT_MASK) != 0;

    ++result.count;
  }

  return result;
}

} // namespace

/* ----------------------------- RingBufferConfig Methods ----------------------------- */

bool RingBufferConfig::isValid() const noexcept { return rxMax > 0 || txMax > 0; }

bool RingBufferConfig::isRxAtMax() const noexcept { return rxMax > 0 && rxPending >= rxMax; }

bool RingBufferConfig::isTxAtMax() const noexcept { return txMax > 0 && txPending >= txMax; }

bool RingBufferConfig::isRtFriendly() const noexcept {
  // Large ring buffers add latency - warn if excessive
  return rxPending <= RT_RING_SIZE_WARN_THRESHOLD && txPending <= RT_RING_SIZE_WARN_THRESHOLD;
}

std::string RingBufferConfig::toString() const {
  if (!isValid()) {
    return "Ring buffers: not available";
  }

  return fmt::format("Ring buffers: RX {}/{} TX {}/{}", rxPending, rxMax, txPending, txMax);
}

/* ----------------------------- CoalesceConfig Methods ----------------------------- */

bool CoalesceConfig::isValid() const noexcept {
  // At least one field should be set if query succeeded
  // Note: all zeros is valid (no coalescing)
  return true; // We can't easily distinguish "not supported" from "all zeros"
}

bool CoalesceConfig::isLowLatency() const noexcept {
  return rxUsecs <= LOW_LATENCY_USECS_THRESHOLD && txUsecs <= LOW_LATENCY_USECS_THRESHOLD &&
         rxMaxFrames <= LOW_LATENCY_FRAMES_THRESHOLD &&
         txMaxFrames <= LOW_LATENCY_FRAMES_THRESHOLD && !useAdaptiveRx && !useAdaptiveTx;
}

bool CoalesceConfig::hasAdaptive() const noexcept { return useAdaptiveRx || useAdaptiveTx; }

bool CoalesceConfig::isRtFriendly() const noexcept {
  // Adaptive coalescing is unpredictable - bad for RT
  if (hasAdaptive()) {
    return false;
  }
  // Low coalescing values are good
  return isLowLatency();
}

std::string CoalesceConfig::toString() const {
  std::string out;
  out += fmt::format("Coalescing: RX {}us/{} frames, TX {}us/{} frames", rxUsecs, rxMaxFrames,
                     txUsecs, txMaxFrames);

  if (hasAdaptive()) {
    out += fmt::format(" [adaptive: RX={} TX={}]", useAdaptiveRx ? "on" : "off",
                       useAdaptiveTx ? "on" : "off");
  }

  return out;
}

/* ----------------------------- PauseConfig Methods ----------------------------- */

bool PauseConfig::isEnabled() const noexcept { return rxPause || txPause; }

std::string PauseConfig::toString() const {
  if (!isEnabled()) {
    return "Pause: disabled";
  }

  std::string out = "Pause:";
  if (rxPause) {
    out += " RX";
  }
  if (txPause) {
    out += " TX";
  }
  if (autoneg) {
    out += " (autoneg)";
  }

  return out;
}

/* ----------------------------- NicFeatures Methods ----------------------------- */

const NicFeature* NicFeatures::find(const char* name) const noexcept {
  if (name == nullptr) {
    return nullptr;
  }

  for (std::size_t i = 0; i < count; ++i) {
    if (std::strcmp(features[i].name.data(), name) == 0) {
      return &features[i];
    }
  }

  return nullptr;
}

bool NicFeatures::isEnabled(const char* name) const noexcept {
  const NicFeature* feat = find(name);
  return feat != nullptr && feat->enabled;
}

std::size_t NicFeatures::countEnabled() const noexcept {
  std::size_t enabled = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (features[i].enabled) {
      ++enabled;
    }
  }
  return enabled;
}

std::string NicFeatures::toString() const {
  if (count == 0) {
    return "Features: not available";
  }

  std::string out = fmt::format("Features: {} total, {} enabled\n", count, countEnabled());

  for (std::size_t i = 0; i < count; ++i) {
    const NicFeature& f = features[i];
    if (f.name[0] == '\0') {
      continue;
    }

    out += fmt::format("  {}: {}", f.name.data(), f.enabled ? "on" : "off");
    if (f.fixed) {
      out += " [fixed]";
    }
    out += '\n';
  }

  return out;
}

/* ----------------------------- EthtoolInfo Methods ----------------------------- */

bool EthtoolInfo::hasTso() const noexcept {
  return features.isEnabled("tx-tcp-segmentation") || features.isEnabled("tx-tcp6-segmentation") ||
         features.isEnabled("tcp-segmentation-offload");
}

bool EthtoolInfo::hasGro() const noexcept { return features.isEnabled("rx-gro"); }

bool EthtoolInfo::hasGso() const noexcept { return features.isEnabled("tx-generic-segmentation"); }

bool EthtoolInfo::hasLro() const noexcept { return features.isEnabled("rx-lro"); }

bool EthtoolInfo::hasRxChecksum() const noexcept { return features.isEnabled("rx-checksum"); }

bool EthtoolInfo::hasTxChecksum() const noexcept {
  return features.isEnabled("tx-checksum-ipv4") || features.isEnabled("tx-checksum-ipv6") ||
         features.isEnabled("tx-checksum-ip-generic");
}

bool EthtoolInfo::hasScatterGather() const noexcept {
  return features.isEnabled("tx-scatter-gather") ||
         features.isEnabled("tx-scatter-gather-fraglist");
}

bool EthtoolInfo::isRtFriendly() const noexcept {
  if (!supportsEthtool) {
    return true; // Can't assess, assume OK
  }

  // Check coalescing
  if (!coalesce.isRtFriendly()) {
    return false;
  }

  // Check ring buffers
  if (rings.isValid() && !rings.isRtFriendly()) {
    return false;
  }

  // LRO adds latency variance
  if (hasLro()) {
    return false;
  }

  return true;
}

int EthtoolInfo::rtScore() const noexcept {
  if (!supportsEthtool) {
    return 50; // Unknown, middle score
  }

  int score = 100;

  // Coalescing assessment (up to -40 points)
  if (coalesce.hasAdaptive()) {
    score -= 20; // Adaptive is unpredictable
  }
  if (coalesce.rxUsecs > 100) {
    score -= 15;
  } else if (coalesce.rxUsecs > 50) {
    score -= 10;
  } else if (coalesce.rxUsecs > LOW_LATENCY_USECS_THRESHOLD) {
    score -= 5;
  }

  if (coalesce.txUsecs > 100) {
    score -= 10;
  } else if (coalesce.txUsecs > 50) {
    score -= 5;
  }

  // Ring buffer assessment (up to -20 points)
  if (rings.isValid()) {
    if (rings.rxPending > 8192) {
      score -= 15;
    } else if (rings.rxPending > RT_RING_SIZE_WARN_THRESHOLD) {
      score -= 10;
    } else if (rings.rxPending > 2048) {
      score -= 5;
    }
  }

  // Feature assessment (up to -20 points)
  if (hasLro()) {
    score -= 15; // LRO adds significant latency variance
  }

  // Pause frames (up to -10 points)
  if (pause.isEnabled()) {
    score -= 10; // Can cause unpredictable stalls
  }

  return (score < 0) ? 0 : score;
}

std::string EthtoolInfo::toString() const {
  std::string out;
  out += fmt::format("Ethtool info for {}\n", ifname.data());

  if (!supportsEthtool) {
    out += "  ethtool not supported\n";
    return out;
  }

  out += "  " + rings.toString() + "\n";
  out += "  " + coalesce.toString() + "\n";
  out += "  " + pause.toString() + "\n";

  // Key features summary
  out += "  Key offloads:";
  if (hasTso()) {
    out += " TSO";
  }
  if (hasGro()) {
    out += " GRO";
  }
  if (hasGso()) {
    out += " GSO";
  }
  if (hasLro()) {
    out += " LRO";
  }
  if (hasRxChecksum()) {
    out += " RX-csum";
  }
  if (hasTxChecksum()) {
    out += " TX-csum";
  }
  out += "\n";

  out += fmt::format("  RT score: {}/100 ({})\n", rtScore(),
                     isRtFriendly() ? "RT-friendly" : "needs tuning");

  return out;
}

/* ----------------------------- EthtoolInfoList Methods ----------------------------- */

const EthtoolInfo* EthtoolInfoList::find(const char* ifname) const noexcept {
  if (ifname == nullptr) {
    return nullptr;
  }

  for (std::size_t i = 0; i < count; ++i) {
    if (std::strcmp(nics[i].ifname.data(), ifname) == 0) {
      return &nics[i];
    }
  }

  return nullptr;
}

bool EthtoolInfoList::empty() const noexcept { return count == 0; }

std::string EthtoolInfoList::toString() const {
  if (count == 0) {
    return "No ethtool information available";
  }

  std::string out;
  for (std::size_t i = 0; i < count; ++i) {
    if (i > 0) {
      out += '\n';
    }
    out += nics[i].toString();
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

EthtoolInfo getEthtoolInfo(const char* ifname) noexcept {
  EthtoolInfo info{};

  if (ifname == nullptr || ifname[0] == '\0') {
    return info;
  }

  copyToFixedArray(info.ifname, ifname);

  const int SOCK = createSocket();
  if (SOCK < 0) {
    return info;
  }

  // Query each component independently - some may not be supported
  info.rings = queryRingParams(SOCK, ifname);
  info.coalesce = queryCoalesce(SOCK, ifname);
  info.pause = queryPause(SOCK, ifname);
  info.features = queryFeatures(SOCK, ifname);

  ::close(SOCK);

  // Mark as supported if any query succeeded
  info.supportsEthtool = info.rings.isValid() || info.features.count > 0;

  return info;
}

EthtoolInfoList getAllEthtoolInfo() noexcept {
  EthtoolInfoList list{};

  DIR* dir = ::opendir(NET_SYS_PATH);
  if (dir == nullptr) {
    return list;
  }

  struct dirent* entry = nullptr;
  while ((entry = ::readdir(dir)) != nullptr && list.count < MAX_INTERFACES) {
    // Skip . and ..
    if (entry->d_name[0] == '.') {
      continue;
    }

    // Skip virtual interfaces
    if (isVirtualInterface(entry->d_name)) {
      continue;
    }

    list.nics[list.count] = getEthtoolInfo(entry->d_name);

    // Only count if ethtool query succeeded
    if (list.nics[list.count].supportsEthtool) {
      ++list.count;
    }
  }

  ::closedir(dir);
  return list;
}

RingBufferConfig getRingBufferConfig(const char* ifname) noexcept {
  if (ifname == nullptr || ifname[0] == '\0') {
    return RingBufferConfig{};
  }

  const int SOCK = createSocket();
  if (SOCK < 0) {
    return RingBufferConfig{};
  }

  RingBufferConfig cfg = queryRingParams(SOCK, ifname);
  ::close(SOCK);

  return cfg;
}

CoalesceConfig getCoalesceConfig(const char* ifname) noexcept {
  if (ifname == nullptr || ifname[0] == '\0') {
    return CoalesceConfig{};
  }

  const int SOCK = createSocket();
  if (SOCK < 0) {
    return CoalesceConfig{};
  }

  CoalesceConfig cfg = queryCoalesce(SOCK, ifname);
  ::close(SOCK);

  return cfg;
}

PauseConfig getPauseConfig(const char* ifname) noexcept {
  if (ifname == nullptr || ifname[0] == '\0') {
    return PauseConfig{};
  }

  const int SOCK = createSocket();
  if (SOCK < 0) {
    return PauseConfig{};
  }

  PauseConfig cfg = queryPause(SOCK, ifname);
  ::close(SOCK);

  return cfg;
}

} // namespace network

} // namespace seeker