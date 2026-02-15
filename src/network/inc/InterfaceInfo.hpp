#ifndef SEEKER_NETWORK_INTERFACE_INFO_HPP
#define SEEKER_NETWORK_INTERFACE_INFO_HPP
/**
 * @file InterfaceInfo.hpp
 * @brief Network interface link status, capabilities, and driver information.
 * @note Linux-only. Reads /sys/class/net/ for interface properties.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides NIC identification without dynamic allocation for RT-safe queries
 * of individual interfaces. Enumeration of all interfaces is not RT-safe.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace network {

/* ----------------------------- Constants ----------------------------- */

/// Maximum number of interfaces to track.
inline constexpr std::size_t MAX_INTERFACES = 32;

/// Interface name size (matches IFNAMSIZ).
inline constexpr std::size_t IF_NAME_SIZE = 16;

/// Generic string field size for state/duplex/driver.
inline constexpr std::size_t IF_STRING_SIZE = 32;

/// MAC address string size ("xx:xx:xx:xx:xx:xx" + null).
inline constexpr std::size_t MAC_STRING_SIZE = 18;

/* ----------------------------- InterfaceInfo ----------------------------- */

/**
 * @brief Network interface snapshot.
 *
 * Contains link state, speed, driver, and queue configuration for a NIC.
 * All string fields use fixed-size arrays to avoid heap allocation.
 */
struct InterfaceInfo {
  std::array<char, IF_NAME_SIZE> ifname{};        ///< Interface name (e.g., "eth0")
  std::array<char, IF_STRING_SIZE> operState{};   ///< Operational state (up/down/unknown)
  std::array<char, IF_STRING_SIZE> duplex{};      ///< Duplex mode (full/half/unknown)
  std::array<char, IF_STRING_SIZE> driver{};      ///< Kernel driver name
  std::array<char, MAC_STRING_SIZE> macAddress{}; ///< MAC address string

  int speedMbps{0}; ///< Link speed in Mbps (0 if unknown/down)
  int mtu{0};       ///< Maximum transmission unit (bytes)
  int rxQueues{0};  ///< Number of receive queues
  int txQueues{0};  ///< Number of transmit queues
  int numaNode{-1}; ///< NUMA node affinity (-1 if unknown)

  /// @brief Check if interface is operationally up.
  [[nodiscard]] bool isUp() const noexcept;

  /// @brief Check if this is a physical NIC (not loopback, veth, bridge, etc.).
  [[nodiscard]] bool isPhysical() const noexcept;

  /// @brief Check if interface has valid link (up and speed > 0).
  [[nodiscard]] bool hasLink() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- InterfaceList ----------------------------- */

/**
 * @brief Collection of network interfaces.
 */
struct InterfaceList {
  InterfaceInfo interfaces[MAX_INTERFACES]{};
  std::size_t count{0};

  /// @brief Find interface by name.
  /// @param ifname Interface name to search for.
  /// @return Pointer to interface, or nullptr if not found.
  [[nodiscard]] const InterfaceInfo* find(const char* ifname) const noexcept;

  /// @brief Check if list is empty.
  [[nodiscard]] bool empty() const noexcept;

  /// @brief Human-readable summary of all interfaces.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Check if an interface name refers to a virtual device.
 * @param ifname Interface name to check.
 * @return true if virtual (loopback, veth, bridge, tap, tun, etc.).
 * @note RT-safe: Bounded file reads, no allocation.
 *
 * Checks:
 *  - Known virtual prefixes (veth, docker, br-, virbr, vnet, tap, tun, dummy)
 *  - Absence of /sys/class/net/\<if\>/device symlink
 *  - Fallback: no speed/duplex for physical indicators
 */
[[nodiscard]] bool isVirtualInterface(const char* ifname) noexcept;

/**
 * @brief Query information for a single network interface.
 * @param ifname Interface name (e.g., "eth0", "enp0s3").
 * @return Populated InterfaceInfo, or default-initialized if not found.
 * @note RT-safe: Bounded file reads, no allocation.
 *
 * Sources:
 *  - /sys/class/net/\<if\>/operstate
 *  - /sys/class/net/\<if\>/speed
 *  - /sys/class/net/\<if\>/duplex
 *  - /sys/class/net/\<if\>/mtu
 *  - /sys/class/net/\<if\>/address
 *  - /sys/class/net/\<if\>/device/driver/module
 *  - /sys/class/net/\<if\>/device/numa_node
 *  - /sys/class/net/\<if\>/queues/
 */
[[nodiscard]] InterfaceInfo getInterfaceInfo(const char* ifname) noexcept;

/**
 * @brief Query information for all network interfaces.
 * @return List of all interfaces found in /sys/class/net/.
 * @note NOT RT-safe: Directory enumeration with unbounded iteration.
 */
[[nodiscard]] InterfaceList getAllInterfaces() noexcept;

/**
 * @brief Query information for physical network interfaces only.
 * @return List of physical NICs (excludes loopback, veth, bridges, etc.).
 * @note NOT RT-safe: Directory enumeration with unbounded iteration.
 */
[[nodiscard]] InterfaceList getPhysicalInterfaces() noexcept;

/**
 * @brief Format speed in human-readable form.
 * @param speedMbps Speed in megabits per second.
 * @return Formatted string (e.g., "1 Gbps", "100 Mbps").
 * @note NOT RT-safe: Allocates for string building.
 */
[[nodiscard]] std::string formatSpeed(int speedMbps);

} // namespace network

} // namespace seeker

#endif // SEEKER_NETWORK_INTERFACE_INFO_HPP