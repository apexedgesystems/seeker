#ifndef SEEKER_DEVICE_CAN_BUS_INFO_HPP
#define SEEKER_DEVICE_CAN_BUS_INFO_HPP
/**
 * @file CanBusInfo.hpp
 * @brief SocketCAN interface enumeration and status.
 * @note Linux-only. Uses SocketCAN interfaces and /sys/class/net/.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides CAN bus information for embedded/flight/automotive software:
 *  - Interface enumeration (can0, vcan0, slcan0, etc.)
 *  - Bitrate and timing configuration
 *  - Error counters and bus state
 *  - Controller mode and features
 *  - RT safety considerations for CAN diagnostics
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace device {

/* ----------------------------- Constants ----------------------------- */

/// Maximum CAN interface name length.
inline constexpr std::size_t CAN_NAME_SIZE = 32;

/// Maximum CAN path length.
inline constexpr std::size_t CAN_PATH_SIZE = 128;

/// Maximum driver string length.
inline constexpr std::size_t CAN_DRIVER_SIZE = 64;

/// Maximum number of CAN interfaces to enumerate.
inline constexpr std::size_t MAX_CAN_INTERFACES = 32;

/// Standard CAN maximum bitrate (1 Mbps).
inline constexpr std::uint32_t CAN_MAX_BITRATE_CLASSIC = 1000000;

/// CAN FD maximum bitrate for data phase (8 Mbps typical).
inline constexpr std::uint32_t CAN_MAX_BITRATE_FD = 8000000;

/* ----------------------------- CanInterfaceType ----------------------------- */

/**
 * @brief Type of CAN interface.
 */
enum class CanInterfaceType : std::uint8_t {
  UNKNOWN = 0, ///< Unknown interface type
  PHYSICAL,    ///< Physical CAN controller (can0, can1)
  VIRTUAL,     ///< Virtual CAN for testing (vcan0)
  SLCAN,       ///< Serial-line CAN (slcan0)
  SOCKETCAND,  ///< Network-based CAN (socketcand)
  PEAK,        ///< PEAK-System PCAN devices
  KVASER,      ///< Kvaser devices
  VECTOR,      ///< Vector Informatik devices
};

/// @brief Convert CanInterfaceType to string.
/// @param type Interface type.
/// @return String representation (e.g., "physical").
[[nodiscard]] const char* toString(CanInterfaceType type) noexcept;

/* ----------------------------- CanBusState ----------------------------- */

/**
 * @brief CAN bus state per ISO 11898.
 *
 * Error states follow the standard CAN error management:
 *  - ERROR_ACTIVE: Normal operation (TEC/REC < 128)
 *  - ERROR_WARNING: High error count warning (TEC/REC >= 96)
 *  - ERROR_PASSIVE: Transmit/receive errors high (TEC/REC >= 128)
 *  - BUS_OFF: Controller disconnected (TEC >= 256)
 */
enum class CanBusState : std::uint8_t {
  UNKNOWN = 0,   ///< State unknown or unavailable
  ERROR_ACTIVE,  ///< Normal operation
  ERROR_WARNING, ///< Error warning threshold reached
  ERROR_PASSIVE, ///< Error passive state
  BUS_OFF,       ///< Bus-off state (controller disconnected)
  STOPPED,       ///< Interface administratively stopped
};

/// @brief Convert CanBusState to string.
/// @param state Bus state.
/// @return String representation (e.g., "error-active").
[[nodiscard]] const char* toString(CanBusState state) noexcept;

/* ----------------------------- CanCtrlMode ----------------------------- */

/**
 * @brief CAN controller mode flags.
 *
 * Reflects CAN_CTRLMODE_* flags from linux/can/netlink.h.
 */
struct CanCtrlMode {
  bool loopback{false};       ///< Local loopback mode
  bool listenOnly{false};     ///< Listen-only (no ACK/TX)
  bool tripleSampling{false}; ///< Triple sampling
  bool oneShot{false};        ///< One-shot mode (no retransmit)
  bool berr{false};           ///< Bus error reporting
  bool fd{false};             ///< CAN FD mode enabled
  bool presumeAck{false};     ///< Presume ACK on TX
  bool fdNonIso{false};       ///< Non-ISO CAN FD mode
  bool ccLen8Dlc{false};      ///< Classic CAN DLC = 8 encoding

  /// @brief Check if any special mode is enabled.
  [[nodiscard]] bool hasSpecialModes() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- CanBitTiming ----------------------------- */

/**
 * @brief CAN bit timing parameters.
 *
 * For CAN FD interfaces, this represents arbitration phase timing.
 */
struct CanBitTiming {
  std::uint32_t bitrate{0};     ///< Bitrate in bits/second
  std::uint32_t samplePoint{0}; ///< Sample point in tenths of percent (e.g., 875 = 87.5%)
  std::uint32_t tq{0};          ///< Time quantum in nanoseconds
  std::uint32_t propSeg{0};     ///< Propagation segment
  std::uint32_t phaseSeg1{0};   ///< Phase segment 1
  std::uint32_t phaseSeg2{0};   ///< Phase segment 2
  std::uint32_t sjw{0};         ///< Synchronization jump width
  std::uint32_t brp{0};         ///< Baud rate prescaler

  /// @brief Check if timing is configured (bitrate > 0).
  [[nodiscard]] bool isConfigured() const noexcept;

  /// @brief Get sample point as percentage.
  [[nodiscard]] double samplePointPercent() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- CanErrorCounters ----------------------------- */

/**
 * @brief CAN error counters (TEC/REC).
 */
struct CanErrorCounters {
  std::uint16_t txErrors{0};        ///< Transmit Error Counter (TEC)
  std::uint16_t rxErrors{0};        ///< Receive Error Counter (REC)
  std::uint32_t busErrors{0};       ///< Bus error count (from berr)
  std::uint32_t errorWarning{0};    ///< Error warning transitions
  std::uint32_t errorPassive{0};    ///< Error passive transitions
  std::uint32_t busOff{0};          ///< Bus-off events
  std::uint32_t arbitrationLost{0}; ///< Arbitration lost events
  std::uint32_t restarts{0};        ///< Controller restart count

  /// @brief Check if any errors have occurred.
  [[nodiscard]] bool hasErrors() const noexcept;

  /// @brief Total error events.
  [[nodiscard]] std::uint32_t totalErrors() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- CanInterfaceStats ----------------------------- */

/**
 * @brief CAN interface traffic statistics.
 */
struct CanInterfaceStats {
  std::uint64_t txFrames{0};  ///< Frames transmitted
  std::uint64_t rxFrames{0};  ///< Frames received
  std::uint64_t txBytes{0};   ///< Bytes transmitted
  std::uint64_t rxBytes{0};   ///< Bytes received
  std::uint64_t txDropped{0}; ///< Frames dropped on TX
  std::uint64_t rxDropped{0}; ///< Frames dropped on RX
  std::uint64_t txErrors{0};  ///< Transmit errors
  std::uint64_t rxErrors{0};  ///< Receive errors

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- CanInterfaceInfo ----------------------------- */

/**
 * @brief Complete information for a CAN interface.
 */
struct CanInterfaceInfo {
  std::array<char, CAN_NAME_SIZE> name{};      ///< Interface name (e.g., "can0")
  std::array<char, CAN_PATH_SIZE> sysfsPath{}; ///< Sysfs path
  std::array<char, CAN_DRIVER_SIZE> driver{};  ///< Driver name

  CanInterfaceType type{CanInterfaceType::UNKNOWN}; ///< Interface type
  CanBusState state{CanBusState::UNKNOWN};          ///< Current bus state

  CanBitTiming bitTiming{};     ///< Arbitration phase timing
  CanBitTiming dataBitTiming{}; ///< Data phase timing (CAN FD only)
  CanCtrlMode ctrlMode{};       ///< Controller mode flags
  CanErrorCounters errors{};    ///< Error counters
  CanInterfaceStats stats{};    ///< Traffic statistics

  std::uint32_t clockFreq{0}; ///< Controller clock frequency (Hz)
  std::uint32_t txqLen{0};    ///< Transmit queue length
  std::int32_t ifindex{-1};   ///< Interface index

  bool exists{false};    ///< Interface exists
  bool isUp{false};      ///< Interface is UP
  bool isRunning{false}; ///< Interface is RUNNING

  /// @brief Check if interface is usable for communication.
  [[nodiscard]] bool isUsable() const noexcept;

  /// @brief Check if this is a CAN FD interface.
  [[nodiscard]] bool isFd() const noexcept;

  /// @brief Check if interface has errors.
  [[nodiscard]] bool hasErrors() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- CanInterfaceList ----------------------------- */

/**
 * @brief Collection of CAN interface information.
 */
struct CanInterfaceList {
  CanInterfaceInfo interfaces[MAX_CAN_INTERFACES]{};
  std::size_t count{0};

  /// @brief Find interface by name.
  /// @param name Interface name to search for (e.g., "can0").
  /// @return Pointer to interface info, or nullptr if not found.
  [[nodiscard]] const CanInterfaceInfo* find(const char* name) const noexcept;

  /// @brief Check if list is empty.
  [[nodiscard]] bool empty() const noexcept;

  /// @brief Count interfaces that are UP.
  [[nodiscard]] std::size_t countUp() const noexcept;

  /// @brief Count physical CAN interfaces.
  [[nodiscard]] std::size_t countPhysical() const noexcept;

  /// @brief Count interfaces with errors.
  [[nodiscard]] std::size_t countWithErrors() const noexcept;

  /// @brief Human-readable summary of all interfaces.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Get information for a specific CAN interface.
 * @param name Interface name (e.g., "can0").
 * @return Populated CanInterfaceInfo, or default-initialized if not found.
 * @note Mostly RT-safe: Bounded sysfs reads.
 *
 * Queries:
 *  - Interface existence and state
 *  - Bit timing and controller mode
 *  - Error counters and statistics
 *  - Driver information
 */
[[nodiscard]] CanInterfaceInfo getCanInterfaceInfo(const char* name) noexcept;

/**
 * @brief Get CAN bit timing for an interface.
 * @param name Interface name.
 * @return Bit timing, or default if not readable.
 * @note RT-safe: Bounded netlink query.
 */
[[nodiscard]] CanBitTiming getCanBitTiming(const char* name) noexcept;

/**
 * @brief Get CAN error counters for an interface.
 * @param name Interface name.
 * @return Error counters, or default if not readable.
 * @note RT-safe: Bounded sysfs reads.
 */
[[nodiscard]] CanErrorCounters getCanErrorCounters(const char* name) noexcept;

/**
 * @brief Get CAN bus state for an interface.
 * @param name Interface name.
 * @return Bus state, or UNKNOWN if not readable.
 * @note RT-safe: Bounded sysfs read.
 */
[[nodiscard]] CanBusState getCanBusState(const char* name) noexcept;

/**
 * @brief Enumerate all CAN interfaces on the system.
 * @return List of CAN interface information.
 * @note NOT RT-safe: Directory enumeration over /sys/class/net/.
 *
 * Discovers all interfaces with type "can" (includes physical, virtual, slcan).
 */
[[nodiscard]] CanInterfaceList getAllCanInterfaces() noexcept;

/**
 * @brief Check if an interface is a CAN interface.
 * @param name Interface name.
 * @return true if interface exists and is type "can".
 * @note RT-safe: Single sysfs read.
 */
[[nodiscard]] bool isCanInterface(const char* name) noexcept;

/**
 * @brief Check if a CAN interface exists.
 * @param name Interface name.
 * @return true if interface exists.
 * @note RT-safe: Single stat call.
 */
[[nodiscard]] bool canInterfaceExists(const char* name) noexcept;

} // namespace device

} // namespace seeker

#endif // SEEKER_DEVICE_CAN_BUS_INFO_HPP
