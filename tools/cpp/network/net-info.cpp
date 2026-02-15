/**
 * @file net-info.cpp
 * @brief One-shot network interface and configuration dump.
 *
 * Displays NIC information, socket buffer configuration, ethtool settings,
 * and busy polling status. Designed for quick network subsystem assessment.
 */

#include "src/network/inc/EthtoolInfo.hpp"
#include "src/network/inc/InterfaceInfo.hpp"
#include "src/network/inc/InterfaceStats.hpp"
#include "src/network/inc/NetworkIsolation.hpp"
#include "src/network/inc/SocketBufferConfig.hpp"
#include "src/helpers/inc/Args.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace net = seeker::network;

namespace {

/* ----------------------------- Argument Handling ----------------------------- */

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_PHYSICAL = 2,
  ARG_VERBOSE = 3,
  ARG_ETHTOOL = 4,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Display network interface information, socket buffers, and configuration.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_PHYSICAL] = {"--physical", 0, false, "Show only physical interfaces"};
  map[ARG_VERBOSE] = {"--verbose", 0, false, "Show detailed information"};
  map[ARG_ETHTOOL] = {"--ethtool", 0, false,
                      "Show ethtool details (ring buffers, coalescing, features)"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printInterfaces(const net::InterfaceList& interfaces, bool verbose) {
  fmt::print("=== Network Interfaces ({}) ===\n", interfaces.count);

  for (std::size_t i = 0; i < interfaces.count; ++i) {
    const net::InterfaceInfo& iface = interfaces.interfaces[i];

    // Basic info line
    fmt::print("  {}: ", iface.ifname.data());

    if (iface.operState[0] != '\0') {
      fmt::print("{}", iface.operState.data());
    } else {
      fmt::print("unknown");
    }

    if (iface.speedMbps > 0) {
      fmt::print(" {}", net::formatSpeed(iface.speedMbps));
    }

    if (iface.duplex[0] != '\0' && std::strcmp(iface.duplex.data(), "unknown") != 0) {
      fmt::print(" {}", iface.duplex.data());
    }

    fmt::print(" mtu={}", iface.mtu);

    if (iface.isPhysical()) {
      fmt::print(" [physical]");
    }

    fmt::print("\n");

    if (verbose) {
      if (iface.macAddress[0] != '\0') {
        fmt::print("      MAC: {}\n", iface.macAddress.data());
      }
      if (iface.driver[0] != '\0') {
        fmt::print("      Driver: {}\n", iface.driver.data());
      }
      if (iface.rxQueues > 0 || iface.txQueues > 0) {
        fmt::print("      Queues: rx={} tx={}\n", iface.rxQueues, iface.txQueues);
      }
      if (iface.numaNode >= 0) {
        fmt::print("      NUMA node: {}\n", iface.numaNode);
      }
    }
  }
}

void printSocketBuffers(const net::SocketBufferConfig& cfg) {
  fmt::print("\n=== Socket Buffers ===\n");

  fmt::print("  Receive:  default={} max={}\n", net::formatBufferSize(cfg.rmemDefault),
             net::formatBufferSize(cfg.rmemMax));
  fmt::print("  Send:     default={} max={}\n", net::formatBufferSize(cfg.wmemDefault),
             net::formatBufferSize(cfg.wmemMax));

  fmt::print("  TCP recv: {} / {} / {}\n", net::formatBufferSize(cfg.tcpRmemMin),
             net::formatBufferSize(cfg.tcpRmemDefault), net::formatBufferSize(cfg.tcpRmemMax));
  fmt::print("  TCP send: {} / {} / {}\n", net::formatBufferSize(cfg.tcpWmemMin),
             net::formatBufferSize(cfg.tcpWmemDefault), net::formatBufferSize(cfg.tcpWmemMax));

  if (cfg.tcpCongestionControl[0] != '\0') {
    fmt::print("  TCP CC:   {}\n", cfg.tcpCongestionControl.data());
  }
}

void printBusyPolling(const net::SocketBufferConfig& cfg) {
  fmt::print("\n=== Busy Polling ===\n");

  if (cfg.isBusyPollingEnabled()) {
    fmt::print("  Status:    ENABLED\n");
    fmt::print("  busy_read: {} us\n", cfg.busyRead);
    fmt::print("  busy_poll: {} us\n", cfg.busyPoll);
  } else {
    fmt::print("  Status:    disabled\n");
    fmt::print("  (Set /proc/sys/net/core/busy_read and busy_poll to enable)\n");
  }
}

void printNetworkIrqs(const net::NetworkIsolation& ni, bool verbose) {
  if (ni.nicCount == 0) {
    return;
  }

  fmt::print("\n=== NIC IRQs ===\n");

  for (std::size_t i = 0; i < ni.nicCount; ++i) {
    const net::NicIrqInfo& nic = ni.nics[i];
    fmt::print("  {}: {} IRQs", nic.ifname.data(), nic.irqCount);

    if (nic.numaNode >= 0) {
      fmt::print(" (NUMA {})", nic.numaNode);
    }

    fmt::print(" -> CPUs [{}]\n", nic.getAffinityCpuList());

    if (verbose && nic.irqCount > 0) {
      fmt::print("      IRQs: ");
      for (std::size_t j = 0; j < nic.irqCount && j < 8; ++j) {
        if (j > 0) {
          fmt::print(", ");
        }
        fmt::print("{}", nic.irqNumbers[j]);
      }
      if (nic.irqCount > 8) {
        fmt::print(", ... ({} more)", nic.irqCount - 8);
      }
      fmt::print("\n");
    }
  }
}

void printEthtool(const net::EthtoolInfoList& ethtoolList, bool verbose) {
  if (ethtoolList.count == 0) {
    fmt::print("\n=== Ethtool Info ===\n");
    fmt::print("  No physical NICs with ethtool support found\n");
    return;
  }

  fmt::print("\n=== Ethtool Info ===\n");

  for (std::size_t i = 0; i < ethtoolList.count; ++i) {
    const net::EthtoolInfo& eth = ethtoolList.nics[i];

    fmt::print("  {}:\n", eth.ifname.data());

    // Ring buffers
    if (eth.rings.isValid()) {
      fmt::print("      Rings: RX {}/{} TX {}/{}\n", eth.rings.rxPending, eth.rings.rxMax,
                 eth.rings.txPending, eth.rings.txMax);
    }

    // Coalescing
    fmt::print("      Coalesce: RX {}us/{} frames, TX {}us/{} frames", eth.coalesce.rxUsecs,
               eth.coalesce.rxMaxFrames, eth.coalesce.txUsecs, eth.coalesce.txMaxFrames);
    if (eth.coalesce.hasAdaptive()) {
      fmt::print(" [adaptive]");
    }
    fmt::print("\n");

    // Pause frames
    if (eth.pause.isEnabled()) {
      fmt::print("      Pause:");
      if (eth.pause.rxPause) {
        fmt::print(" RX");
      }
      if (eth.pause.txPause) {
        fmt::print(" TX");
      }
      if (eth.pause.autoneg) {
        fmt::print(" (autoneg)");
      }
      fmt::print("\n");
    }

    // Key offloads
    fmt::print("      Offloads:");
    if (eth.hasTso()) {
      fmt::print(" TSO");
    }
    if (eth.hasGro()) {
      fmt::print(" GRO");
    }
    if (eth.hasGso()) {
      fmt::print(" GSO");
    }
    if (eth.hasLro()) {
      fmt::print(" LRO");
    }
    if (eth.hasRxChecksum()) {
      fmt::print(" RX-csum");
    }
    if (eth.hasTxChecksum()) {
      fmt::print(" TX-csum");
    }
    if (eth.hasScatterGather()) {
      fmt::print(" SG");
    }
    fmt::print("\n");

    // RT assessment
    fmt::print("      RT Score: {}/100 ({})\n", eth.rtScore(),
               eth.isRtFriendly() ? "RT-friendly" : "needs tuning");

    // Verbose: all features
    if (verbose && eth.features.count > 0) {
      fmt::print("      Features ({} total, {} enabled):\n", eth.features.count,
                 eth.features.countEnabled());
      for (std::size_t j = 0; j < eth.features.count; ++j) {
        const net::NicFeature& f = eth.features.features[j];
        if (f.name[0] == '\0') {
          continue;
        }
        fmt::print("        {}: {}{}\n", f.name.data(), f.enabled ? "on" : "off",
                   f.fixed ? " [fixed]" : "");
      }
    }
  }
}

void printHuman(const net::InterfaceList& interfaces, const net::SocketBufferConfig& bufCfg,
                const net::NetworkIsolation& netIso, const net::EthtoolInfoList& ethtoolList,
                bool verbose, bool showEthtool) {
  printInterfaces(interfaces, verbose);
  printSocketBuffers(bufCfg);
  printBusyPolling(bufCfg);
  printNetworkIrqs(netIso, verbose);

  if (showEthtool) {
    printEthtool(ethtoolList, verbose);
  }

  // Summary assessment
  fmt::print("\n=== Assessment ===\n");
  if (bufCfg.isLowLatencyConfig()) {
    fmt::print("  Configuration: Low-latency optimized\n");
  } else if (bufCfg.isHighThroughputConfig()) {
    fmt::print("  Configuration: High-throughput optimized\n");
  } else {
    fmt::print("  Configuration: Default/standard\n");
  }

  // Ethtool summary
  if (showEthtool && ethtoolList.count > 0) {
    int rtFriendlyCount = 0;
    for (std::size_t i = 0; i < ethtoolList.count; ++i) {
      if (ethtoolList.nics[i].isRtFriendly()) {
        ++rtFriendlyCount;
      }
    }
    fmt::print("  NIC Tuning: {}/{} NICs RT-friendly\n", rtFriendlyCount, ethtoolList.count);
  }
}

/* ----------------------------- JSON Output ----------------------------- */

void printJson(const net::InterfaceList& interfaces, const net::SocketBufferConfig& bufCfg,
               const net::NetworkIsolation& netIso, const net::EthtoolInfoList& ethtoolList,
               bool showEthtool) {
  fmt::print("{{\n");

  // Interfaces
  fmt::print("  \"interfaces\": [\n");
  for (std::size_t i = 0; i < interfaces.count; ++i) {
    const net::InterfaceInfo& iface = interfaces.interfaces[i];
    fmt::print("    {{\n");
    fmt::print("      \"name\": \"{}\",\n", iface.ifname.data());
    fmt::print("      \"state\": \"{}\",\n", iface.operState.data());
    fmt::print("      \"speedMbps\": {},\n", iface.speedMbps);
    fmt::print("      \"duplex\": \"{}\",\n", iface.duplex.data());
    fmt::print("      \"mtu\": {},\n", iface.mtu);
    fmt::print("      \"mac\": \"{}\",\n", iface.macAddress.data());
    fmt::print("      \"driver\": \"{}\",\n", iface.driver.data());
    fmt::print("      \"rxQueues\": {},\n", iface.rxQueues);
    fmt::print("      \"txQueues\": {},\n", iface.txQueues);
    fmt::print("      \"numaNode\": {},\n", iface.numaNode);
    fmt::print("      \"isPhysical\": {}\n", iface.isPhysical());
    fmt::print("    }}{}\n", (i + 1 < interfaces.count) ? "," : "");
  }
  fmt::print("  ],\n");

  // Socket buffers
  fmt::print("  \"socketBuffers\": {{\n");
  fmt::print("    \"rmemDefault\": {},\n", bufCfg.rmemDefault);
  fmt::print("    \"rmemMax\": {},\n", bufCfg.rmemMax);
  fmt::print("    \"wmemDefault\": {},\n", bufCfg.wmemDefault);
  fmt::print("    \"wmemMax\": {},\n", bufCfg.wmemMax);
  fmt::print("    \"tcpRmem\": [{}, {}, {}],\n", bufCfg.tcpRmemMin, bufCfg.tcpRmemDefault,
             bufCfg.tcpRmemMax);
  fmt::print("    \"tcpWmem\": [{}, {}, {}],\n", bufCfg.tcpWmemMin, bufCfg.tcpWmemDefault,
             bufCfg.tcpWmemMax);
  fmt::print("    \"tcpCongestionControl\": \"{}\",\n", bufCfg.tcpCongestionControl.data());
  fmt::print("    \"busyRead\": {},\n", bufCfg.busyRead);
  fmt::print("    \"busyPoll\": {},\n", bufCfg.busyPoll);
  fmt::print("    \"busyPollingEnabled\": {}\n", bufCfg.isBusyPollingEnabled());
  fmt::print("  }},\n");

  // Network IRQs
  fmt::print("  \"nicIrqs\": [\n");
  for (std::size_t i = 0; i < netIso.nicCount; ++i) {
    const net::NicIrqInfo& nic = netIso.nics[i];
    fmt::print("    {{\n");
    fmt::print("      \"name\": \"{}\",\n", nic.ifname.data());
    fmt::print("      \"irqCount\": {},\n", nic.irqCount);
    fmt::print("      \"numaNode\": {},\n", nic.numaNode);
    fmt::print("      \"affinityCpus\": \"{}\",\n", nic.getAffinityCpuList());
    fmt::print("      \"irqs\": [");
    for (std::size_t j = 0; j < nic.irqCount; ++j) {
      if (j > 0) {
        fmt::print(", ");
      }
      fmt::print("{}", nic.irqNumbers[j]);
    }
    fmt::print("]\n");
    fmt::print("    }}{}\n", (i + 1 < netIso.nicCount) ? "," : "");
  }
  fmt::print("  ],\n");

  // Ethtool info
  if (showEthtool) {
    fmt::print("  \"ethtool\": [\n");
    for (std::size_t i = 0; i < ethtoolList.count; ++i) {
      const net::EthtoolInfo& eth = ethtoolList.nics[i];
      fmt::print("    {{\n");
      fmt::print("      \"name\": \"{}\",\n", eth.ifname.data());
      fmt::print("      \"rings\": {{\n");
      fmt::print("        \"rxPending\": {},\n", eth.rings.rxPending);
      fmt::print("        \"rxMax\": {},\n", eth.rings.rxMax);
      fmt::print("        \"txPending\": {},\n", eth.rings.txPending);
      fmt::print("        \"txMax\": {}\n", eth.rings.txMax);
      fmt::print("      }},\n");
      fmt::print("      \"coalesce\": {{\n");
      fmt::print("        \"rxUsecs\": {},\n", eth.coalesce.rxUsecs);
      fmt::print("        \"rxMaxFrames\": {},\n", eth.coalesce.rxMaxFrames);
      fmt::print("        \"txUsecs\": {},\n", eth.coalesce.txUsecs);
      fmt::print("        \"txMaxFrames\": {},\n", eth.coalesce.txMaxFrames);
      fmt::print("        \"adaptiveRx\": {},\n", eth.coalesce.useAdaptiveRx);
      fmt::print("        \"adaptiveTx\": {}\n", eth.coalesce.useAdaptiveTx);
      fmt::print("      }},\n");
      fmt::print("      \"pause\": {{\n");
      fmt::print("        \"rx\": {},\n", eth.pause.rxPause);
      fmt::print("        \"tx\": {},\n", eth.pause.txPause);
      fmt::print("        \"autoneg\": {}\n", eth.pause.autoneg);
      fmt::print("      }},\n");
      fmt::print("      \"offloads\": {{\n");
      fmt::print("        \"tso\": {},\n", eth.hasTso());
      fmt::print("        \"gro\": {},\n", eth.hasGro());
      fmt::print("        \"gso\": {},\n", eth.hasGso());
      fmt::print("        \"lro\": {},\n", eth.hasLro());
      fmt::print("        \"rxChecksum\": {},\n", eth.hasRxChecksum());
      fmt::print("        \"txChecksum\": {},\n", eth.hasTxChecksum());
      fmt::print("        \"scatterGather\": {}\n", eth.hasScatterGather());
      fmt::print("      }},\n");
      fmt::print("      \"rtScore\": {},\n", eth.rtScore());
      fmt::print("      \"rtFriendly\": {}\n", eth.isRtFriendly());
      fmt::print("    }}{}\n", (i + 1 < ethtoolList.count) ? "," : "");
    }
    fmt::print("  ],\n");
  }

  // Assessment
  fmt::print("  \"assessment\": {{\n");
  fmt::print("    \"lowLatencyReady\": {},\n", bufCfg.isLowLatencyConfig());
  fmt::print("    \"highThroughputReady\": {}\n", bufCfg.isHighThroughputConfig());
  fmt::print("  }}\n");

  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  bool physicalOnly = false;
  bool verbose = false;
  bool showEthtool = false;

  if (argc > 1) {
    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc - 1));
    for (int i = 1; i < argc; ++i) {
      args.emplace_back(argv[i]);
    }

    std::string error;
    if (!seeker::helpers::args::parseArgs(args, ARG_MAP, pargs, error)) {
      fmt::print(stderr, "Error: {}\n\n", error);
      seeker::helpers::args::printUsage(argv[0], DESCRIPTION, ARG_MAP);
      return 1;
    }

    if (pargs.count(ARG_HELP) != 0) {
      seeker::helpers::args::printUsage(argv[0], DESCRIPTION, ARG_MAP);
      return 0;
    }

    jsonOutput = (pargs.count(ARG_JSON) != 0);
    physicalOnly = (pargs.count(ARG_PHYSICAL) != 0);
    verbose = (pargs.count(ARG_VERBOSE) != 0);
    showEthtool = (pargs.count(ARG_ETHTOOL) != 0);
  }

  // Gather data
  const net::InterfaceList INTERFACES =
      physicalOnly ? net::getPhysicalInterfaces() : net::getAllInterfaces();
  const net::SocketBufferConfig BUF_CFG = net::getSocketBufferConfig();
  const net::NetworkIsolation NET_ISO = net::getNetworkIsolation();

  // Gather ethtool info if requested
  net::EthtoolInfoList ethtoolList{};
  if (showEthtool) {
    ethtoolList = net::getAllEthtoolInfo();
  }

  if (jsonOutput) {
    printJson(INTERFACES, BUF_CFG, NET_ISO, ethtoolList, showEthtool);
  } else {
    printHuman(INTERFACES, BUF_CFG, NET_ISO, ethtoolList, verbose, showEthtool);
  }

  return 0;
}