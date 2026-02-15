/**
 * @file SecurityStatus.cpp
 * @brief Implementation of Linux Security Module status detection.
 */

#include "src/system/inc/SecurityStatus.hpp"
#include "src/helpers/inc/Files.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <dirent.h> // opendir, readdir, closedir

#include <cstring> // strlen, strcmp, strncpy

#include <fmt/core.h>

namespace seeker {

namespace system {

namespace {

using seeker::helpers::files::isDirectory;
using seeker::helpers::files::pathExists;
using seeker::helpers::files::readFileInt;
using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::files::readFileUint64;
using seeker::helpers::strings::copyToFixedArray;

/* ----------------------------- Constants ----------------------------- */

constexpr const char* SELINUX_MNT = "/sys/fs/selinux";
constexpr const char* SELINUX_ENFORCE = "/sys/fs/selinux/enforce";
constexpr const char* SELINUX_POLICY = "/sys/fs/selinux/policyvers";
constexpr const char* SELINUX_MCS = "/sys/fs/selinux/mcs";

constexpr const char* APPARMOR_MNT = "/sys/kernel/security/apparmor";
constexpr const char* APPARMOR_PROFILES = "/sys/kernel/security/apparmor/profiles";

constexpr const char* LSM_LIST = "/sys/kernel/security/lsm";

constexpr const char* SECCOMP_ACTIONS = "/proc/sys/kernel/seccomp/actions_avail";
constexpr const char* LANDLOCK_ABI = "/sys/kernel/security/landlock/abi_version";
constexpr const char* YAMA_PTRACE = "/proc/sys/kernel/yama/ptrace_scope";

constexpr const char* PROC_SELF_ATTR_CURRENT = "/proc/self/attr/current";

/* ----------------------------- File Helpers ----------------------------- */

/// Read SELinux current context
void readSelinuxContext(std::array<char, SECURITY_CONTEXT_SIZE>& out) noexcept {
  out[0] = '\0';

  std::array<char, SECURITY_CONTEXT_SIZE> buf{};
  const std::size_t LEN = readFileToBuffer(PROC_SELF_ATTR_CURRENT, buf.data(), buf.size());
  if (LEN > 0) {
    copyToFixedArray(out, buf.data());
  }
}

/// Count AppArmor profiles by mode
void countApparmorProfiles(std::uint32_t& enforce, std::uint32_t& complain) noexcept {
  enforce = 0;
  complain = 0;

  std::array<char, 4096> buf{};
  const std::size_t LEN = readFileToBuffer(APPARMOR_PROFILES, buf.data(), buf.size());
  if (LEN == 0) {
    return;
  }

  // Format: "profile_name (mode)"
  // Modes: enforce, complain
  const char* ptr = buf.data();
  while (*ptr != '\0') {
    // Find mode in parentheses
    const char* OPEN = std::strchr(ptr, '(');
    const char* CLOSE = OPEN != nullptr ? std::strchr(OPEN, ')') : nullptr;

    if (OPEN != nullptr && CLOSE != nullptr && CLOSE > OPEN) {
      const std::size_t MODE_LEN = static_cast<std::size_t>(CLOSE - OPEN - 1);
      if (MODE_LEN == 7 && std::strncmp(OPEN + 1, "enforce", 7) == 0) {
        ++enforce;
      } else if (MODE_LEN == 8 && std::strncmp(OPEN + 1, "complain", 8) == 0) {
        ++complain;
      }
    }

    // Move to next line
    const char* NL = std::strchr(ptr, '\n');
    if (NL == nullptr) {
      break;
    }
    ptr = NL + 1;
  }
}

/// Parse LSM list from /sys/kernel/security/lsm
void parseLsmList(SecurityStatus& status) noexcept {
  status.lsmCount = 0;

  std::array<char, 256> buf{};
  const std::size_t LEN = readFileToBuffer(LSM_LIST, buf.data(), buf.size());
  if (LEN == 0) {
    return;
  }

  // Format: "lockdown,capability,yama,apparmor,bpf,landlock"
  const char* ptr = buf.data();
  while (*ptr != '\0' && status.lsmCount < MAX_LSMS) {
    // Find end of current LSM name
    const char* comma = std::strchr(ptr, ',');
    const std::size_t NAME_LEN =
        (comma != nullptr) ? static_cast<std::size_t>(comma - ptr) : std::strlen(ptr);

    if (NAME_LEN > 0 && NAME_LEN < LSM_NAME_SIZE) {
      LsmInfo& lsm = status.lsms[status.lsmCount];

      // Copy name
      for (std::size_t i = 0; i < NAME_LEN && i < LSM_NAME_SIZE - 1; ++i) {
        lsm.name[i] = ptr[i];
      }
      lsm.name[NAME_LEN < LSM_NAME_SIZE ? NAME_LEN : LSM_NAME_SIZE - 1] = '\0';
      lsm.active = true;

      ++status.lsmCount;
    }

    if (comma == nullptr) {
      break;
    }
    ptr = comma + 1;
  }
}

} // namespace

/* ----------------------------- SelinuxMode ----------------------------- */

const char* toString(SelinuxMode mode) noexcept {
  switch (mode) {
  case SelinuxMode::NOT_PRESENT:
    return "not present";
  case SelinuxMode::DISABLED:
    return "disabled";
  case SelinuxMode::PERMISSIVE:
    return "permissive";
  case SelinuxMode::ENFORCING:
    return "enforcing";
  }
  return "unknown";
}

/* ----------------------------- ApparmorMode ----------------------------- */

const char* toString(ApparmorMode mode) noexcept {
  switch (mode) {
  case ApparmorMode::NOT_PRESENT:
    return "not present";
  case ApparmorMode::DISABLED:
    return "disabled";
  case ApparmorMode::ENABLED:
    return "enabled";
  }
  return "unknown";
}

/* ----------------------------- SelinuxStatus Methods ----------------------------- */

bool SelinuxStatus::isActive() const noexcept {
  return mode == SelinuxMode::PERMISSIVE || mode == SelinuxMode::ENFORCING;
}

bool SelinuxStatus::isEnforcing() const noexcept { return mode == SelinuxMode::ENFORCING; }

std::string SelinuxStatus::toString() const {
  if (mode == SelinuxMode::NOT_PRESENT) {
    return "SELinux: not present";
  }

  std::string out;
  out += fmt::format("SELinux: {}", seeker::system::toString(mode));

  if (isActive()) {
    if (policyType[0] != '\0') {
      out += fmt::format(" (policy: {})", policyType.data());
    }
    if (policyVersion > 0) {
      out += fmt::format(" v{}", policyVersion);
    }
    if (mcsEnabled) {
      out += " MCS";
    }
    if (mlsEnabled) {
      out += " MLS";
    }
  }

  return out;
}

/* ----------------------------- ApparmorStatus Methods ----------------------------- */

bool ApparmorStatus::isActive() const noexcept { return mode == ApparmorMode::ENABLED; }

std::string ApparmorStatus::toString() const {
  if (mode == ApparmorMode::NOT_PRESENT) {
    return "AppArmor: not present";
  }

  std::string out;
  out += fmt::format("AppArmor: {}", seeker::system::toString(mode));

  if (isActive()) {
    out += fmt::format(" ({} profiles: {} enforce, {} complain)", profilesLoaded, profilesEnforce,
                       profilesComplain);
  }

  return out;
}

/* ----------------------------- LsmInfo Methods ----------------------------- */

std::string LsmInfo::toString() const {
  if (name[0] == '\0') {
    return "(empty)";
  }
  return fmt::format("{}: {}", name.data(), active ? "active" : "inactive");
}

/* ----------------------------- SecurityStatus Methods ----------------------------- */

bool SecurityStatus::hasEnforcement() const noexcept {
  if (selinux.isEnforcing()) {
    return true;
  }
  if (apparmor.isActive() && apparmor.profilesEnforce > 0) {
    return true;
  }
  return false;
}

std::string SecurityStatus::activeLsmList() const {
  if (lsmCount == 0) {
    return "none";
  }

  std::string out;
  for (std::size_t i = 0; i < lsmCount; ++i) {
    if (i > 0) {
      out += ", ";
    }
    out += lsms[i].name.data();
  }
  return out;
}

std::string SecurityStatus::toString() const {
  std::string out;

  out += fmt::format("Active LSMs: {}\n", activeLsmList());
  out += selinux.toString() + "\n";
  out += apparmor.toString() + "\n";

  out += fmt::format("Seccomp: {}\n", seccompAvailable ? "available" : "not available");
  out += fmt::format("Landlock: {}\n", landLockAvailable ? "available" : "not available");
  out += fmt::format("Yama ptrace: {}\n", yamaPtrace ? "restricted" : "unrestricted");

  return out;
}

/* ----------------------------- API ----------------------------- */

bool selinuxAvailable() noexcept { return isDirectory(SELINUX_MNT); }

bool apparmorAvailable() noexcept { return isDirectory(APPARMOR_MNT); }

SelinuxStatus getSelinuxStatus() noexcept {
  SelinuxStatus status{};

  if (!selinuxAvailable()) {
    status.mode = SelinuxMode::NOT_PRESENT;
    return status;
  }

  // Check enforce status
  const std::int32_t ENFORCE = readFileInt(SELINUX_ENFORCE, -1);
  if (ENFORCE < 0) {
    status.mode = SelinuxMode::DISABLED;
    return status;
  }

  status.mode = (ENFORCE != 0) ? SelinuxMode::ENFORCING : SelinuxMode::PERMISSIVE;

  // Read policy version
  status.policyVersion = static_cast<std::uint32_t>(readFileUint64(SELINUX_POLICY, 0));

  // Check MCS/MLS
  status.mcsEnabled = pathExists(SELINUX_MCS);

  // Read current context
  readSelinuxContext(status.currentContext);

  // Try to determine policy type from context
  // Context format: user:role:type:level
  // Targeted policy typically has "unconfined_t" or similar
  if (status.currentContext[0] != '\0') {
    copyToFixedArray(status.policyType, "targeted"); // Default assumption
  }

  return status;
}

ApparmorStatus getApparmorStatus() noexcept {
  ApparmorStatus status{};

  if (!apparmorAvailable()) {
    status.mode = ApparmorMode::NOT_PRESENT;
    return status;
  }

  // Check if profiles exist
  if (!pathExists(APPARMOR_PROFILES)) {
    status.mode = ApparmorMode::DISABLED;
    return status;
  }

  status.mode = ApparmorMode::ENABLED;

  // Count profiles by mode
  countApparmorProfiles(status.profilesEnforce, status.profilesComplain);
  status.profilesLoaded = status.profilesEnforce + status.profilesComplain;

  return status;
}

SecurityStatus getSecurityStatus() noexcept {
  SecurityStatus status{};

  // Get individual subsystem status
  status.selinux = getSelinuxStatus();
  status.apparmor = getApparmorStatus();

  // Parse LSM list
  parseLsmList(status);

  // Check for other security features
  status.seccompAvailable = pathExists(SECCOMP_ACTIONS);
  status.landLockAvailable = pathExists(LANDLOCK_ABI);

  // Check Yama ptrace scope
  const std::int32_t YAMA = readFileInt(YAMA_PTRACE, 0);
  status.yamaPtrace = (YAMA > 0);

  return status;
}

} // namespace system

} // namespace seeker