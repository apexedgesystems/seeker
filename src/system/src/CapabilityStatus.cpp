/**
 * @file CapabilityStatus.cpp
 * @brief Implementation of Linux capability status queries.
 * @note Uses capget(2) syscall directly to avoid libcap dependency.
 */

#include "src/system/inc/CapabilityStatus.hpp"

#include <linux/capability.h> // cap_user_header_t, cap_user_data_t, _LINUX_CAPABILITY_VERSION_3
#include <sys/syscall.h>      // SYS_capget
#include <unistd.h>           // syscall, geteuid

#include <fmt/core.h>

namespace seeker {

namespace system {

namespace {

/* ----------------------------- Capability Helpers ----------------------------- */

/**
 * @brief Get raw capability data via syscall.
 * @param effective Output for effective capability mask.
 * @param permitted Output for permitted capability mask.
 * @param inheritable Output for inheritable capability mask.
 * @return True on success.
 *
 * Uses syscall directly to avoid libcap dependency.
 * Capability sets are 64-bit on modern kernels (CAP_LAST_CAP > 31).
 */
bool getCapabilitySets(std::uint64_t& effective, std::uint64_t& permitted,
                       std::uint64_t& inheritable) noexcept {
  struct __user_cap_header_struct header{};
  struct __user_cap_data_struct data[2]{}; // 64 capabilities (2 x 32-bit)

  header.version = _LINUX_CAPABILITY_VERSION_3;
  header.pid = 0; // Current process

  if (::syscall(SYS_capget, &header, data) != 0) {
    return false;
  }

  // Combine low and high 32-bit words into 64-bit masks
  effective = static_cast<std::uint64_t>(data[0].effective) |
              (static_cast<std::uint64_t>(data[1].effective) << 32);
  permitted = static_cast<std::uint64_t>(data[0].permitted) |
              (static_cast<std::uint64_t>(data[1].permitted) << 32);
  inheritable = static_cast<std::uint64_t>(data[0].inheritable) |
                (static_cast<std::uint64_t>(data[1].inheritable) << 32);

  return true;
}

/// Check if bit is set in mask.
inline bool hasBit(std::uint64_t mask, int bit) noexcept {
  if (bit < 0 || bit > 63) {
    return false;
  }
  return (mask & (1ULL << bit)) != 0;
}

} // namespace

/* ----------------------------- CapabilityStatus Methods ----------------------------- */

bool CapabilityStatus::canUseRtScheduling() const noexcept { return isRoot || sysNice; }

bool CapabilityStatus::canLockMemory() const noexcept { return isRoot || ipcLock; }

bool CapabilityStatus::isPrivileged() const noexcept { return isRoot || sysAdmin; }

bool CapabilityStatus::hasCapability(int capBit) const noexcept {
  return hasBit(effective, capBit);
}

std::string CapabilityStatus::toString() const {
  std::string out;
  out.reserve(512);

  out += "Capability Status:\n";
  out += fmt::format("  Running as root: {}\n", isRoot ? "yes" : "no");

  out += "  RT-Relevant:\n";
  out += fmt::format("    CAP_SYS_NICE:     {} (RT scheduling)\n", sysNice ? "yes" : "no");
  out += fmt::format("    CAP_IPC_LOCK:     {} (memory locking)\n", ipcLock ? "yes" : "no");
  out += fmt::format("    CAP_SYS_RAWIO:    {} (direct I/O)\n", sysRawio ? "yes" : "no");
  out += fmt::format("    CAP_SYS_RESOURCE: {} (override rlimits)\n", sysResource ? "yes" : "no");

  out += "  Administrative:\n";
  out += fmt::format("    CAP_SYS_ADMIN:    {}\n", sysAdmin ? "yes" : "no");
  out += fmt::format("    CAP_NET_ADMIN:    {}\n", netAdmin ? "yes" : "no");
  out += fmt::format("    CAP_NET_RAW:      {}\n", netRaw ? "yes" : "no");
  out += fmt::format("    CAP_SYS_PTRACE:   {}\n", sysPtrace ? "yes" : "no");

  out += fmt::format("  Raw masks: eff=0x{:016x} perm=0x{:016x}\n", effective, permitted);

  return out;
}

std::string CapabilityStatus::toRtSummary() const {
  std::string out;
  out.reserve(256);

  out += "RT Capability Summary:\n";
  out += fmt::format("  RT scheduling: {}\n", canUseRtScheduling() ? "allowed" : "NOT allowed");
  out += fmt::format("  Memory locking: {}\n", canLockMemory() ? "allowed" : "NOT allowed");
  out += fmt::format("  Privileged: {}\n", isPrivileged() ? "yes" : "no");

  if (!canUseRtScheduling() && !canLockMemory()) {
    out += "  Recommendation: Run with CAP_SYS_NICE,CAP_IPC_LOCK or as root\n";
  } else if (!canUseRtScheduling()) {
    out += "  Recommendation: Add CAP_SYS_NICE for RT scheduling\n";
  } else if (!canLockMemory()) {
    out += "  Recommendation: Add CAP_IPC_LOCK for memory locking\n";
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

CapabilityStatus getCapabilityStatus() noexcept {
  CapabilityStatus status{};

  // Check root status
  status.isRoot = (::geteuid() == 0);

  // Get capability sets
  if (getCapabilitySets(status.effective, status.permitted, status.inheritable)) {
    // Extract RT-relevant capabilities
    status.sysNice = hasBit(status.effective, CAP_SYS_NICE_BIT);
    status.ipcLock = hasBit(status.effective, CAP_IPC_LOCK_BIT);
    status.sysRawio = hasBit(status.effective, CAP_SYS_RAWIO_BIT);
    status.sysResource = hasBit(status.effective, CAP_SYS_RESOURCE_BIT);

    // Extract administrative capabilities
    status.sysAdmin = hasBit(status.effective, CAP_SYS_ADMIN_BIT);
    status.netAdmin = hasBit(status.effective, CAP_NET_ADMIN_BIT);
    status.netRaw = hasBit(status.effective, CAP_NET_RAW_BIT);
    status.sysPtrace = hasBit(status.effective, CAP_SYS_PTRACE_BIT);
  }

  // Root has all capabilities implicitly
  if (status.isRoot) {
    status.sysNice = true;
    status.ipcLock = true;
    status.sysRawio = true;
    status.sysResource = true;
    status.sysAdmin = true;
    status.netAdmin = true;
    status.netRaw = true;
    status.sysPtrace = true;
  }

  return status;
}

bool hasCapability(int capBit) noexcept {
  if (capBit < 0 || capBit > 63) {
    return false;
  }

  std::uint64_t effective = 0;
  std::uint64_t permitted = 0;
  std::uint64_t inheritable = 0;

  if (!getCapabilitySets(effective, permitted, inheritable)) {
    // Fall back to root check
    return (::geteuid() == 0);
  }

  return hasBit(effective, capBit) || (::geteuid() == 0);
}

bool isRunningAsRoot() noexcept { return ::geteuid() == 0; }

const char* capabilityName(int capBit) noexcept {
  switch (capBit) {
  case 0:
    return "CAP_CHOWN";
  case 1:
    return "CAP_DAC_OVERRIDE";
  case 2:
    return "CAP_DAC_READ_SEARCH";
  case 3:
    return "CAP_FOWNER";
  case 4:
    return "CAP_FSETID";
  case 5:
    return "CAP_KILL";
  case 6:
    return "CAP_SETGID";
  case 7:
    return "CAP_SETUID";
  case 8:
    return "CAP_SETPCAP";
  case 9:
    return "CAP_LINUX_IMMUTABLE";
  case 10:
    return "CAP_NET_BIND_SERVICE";
  case 11:
    return "CAP_NET_BROADCAST";
  case CAP_NET_ADMIN_BIT:
    return "CAP_NET_ADMIN"; // 12
  case CAP_NET_RAW_BIT:
    return "CAP_NET_RAW"; // 13
  case CAP_IPC_LOCK_BIT:
    return "CAP_IPC_LOCK"; // 14
  case 15:
    return "CAP_IPC_OWNER";
  case 16:
    return "CAP_SYS_MODULE";
  case CAP_SYS_RAWIO_BIT:
    return "CAP_SYS_RAWIO"; // 17
  case 18:
    return "CAP_SYS_CHROOT";
  case CAP_SYS_PTRACE_BIT:
    return "CAP_SYS_PTRACE"; // 19
  case 20:
    return "CAP_SYS_PACCT";
  case CAP_SYS_ADMIN_BIT:
    return "CAP_SYS_ADMIN"; // 21
  case 22:
    return "CAP_SYS_BOOT";
  case CAP_SYS_NICE_BIT:
    return "CAP_SYS_NICE"; // 23
  case CAP_SYS_RESOURCE_BIT:
    return "CAP_SYS_RESOURCE"; // 24
  case 25:
    return "CAP_SYS_TIME";
  case 26:
    return "CAP_SYS_TTY_CONFIG";
  case 27:
    return "CAP_MKNOD";
  case 28:
    return "CAP_LEASE";
  case 29:
    return "CAP_AUDIT_WRITE";
  case 30:
    return "CAP_AUDIT_CONTROL";
  case 31:
    return "CAP_SETFCAP";
  case 32:
    return "CAP_MAC_OVERRIDE";
  case 33:
    return "CAP_MAC_ADMIN";
  case 34:
    return "CAP_SYSLOG";
  case 35:
    return "CAP_WAKE_ALARM";
  case 36:
    return "CAP_BLOCK_SUSPEND";
  case 37:
    return "CAP_AUDIT_READ";
  case 38:
    return "CAP_PERFMON";
  case 39:
    return "CAP_BPF";
  case 40:
    return "CAP_CHECKPOINT_RESTORE";
  default:
    return "CAP_UNKNOWN";
  }
}

} // namespace system

} // namespace seeker