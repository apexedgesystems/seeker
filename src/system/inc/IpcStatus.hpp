#ifndef SEEKER_SYSTEM_IPC_STATUS_HPP
#define SEEKER_SYSTEM_IPC_STATUS_HPP
/**
 * @file IpcStatus.hpp
 * @brief System V and POSIX IPC resource status (Linux).
 * @note Linux-only. Reads /proc/sysvipc/, /dev/mqueue/, and kernel sysctls.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Use cases for RT systems:
 *  - Verify shared memory limits before allocating RT buffers
 *  - Check message queue limits for RT communication channels
 *  - Audit semaphore availability for RT synchronization
 *  - Detect IPC resource exhaustion before failures occur
 */

#include <cstddef> // std::size_t
#include <cstdint> // std::uint64_t, std::uint32_t
#include <string>  // std::string

namespace seeker {

namespace system {

/* ----------------------------- Constants ----------------------------- */

/// Maximum number of IPC entries to enumerate per type.
inline constexpr std::size_t MAX_IPC_ENTRIES = 64;

/// Buffer size for IPC key/identifier strings.
inline constexpr std::size_t IPC_KEY_SIZE = 32;

/// Buffer size for POSIX mqueue names.
inline constexpr std::size_t MQUEUE_NAME_SIZE = 64;

/* ----------------------------- ShmLimits ----------------------------- */

/**
 * @brief System V shared memory limits.
 *
 * Kernel limits on shared memory segments and sizes.
 */
struct ShmLimits {
  std::uint64_t shmmax{0}; ///< Maximum segment size (bytes)
  std::uint64_t shmall{0}; ///< Maximum total shared memory (pages)
  std::uint32_t shmmni{0}; ///< Maximum number of segments
  std::uint32_t shmmin{1}; ///< Minimum segment size (always 1)

  /// Page size for shmall calculations.
  std::uint64_t pageSize{4096};

  /// True if successfully read.
  bool valid{false};

  /// @brief Get maximum total bytes (shmall * pageSize).
  [[nodiscard]] std::uint64_t maxTotalBytes() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- SemLimits ----------------------------- */

/**
 * @brief System V semaphore limits.
 *
 * Kernel limits on semaphore arrays and operations.
 */
struct SemLimits {
  std::uint32_t semmsl{0}; ///< Max semaphores per array
  std::uint32_t semmns{0}; ///< Max semaphores system-wide
  std::uint32_t semopm{0}; ///< Max ops per semop call
  std::uint32_t semmni{0}; ///< Max semaphore arrays

  /// True if successfully read.
  bool valid{false};

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- MsgLimits ----------------------------- */

/**
 * @brief System V message queue limits.
 *
 * Kernel limits on message queues.
 */
struct MsgLimits {
  std::uint64_t msgmax{0}; ///< Max message size (bytes)
  std::uint64_t msgmnb{0}; ///< Max bytes per queue
  std::uint32_t msgmni{0}; ///< Max number of queues

  /// True if successfully read.
  bool valid{false};

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- PosixMqLimits ----------------------------- */

/**
 * @brief POSIX message queue limits.
 *
 * Limits from /proc/sys/fs/mqueue/.
 */
struct PosixMqLimits {
  std::uint32_t queuesMax{0};  ///< Max queues per user
  std::uint32_t msgMax{0};     ///< Max messages per queue (default)
  std::uint64_t msgsizeMax{0}; ///< Max message size (bytes, default)

  /// True if successfully read.
  bool valid{false};

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- ShmSegment ----------------------------- */

/**
 * @brief Information about a single shared memory segment.
 */
struct ShmSegment {
  std::int32_t shmid{-1};        ///< Segment identifier
  std::int32_t key{0};           ///< Key (or IPC_PRIVATE)
  std::uint64_t size{0};         ///< Size in bytes
  std::uint32_t nattch{0};       ///< Number of attached processes
  std::uint32_t uid{0};          ///< Owner UID
  std::uint32_t gid{0};          ///< Owner GID
  std::uint32_t mode{0};         ///< Permissions
  bool markedForDeletion{false}; ///< Marked for removal

  /// @brief Check if this process can attach.
  /// @param processUid Current process UID.
  /// @return True if permissions allow attach.
  [[nodiscard]] bool canAttach(std::uint32_t processUid) const noexcept;
};

/* ----------------------------- ShmStatus ----------------------------- */

/**
 * @brief System V shared memory status.
 */
struct ShmStatus {
  ShmLimits limits{};
  ShmSegment segments[MAX_IPC_ENTRIES]{};
  std::size_t segmentCount{0};

  /// Total bytes in use.
  std::uint64_t totalBytes{0};

  /// @brief Check if near segment limit.
  [[nodiscard]] bool isNearSegmentLimit() const noexcept;

  /// @brief Check if near total memory limit.
  [[nodiscard]] bool isNearMemoryLimit() const noexcept;

  /// @brief Find segment by ID.
  [[nodiscard]] const ShmSegment* find(std::int32_t shmid) const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- SemStatus ----------------------------- */

/**
 * @brief System V semaphore status.
 */
struct SemStatus {
  SemLimits limits{};

  /// Number of semaphore arrays in use.
  std::uint32_t arraysInUse{0};

  /// Total semaphores in use.
  std::uint32_t semsInUse{0};

  /// @brief Check if near array limit.
  [[nodiscard]] bool isNearArrayLimit() const noexcept;

  /// @brief Check if near semaphore limit.
  [[nodiscard]] bool isNearSemLimit() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- MsgStatus ----------------------------- */

/**
 * @brief System V message queue status.
 */
struct MsgStatus {
  MsgLimits limits{};

  /// Number of queues in use.
  std::uint32_t queuesInUse{0};

  /// Total messages across all queues.
  std::uint64_t totalMessages{0};

  /// Total bytes across all queues.
  std::uint64_t totalBytes{0};

  /// @brief Check if near queue limit.
  [[nodiscard]] bool isNearQueueLimit() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- PosixMqStatus ----------------------------- */

/**
 * @brief POSIX message queue status.
 */
struct PosixMqStatus {
  PosixMqLimits limits{};

  /// Number of queues found in /dev/mqueue.
  std::uint32_t queuesInUse{0};

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- IpcStatus ----------------------------- */

/**
 * @brief Complete IPC status snapshot.
 *
 * Aggregates System V and POSIX IPC status for comprehensive
 * IPC resource monitoring.
 */
struct IpcStatus {
  ShmStatus shm{};         ///< Shared memory status
  SemStatus sem{};         ///< Semaphore status
  MsgStatus msg{};         ///< Message queue status
  PosixMqStatus posixMq{}; ///< POSIX mqueue status

  /// @brief Check if any IPC subsystem is near limits.
  [[nodiscard]] bool isNearAnyLimit() const noexcept;

  /// @brief RT readiness score for IPC (0-100).
  [[nodiscard]] int rtScore() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query complete IPC status.
 * @return Populated IpcStatus structure.
 * @note RT-safe: Bounded file reads, fixed-size output.
 *
 * Sources:
 *  - /proc/sys/kernel/shm* for shared memory limits
 *  - /proc/sys/kernel/sem for semaphore limits
 *  - /proc/sys/kernel/msg* for message queue limits
 *  - /proc/sys/fs/mqueue/ (all files) for POSIX mqueue limits
 *  - /proc/sysvipc/ (shm, sem, msg) for resource usage
 *  - /dev/mqueue for POSIX queue enumeration
 */
[[nodiscard]] IpcStatus getIpcStatus() noexcept;

/**
 * @brief Query shared memory status only.
 * @return Populated ShmStatus structure.
 * @note RT-safe: Bounded file reads.
 */
[[nodiscard]] ShmStatus getShmStatus() noexcept;

/**
 * @brief Query semaphore status only.
 * @return Populated SemStatus structure.
 * @note RT-safe: Bounded file reads.
 */
[[nodiscard]] SemStatus getSemStatus() noexcept;

/**
 * @brief Query System V message queue status only.
 * @return Populated MsgStatus structure.
 * @note RT-safe: Bounded file reads.
 */
[[nodiscard]] MsgStatus getMsgStatus() noexcept;

/**
 * @brief Query POSIX message queue status.
 * @return Populated PosixMqStatus structure.
 * @note RT-safe: Bounded directory enumeration.
 */
[[nodiscard]] PosixMqStatus getPosixMqStatus() noexcept;

/**
 * @brief Query shared memory limits only.
 * @return Populated ShmLimits structure.
 * @note RT-safe: Few file reads.
 */
[[nodiscard]] ShmLimits getShmLimits() noexcept;

/**
 * @brief Query semaphore limits only.
 * @return Populated SemLimits structure.
 * @note RT-safe: Single file read.
 */
[[nodiscard]] SemLimits getSemLimits() noexcept;

} // namespace system

} // namespace seeker

#endif // SEEKER_SYSTEM_IPC_STATUS_HPP