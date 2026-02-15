/**
 * @file VirtualizationInfo.cpp
 * @brief Implementation of virtualization detection from multiple sources.
 */

#include "src/system/inc/VirtualizationInfo.hpp"
#include "src/helpers/inc/Files.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace system {

namespace {

using seeker::helpers::files::pathExists;
using seeker::helpers::files::readFileToBuffer;

/// Copy string safely with null termination.
template <std::size_t N> void safeCopy(std::array<char, N>& dest, const char* src) noexcept {
  if (src == nullptr) {
    dest[0] = '\0';
    return;
  }
  std::size_t i = 0;
  while (i < N - 1 && src[i] != '\0') {
    dest[i] = src[i];
    ++i;
  }
  dest[i] = '\0';
}

/// Case-insensitive substring search.
bool containsIgnoreCase(const char* haystack, const char* needle) noexcept {
  if (haystack == nullptr || needle == nullptr) {
    return false;
  }

  const std::size_t NEEDLE_LEN = std::strlen(needle);
  const std::size_t HAYSTACK_LEN = std::strlen(haystack);

  if (NEEDLE_LEN > HAYSTACK_LEN) {
    return false;
  }

  for (std::size_t i = 0; i <= HAYSTACK_LEN - NEEDLE_LEN; ++i) {
    bool match = true;
    for (std::size_t j = 0; j < NEEDLE_LEN; ++j) {
      const char HC = (haystack[i + j] >= 'A' && haystack[i + j] <= 'Z')
                          ? static_cast<char>(haystack[i + j] + 32)
                          : haystack[i + j];
      const char NC =
          (needle[j] >= 'A' && needle[j] <= 'Z') ? static_cast<char>(needle[j] + 32) : needle[j];
      if (HC != NC) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

/// Check if string contains exact substring.
bool contains(const char* haystack, const char* needle) noexcept {
  return std::strstr(haystack, needle) != nullptr;
}

/* ----------------------------- CPUID Helper ----------------------------- */

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)

/// Execute CPUID instruction.
void cpuid(std::uint32_t leaf, std::uint32_t subleaf, std::uint32_t& eax, std::uint32_t& ebx,
           std::uint32_t& ecx, std::uint32_t& edx) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  __asm__ __volatile__("cpuid"
                       : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                       : "a"(leaf), "c"(subleaf));
#else
  eax = ebx = ecx = edx = 0;
#endif
}

/// Check CPUID for hypervisor presence.
bool cpuidHasHypervisor() noexcept {
  std::uint32_t eax = 0;
  std::uint32_t ebx = 0;
  std::uint32_t ecx = 0;
  std::uint32_t edx = 0;
  cpuid(1, 0, eax, ebx, ecx, edx);
  // Bit 31 of ECX indicates hypervisor presence
  return (ecx & (1U << 31)) != 0;
}

/// Get hypervisor vendor string from CPUID leaf 0x40000000.
void getHypervisorVendor(char* buf, std::size_t bufSize) noexcept {
  if (bufSize < 13) {
    if (bufSize > 0)
      buf[0] = '\0';
    return;
  }

  std::uint32_t eax = 0;
  std::uint32_t ebx = 0;
  std::uint32_t ecx = 0;
  std::uint32_t edx = 0;
  cpuid(0x40000000, 0, eax, ebx, ecx, edx);

  // Vendor string is in EBX, ECX, EDX (12 chars)
  std::memcpy(buf, &ebx, 4);
  std::memcpy(buf + 4, &ecx, 4);
  std::memcpy(buf + 8, &edx, 4);
  buf[12] = '\0';
}

#else

bool cpuidHasHypervisor() noexcept { return false; }
void getHypervisorVendor(char* buf, std::size_t bufSize) noexcept {
  if (bufSize > 0)
    buf[0] = '\0';
}

#endif

/* ----------------------------- Detection Helpers ----------------------------- */

/// Detect hypervisor from vendor string.
Hypervisor detectHypervisorFromVendor(const char* vendor) noexcept {
  if (vendor == nullptr || vendor[0] == '\0') {
    return Hypervisor::NONE;
  }

  if (contains(vendor, "KVMKVMKVM") || containsIgnoreCase(vendor, "KVM")) {
    return Hypervisor::KVM;
  }
  if (contains(vendor, "VMwareVMware") || containsIgnoreCase(vendor, "VMware")) {
    return Hypervisor::VMWARE;
  }
  if (contains(vendor, "VBoxVBoxVBox") || containsIgnoreCase(vendor, "VirtualBox") ||
      containsIgnoreCase(vendor, "VBox")) {
    return Hypervisor::VIRTUALBOX;
  }
  if (contains(vendor, "Microsoft Hv") || containsIgnoreCase(vendor, "Hyper-V")) {
    return Hypervisor::HYPERV;
  }
  if (containsIgnoreCase(vendor, "XenVMMXenVMM") || containsIgnoreCase(vendor, "Xen")) {
    return Hypervisor::XEN;
  }
  if (containsIgnoreCase(vendor, "prl hyperv") || containsIgnoreCase(vendor, "Parallels")) {
    return Hypervisor::PARALLELS;
  }
  if (containsIgnoreCase(vendor, "bhyve")) {
    return Hypervisor::BHYVE;
  }
  if (containsIgnoreCase(vendor, "ACRN")) {
    return Hypervisor::ACRN;
  }
  if (containsIgnoreCase(vendor, "QNXQVMBSQG")) {
    return Hypervisor::QNX;
  }

  return Hypervisor::OTHER;
}

/// Detect hypervisor from DMI product/manufacturer strings.
Hypervisor detectHypervisorFromDMI(const char* product, const char* manufacturer,
                                   const char* bios) noexcept {
  // Check product name
  if (product != nullptr && product[0] != '\0') {
    if (containsIgnoreCase(product, "VMware")) {
      return Hypervisor::VMWARE;
    }
    if (containsIgnoreCase(product, "VirtualBox")) {
      return Hypervisor::VIRTUALBOX;
    }
    if (containsIgnoreCase(product, "KVM") || containsIgnoreCase(product, "QEMU")) {
      return Hypervisor::KVM;
    }
    if (containsIgnoreCase(product, "Virtual Machine") &&
        containsIgnoreCase(manufacturer, "Microsoft")) {
      return Hypervisor::HYPERV;
    }
    if (containsIgnoreCase(product, "HVM domU") || containsIgnoreCase(product, "Xen")) {
      return Hypervisor::XEN;
    }
    if (containsIgnoreCase(product, "Parallels")) {
      return Hypervisor::PARALLELS;
    }
    if (containsIgnoreCase(product, "Google Compute")) {
      return Hypervisor::GOOGLE_COMPUTE;
    }
  }

  // Check manufacturer
  if (manufacturer != nullptr && manufacturer[0] != '\0') {
    if (containsIgnoreCase(manufacturer, "Amazon EC2") ||
        containsIgnoreCase(manufacturer, "Amazon")) {
      return Hypervisor::AWS_NITRO;
    }
    if (containsIgnoreCase(manufacturer, "Google")) {
      return Hypervisor::GOOGLE_COMPUTE;
    }
    if (containsIgnoreCase(manufacturer, "Microsoft Corporation") && product != nullptr &&
        containsIgnoreCase(product, "Virtual")) {
      return Hypervisor::AZURE;
    }
    if (containsIgnoreCase(manufacturer, "QEMU")) {
      return Hypervisor::KVM;
    }
    if (containsIgnoreCase(manufacturer, "Xen")) {
      return Hypervisor::XEN;
    }
  }

  // Check BIOS vendor
  if (bios != nullptr && bios[0] != '\0') {
    if (containsIgnoreCase(bios, "SeaBIOS") || containsIgnoreCase(bios, "QEMU")) {
      return Hypervisor::KVM;
    }
    if (containsIgnoreCase(bios, "VMware")) {
      return Hypervisor::VMWARE;
    }
    if (containsIgnoreCase(bios, "VirtualBox") || containsIgnoreCase(bios, "innotek")) {
      return Hypervisor::VIRTUALBOX;
    }
    if (containsIgnoreCase(bios, "Hyper-V") || containsIgnoreCase(bios, "Microsoft")) {
      return Hypervisor::HYPERV;
    }
    if (containsIgnoreCase(bios, "Xen")) {
      return Hypervisor::XEN;
    }
  }

  return Hypervisor::NONE;
}

/// Detect container runtime from various indicators.
ContainerRuntime detectContainerRuntime() noexcept {
  // Check /.dockerenv
  if (pathExists("/.dockerenv")) {
    return ContainerRuntime::DOCKER;
  }

  // Check /run/.containerenv (Podman)
  if (pathExists("/run/.containerenv")) {
    return ContainerRuntime::PODMAN;
  }

  // Check cgroup for container indicators
  std::array<char, 4096> cgroupBuf{};
  if (readFileToBuffer("/proc/1/cgroup", cgroupBuf.data(), cgroupBuf.size()) > 0) {
    if (contains(cgroupBuf.data(), "docker")) {
      return ContainerRuntime::DOCKER;
    }
    if (contains(cgroupBuf.data(), "libpod") || contains(cgroupBuf.data(), "podman")) {
      return ContainerRuntime::PODMAN;
    }
    if (contains(cgroupBuf.data(), "lxc")) {
      return ContainerRuntime::LXC;
    }
    if (contains(cgroupBuf.data(), "machine.slice") &&
        contains(cgroupBuf.data(), "systemd-nspawn")) {
      return ContainerRuntime::SYSTEMD_NSPAWN;
    }
  }

  // Check for LXC container
  if (pathExists("/dev/lxc")) {
    return ContainerRuntime::LXC;
  }

  // Check for OpenVZ
  if (pathExists("/proc/vz") && !pathExists("/proc/bc")) {
    return ContainerRuntime::OPENVZ;
  }

  // Check for WSL
  std::array<char, 512> versionBuf{};
  if (readFileToBuffer("/proc/version", versionBuf.data(), versionBuf.size()) > 0) {
    if (containsIgnoreCase(versionBuf.data(), "microsoft") ||
        containsIgnoreCase(versionBuf.data(), "WSL")) {
      return ContainerRuntime::WSL;
    }
  }

  // Check container environment variable hint
  std::array<char, 256> envBuf{};
  if (readFileToBuffer("/proc/1/environ", envBuf.data(), envBuf.size()) > 0) {
    if (contains(envBuf.data(), "container=docker")) {
      return ContainerRuntime::DOCKER;
    }
    if (contains(envBuf.data(), "container=podman")) {
      return ContainerRuntime::PODMAN;
    }
    if (contains(envBuf.data(), "container=lxc")) {
      return ContainerRuntime::LXC;
    }
    if (contains(envBuf.data(), "container=systemd-nspawn")) {
      return ContainerRuntime::SYSTEMD_NSPAWN;
    }
  }

  return ContainerRuntime::NONE;
}

/// Check if product/manufacturer indicates cloud environment.
bool isCloudEnvironment(const char* product, const char* manufacturer) noexcept {
  if (product != nullptr) {
    if (containsIgnoreCase(product, "Amazon") || containsIgnoreCase(product, "EC2") ||
        containsIgnoreCase(product, "Google") || containsIgnoreCase(product, "Azure") ||
        containsIgnoreCase(product, "Compute Engine") ||
        containsIgnoreCase(product, "DigitalOcean") || containsIgnoreCase(product, "Linode") ||
        containsIgnoreCase(product, "Vultr")) {
      return true;
    }
  }
  if (manufacturer != nullptr) {
    if (containsIgnoreCase(manufacturer, "Amazon") || containsIgnoreCase(manufacturer, "Google") ||
        containsIgnoreCase(manufacturer, "Microsoft Corporation")) {
      return true;
    }
  }
  return false;
}

/// Calculate RT suitability score based on virtualization type.
int calculateRtSuitability(VirtType type, Hypervisor hv, bool nested) noexcept {
  if (type == VirtType::NONE) {
    return 100; // Bare metal is optimal
  }

  if (nested) {
    return 10; // Nested virtualization is very poor for RT
  }

  if (type == VirtType::CONTAINER) {
    return 80; // Containers share kernel, reasonable for RT
  }

  // VM suitability depends on hypervisor
  switch (hv) {
  case Hypervisor::KVM:
    return 60; // KVM with proper tuning can be decent
  case Hypervisor::VMWARE:
    return 50; // VMware has good but not perfect RT support
  case Hypervisor::XEN:
    return 55; // Xen has RT-Xen variant
  case Hypervisor::HYPERV:
    return 40; // Hyper-V is less RT-friendly
  case Hypervisor::VIRTUALBOX:
    return 30; // VirtualBox is not designed for RT
  default:
    return 35; // Unknown hypervisor, assume moderate
  }
}

} // namespace

/* ----------------------------- Enum toString ----------------------------- */

const char* toString(VirtType type) noexcept {
  switch (type) {
  case VirtType::NONE:
    return "bare_metal";
  case VirtType::VM:
    return "vm";
  case VirtType::CONTAINER:
    return "container";
  case VirtType::UNKNOWN:
    return "unknown";
  }
  return "unknown";
}

const char* toString(Hypervisor hv) noexcept {
  switch (hv) {
  case Hypervisor::NONE:
    return "none";
  case Hypervisor::KVM:
    return "kvm";
  case Hypervisor::VMWARE:
    return "vmware";
  case Hypervisor::VIRTUALBOX:
    return "virtualbox";
  case Hypervisor::HYPERV:
    return "hyper-v";
  case Hypervisor::XEN:
    return "xen";
  case Hypervisor::PARALLELS:
    return "parallels";
  case Hypervisor::BHYVE:
    return "bhyve";
  case Hypervisor::QNX:
    return "qnx";
  case Hypervisor::ACRN:
    return "acrn";
  case Hypervisor::POWERVM:
    return "powervm";
  case Hypervisor::ZVM:
    return "zvm";
  case Hypervisor::AWS_NITRO:
    return "aws_nitro";
  case Hypervisor::GOOGLE_COMPUTE:
    return "google_compute";
  case Hypervisor::AZURE:
    return "azure";
  case Hypervisor::OTHER:
    return "other";
  }
  return "unknown";
}

const char* toString(ContainerRuntime rt) noexcept {
  switch (rt) {
  case ContainerRuntime::NONE:
    return "none";
  case ContainerRuntime::DOCKER:
    return "docker";
  case ContainerRuntime::PODMAN:
    return "podman";
  case ContainerRuntime::LXC:
    return "lxc";
  case ContainerRuntime::SYSTEMD_NSPAWN:
    return "systemd-nspawn";
  case ContainerRuntime::RKT:
    return "rkt";
  case ContainerRuntime::OPENVZ:
    return "openvz";
  case ContainerRuntime::WSL:
    return "wsl";
  case ContainerRuntime::OTHER:
    return "other";
  }
  return "unknown";
}

/* ----------------------------- VirtualizationInfo Methods ----------------------------- */

bool VirtualizationInfo::isBareMetal() const noexcept { return type == VirtType::NONE; }

bool VirtualizationInfo::isVirtualMachine() const noexcept { return type == VirtType::VM; }

bool VirtualizationInfo::isContainer() const noexcept { return type == VirtType::CONTAINER; }

bool VirtualizationInfo::isVirtualized() const noexcept { return type != VirtType::NONE; }

bool VirtualizationInfo::isCloud() const noexcept {
  return hypervisor == Hypervisor::AWS_NITRO || hypervisor == Hypervisor::GOOGLE_COMPUTE ||
         hypervisor == Hypervisor::AZURE ||
         isCloudEnvironment(productName.data(), manufacturer.data());
}

bool VirtualizationInfo::isRtSuitable() const noexcept { return rtSuitability >= 70; }

const char* VirtualizationInfo::description() const noexcept {
  switch (type) {
  case VirtType::NONE:
    return "Bare metal";
  case VirtType::VM:
    if (hypervisorName[0] != '\0') {
      return hypervisorName.data();
    }
    return seeker::system::toString(hypervisor);
  case VirtType::CONTAINER:
    if (containerName[0] != '\0') {
      return containerName.data();
    }
    return seeker::system::toString(containerRuntime);
  case VirtType::UNKNOWN:
    return "Unknown virtualization";
  }
  return "Unknown";
}

std::string VirtualizationInfo::toString() const {
  std::string out;
  out.reserve(512);

  out += "Virtualization Info:\n";
  out += fmt::format("  Type:           {}\n", seeker::system::toString(type));
  out += fmt::format("  Description:    {}\n", description());

  if (type == VirtType::VM) {
    out += fmt::format("  Hypervisor:     {}\n", seeker::system::toString(hypervisor));
    if (hypervisorName[0] != '\0') {
      out += fmt::format("  HV Vendor:      {}\n", hypervisorName.data());
    }
  }

  if (type == VirtType::CONTAINER) {
    out += fmt::format("  Container:      {}\n", seeker::system::toString(containerRuntime));
    if (containerName[0] != '\0') {
      out += fmt::format("  Runtime:        {}\n", containerName.data());
    }
  }

  if (productName[0] != '\0') {
    out += fmt::format("  Product:        {}\n", productName.data());
  }
  if (manufacturer[0] != '\0') {
    out += fmt::format("  Manufacturer:   {}\n", manufacturer.data());
  }

  out += fmt::format("  CPUID HV bit:   {}\n", cpuidHypervisor ? "set" : "not set");
  out += fmt::format("  DMI virtual:    {}\n", dmiVirtual ? "yes" : "no");
  out += fmt::format("  Nested:         {}\n", nested ? "yes" : "no");
  out += fmt::format("  Paravirt:       {}\n", paravirt ? "yes" : "no");
  out += fmt::format("  Cloud:          {}\n", isCloud() ? "yes" : "no");
  out += fmt::format("  Confidence:     {}%\n", confidence);
  out += fmt::format("  RT Suitability: {}%\n", rtSuitability);

  return out;
}

/* ----------------------------- API ----------------------------- */

VirtualizationInfo getVirtualizationInfo() noexcept {
  VirtualizationInfo info{};

  std::array<char, 256> buf{};
  int confidence = 0;

  // 1. Check CPUID hypervisor bit (x86 only)
  info.cpuidHypervisor = cpuidHasHypervisor();
  if (info.cpuidHypervisor) {
    confidence += 40;

    // Get hypervisor vendor string
    std::array<char, 16> hvVendor{};
    getHypervisorVendor(hvVendor.data(), hvVendor.size());
    if (hvVendor[0] != '\0') {
      safeCopy(info.hypervisorName, hvVendor.data());
      info.hypervisor = detectHypervisorFromVendor(hvVendor.data());
      info.type = VirtType::VM;
      confidence += 20;
    }
  }

  // 2. Read DMI information
  if (readFileToBuffer("/sys/class/dmi/id/product_name", buf.data(), buf.size()) > 0) {
    safeCopy(info.productName, buf.data());
  }
  if (readFileToBuffer("/sys/class/dmi/id/sys_vendor", buf.data(), buf.size()) > 0) {
    safeCopy(info.manufacturer, buf.data());
  }
  if (readFileToBuffer("/sys/class/dmi/id/bios_vendor", buf.data(), buf.size()) > 0) {
    safeCopy(info.biosVendor, buf.data());
  }

  // Check DMI for virtual indicators
  const Hypervisor DMI_HV = detectHypervisorFromDMI(
      info.productName.data(), info.manufacturer.data(), info.biosVendor.data());
  if (DMI_HV != Hypervisor::NONE) {
    info.dmiVirtual = true;
    if (info.hypervisor == Hypervisor::NONE || info.hypervisor == Hypervisor::OTHER) {
      info.hypervisor = DMI_HV;
    }
    info.type = VirtType::VM;
    confidence += 30;
  }

  // 3. Check for container environment
  const ContainerRuntime CONTAINER_RT = detectContainerRuntime();
  if (CONTAINER_RT != ContainerRuntime::NONE) {
    info.containerIndicators = true;
    info.containerRuntime = CONTAINER_RT;
    safeCopy(info.containerName, seeker::system::toString(CONTAINER_RT));

    // Container takes precedence over VM detection since we might be
    // a container running inside a VM
    info.type = VirtType::CONTAINER;
    confidence += 30;
  }

  // 4. Check for paravirtualization
  if (pathExists("/sys/hypervisor/type")) {
    info.paravirt = true;
    if (readFileToBuffer("/sys/hypervisor/type", buf.data(), buf.size()) > 0) {
      if (contains(buf.data(), "xen")) {
        info.hypervisor = Hypervisor::XEN;
      }
    }
  }

  // 5. Check for nested virtualization
  if (pathExists("/sys/module/kvm_intel/parameters/nested")) {
    if (readFileToBuffer("/sys/module/kvm_intel/parameters/nested", buf.data(), buf.size()) > 0) {
      if (buf[0] == 'Y' || buf[0] == '1') {
        info.nested = true;
      }
    }
  }
  if (pathExists("/sys/module/kvm_amd/parameters/nested")) {
    if (readFileToBuffer("/sys/module/kvm_amd/parameters/nested", buf.data(), buf.size()) > 0) {
      if (buf[0] == '1') {
        info.nested = true;
      }
    }
  }

  // Finalize type if still unknown
  if (info.type == VirtType::NONE && (info.cpuidHypervisor || info.dmiVirtual)) {
    info.type = VirtType::VM;
  }

  // If we detected VM indicators but no specific hypervisor
  if (info.type == VirtType::VM && info.hypervisor == Hypervisor::NONE && confidence > 20) {
    info.hypervisor = Hypervisor::OTHER;
  }

  // If no virtualization detected but confidence is low, mark as unknown
  if (info.type == VirtType::NONE && confidence == 0) {
    // Check if we could even read DMI data (might indicate issue)
    if (info.productName[0] == '\0' && info.manufacturer[0] == '\0') {
      // Cannot read system info, could be virtualization blocking it
      // but more likely just permissions - leave as bare metal
    }
  }

  // Calculate confidence and RT suitability
  info.confidence = (confidence > 100) ? 100 : confidence;
  if (info.type == VirtType::NONE) {
    info.confidence = 90; // High confidence in bare metal if no indicators found
  }

  info.rtSuitability = calculateRtSuitability(info.type, info.hypervisor, info.nested);

  return info;
}

bool isVirtualized() noexcept {
  // Quick check without full detection
  if (cpuidHasHypervisor()) {
    return true;
  }

  // Check common container indicators
  if (pathExists("/.dockerenv") || pathExists("/run/.containerenv")) {
    return true;
  }

  // Check cgroup for container hints
  std::array<char, 512> buf{};
  if (readFileToBuffer("/proc/1/cgroup", buf.data(), buf.size()) > 0) {
    if (contains(buf.data(), "docker") || contains(buf.data(), "lxc") ||
        contains(buf.data(), "podman")) {
      return true;
    }
  }

  return false;
}

bool isContainerized() noexcept { return detectContainerRuntime() != ContainerRuntime::NONE; }

} // namespace system

} // namespace seeker