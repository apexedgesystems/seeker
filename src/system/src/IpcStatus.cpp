/**
 * @file IpcStatus.cpp
 * @brief Implementation of IPC resource status collection.
 */

#include "src/system/inc/IpcStatus.hpp"
#include "src/helpers/inc/Files.hpp"
#include "src/helpers/inc/Format.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace system {

namespace {

using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::format::bytesBinary;

/// Read unsigned 64-bit integer from file.
std::uint64_t readUint64FromFile(const char* path, std::uint64_t defaultVal = 0) noexcept {
  std::array<char, 32> buf{};
  if (readFileToBuffer(path, buf.data(), buf.size()) == 0) {
    return defaultVal;
  }
  return std::strtoull(buf.data(), nullptr, 10);
}

/// Read unsigned 32-bit integer from file.
std::uint32_t readUint32FromFile(const char* path, std::uint32_t defaultVal = 0) noexcept {
  std::array<char, 32> buf{};
  if (readFileToBuffer(path, buf.data(), buf.size()) == 0) {
    return defaultVal;
  }
  return static_cast<std::uint32_t>(std::strtoul(buf.data(), nullptr, 10));
}

/// Get system page size.
std::uint64_t getPageSize() noexcept {
  const long PS = ::sysconf(_SC_PAGESIZE);
  return (PS > 0) ? static_cast<std::uint64_t>(PS) : 4096;
}

/// Count lines in a file (for /proc/sysvipc/* counting).
std::size_t countFileLines(const char* path) noexcept {
  std::array<char, 16384> buf{};
  const std::size_t LEN = readFileToBuffer(path, buf.data(), buf.size());
  if (LEN == 0) {
    return 0;
  }

  std::size_t lines = 0;
  for (std::size_t i = 0; i < LEN; ++i) {
    if (buf[i] == '\n') {
      ++lines;
    }
  }
  return lines;
}

/// Count entries in directory.
std::size_t countDirEntries(const char* path) noexcept {
  DIR* dir = ::opendir(path);
  if (dir == nullptr) {
    return 0;
  }

  std::size_t count = 0;
  struct dirent* entry = nullptr;
  while ((entry = ::readdir(dir)) != nullptr) {
    // Skip . and ..
    if (entry->d_name[0] == '.') {
      if (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0')) {
        continue;
      }
    }
    ++count;
  }

  ::closedir(dir);
  return count;
}

/// Parse a line from /proc/sysvipc/shm.
bool parseShmLine(const char* line, ShmSegment& seg) noexcept {
  // Format: key shmid perms size cpid lpid nattch uid gid cuid cgid atime dtime ctime rss swap
  std::int32_t key = 0;
  std::int32_t shmid = 0;
  std::uint32_t perms = 0;
  std::uint64_t size = 0;
  std::uint32_t nattch = 0;
  std::uint32_t uid = 0;
  std::uint32_t gid = 0;

  // Simple parsing - just get the key fields we need
  const int PARSED = std::sscanf(line, "%d %d %o %lu %*d %*d %u %u %u", &key, &shmid, &perms, &size,
                                 &nattch, &uid, &gid);

  if (PARSED < 7) {
    return false;
  }

  seg.key = key;
  seg.shmid = shmid;
  seg.mode = perms;
  seg.size = size;
  seg.nattch = nattch;
  seg.uid = uid;
  seg.gid = gid;
  seg.markedForDeletion = ((perms & 01000) != 0); // SHM_DEST flag

  return true;
}

} // namespace

/* ----------------------------- ShmLimits Methods ----------------------------- */

std::uint64_t ShmLimits::maxTotalBytes() const noexcept { return shmall * pageSize; }

std::string ShmLimits::toString() const {
  if (!valid) {
    return "Shared Memory Limits: not available";
  }

  return fmt::format("Shared Memory Limits:\n"
                     "  Max segment size:  {}\n"
                     "  Max total memory:  {} ({} pages)\n"
                     "  Max segments:      {}",
                     bytesBinary(shmmax), bytesBinary(maxTotalBytes()), shmall, shmmni);
}

/* ----------------------------- SemLimits Methods ----------------------------- */

std::string SemLimits::toString() const {
  if (!valid) {
    return "Semaphore Limits: not available";
  }

  return fmt::format("Semaphore Limits:\n"
                     "  Max arrays:           {}\n"
                     "  Max sems per array:   {}\n"
                     "  Max sems system-wide: {}\n"
                     "  Max ops per semop:    {}",
                     semmni, semmsl, semmns, semopm);
}

/* ----------------------------- MsgLimits Methods ----------------------------- */

std::string MsgLimits::toString() const {
  if (!valid) {
    return "Message Queue Limits: not available";
  }

  return fmt::format("Message Queue Limits:\n"
                     "  Max queues:         {}\n"
                     "  Max message size:   {}\n"
                     "  Max bytes per queue: {}",
                     msgmni, bytesBinary(msgmax), bytesBinary(msgmnb));
}

/* ----------------------------- PosixMqLimits Methods ----------------------------- */

std::string PosixMqLimits::toString() const {
  if (!valid) {
    return "POSIX MQ Limits: not available";
  }

  return fmt::format("POSIX MQ Limits:\n"
                     "  Max queues per user: {}\n"
                     "  Max msgs per queue:  {}\n"
                     "  Max message size:    {}",
                     queuesMax, msgMax, bytesBinary(msgsizeMax));
}

/* ----------------------------- ShmSegment Methods ----------------------------- */

bool ShmSegment::canAttach(std::uint32_t processUid) const noexcept {
  // Root can always attach
  if (processUid == 0) {
    return true;
  }
  // Owner can attach
  if (uid == processUid) {
    return true;
  }
  // Check world read permission
  return (mode & 0004) != 0;
}

/* ----------------------------- ShmStatus Methods ----------------------------- */

bool ShmStatus::isNearSegmentLimit() const noexcept {
  if (!limits.valid || limits.shmmni == 0) {
    return false;
  }
  return segmentCount >= (limits.shmmni * 9 / 10); // 90% threshold
}

bool ShmStatus::isNearMemoryLimit() const noexcept {
  if (!limits.valid || limits.maxTotalBytes() == 0) {
    return false;
  }
  return totalBytes >= (limits.maxTotalBytes() * 9 / 10);
}

const ShmSegment* ShmStatus::find(std::int32_t shmid) const noexcept {
  for (std::size_t i = 0; i < segmentCount; ++i) {
    if (segments[i].shmid == shmid) {
      return &segments[i];
    }
  }
  return nullptr;
}

std::string ShmStatus::toString() const {
  std::string out;
  out.reserve(256);

  out += "Shared Memory Status:\n";
  out += fmt::format("  Segments in use: {} / {}\n", segmentCount, limits.shmmni);
  out += fmt::format("  Memory in use:   {} / {}\n", bytesBinary(totalBytes),
                     bytesBinary(limits.maxTotalBytes()));

  if (isNearSegmentLimit()) {
    out += "  WARNING: Near segment limit!\n";
  }
  if (isNearMemoryLimit()) {
    out += "  WARNING: Near memory limit!\n";
  }

  return out;
}

/* ----------------------------- SemStatus Methods ----------------------------- */

bool SemStatus::isNearArrayLimit() const noexcept {
  if (!limits.valid || limits.semmni == 0) {
    return false;
  }
  return arraysInUse >= (limits.semmni * 9 / 10);
}

bool SemStatus::isNearSemLimit() const noexcept {
  if (!limits.valid || limits.semmns == 0) {
    return false;
  }
  return semsInUse >= (limits.semmns * 9 / 10);
}

std::string SemStatus::toString() const {
  std::string out;
  out.reserve(128);

  out += "Semaphore Status:\n";
  out += fmt::format("  Arrays in use:     {} / {}\n", arraysInUse, limits.semmni);
  out += fmt::format("  Semaphores in use: {} / {}\n", semsInUse, limits.semmns);

  if (isNearArrayLimit()) {
    out += "  WARNING: Near array limit!\n";
  }
  if (isNearSemLimit()) {
    out += "  WARNING: Near semaphore limit!\n";
  }

  return out;
}

/* ----------------------------- MsgStatus Methods ----------------------------- */

bool MsgStatus::isNearQueueLimit() const noexcept {
  if (!limits.valid || limits.msgmni == 0) {
    return false;
  }
  return queuesInUse >= (limits.msgmni * 9 / 10);
}

std::string MsgStatus::toString() const {
  std::string out;
  out.reserve(128);

  out += "Message Queue Status:\n";
  out += fmt::format("  Queues in use:   {} / {}\n", queuesInUse, limits.msgmni);
  out += fmt::format("  Total messages:  {}\n", totalMessages);
  out += fmt::format("  Total bytes:     {}\n", bytesBinary(totalBytes));

  if (isNearQueueLimit()) {
    out += "  WARNING: Near queue limit!\n";
  }

  return out;
}

/* ----------------------------- PosixMqStatus Methods ----------------------------- */

std::string PosixMqStatus::toString() const {
  std::string out;
  out.reserve(128);

  out += "POSIX MQ Status:\n";
  out += fmt::format("  Queues in use: {} / {}\n", queuesInUse, limits.queuesMax);

  return out;
}

/* ----------------------------- IpcStatus Methods ----------------------------- */

bool IpcStatus::isNearAnyLimit() const noexcept {
  return shm.isNearSegmentLimit() || shm.isNearMemoryLimit() || sem.isNearArrayLimit() ||
         sem.isNearSemLimit() || msg.isNearQueueLimit();
}

int IpcStatus::rtScore() const noexcept {
  int score = 100;

  // Deduct for being near limits
  if (shm.isNearSegmentLimit())
    score -= 15;
  if (shm.isNearMemoryLimit())
    score -= 15;
  if (sem.isNearArrayLimit())
    score -= 15;
  if (sem.isNearSemLimit())
    score -= 15;
  if (msg.isNearQueueLimit())
    score -= 10;

  // Bonus for having adequate limits
  if (shm.limits.valid && shm.limits.shmmax >= 1024ULL * 1024 * 1024) {
    score += 5; // Can allocate large segments
  }
  if (posixMq.limits.valid && posixMq.limits.msgsizeMax >= 8192) {
    score += 5; // Can send decent-sized messages
  }

  // Clamp
  if (score < 0)
    score = 0;
  if (score > 100)
    score = 100;

  return score;
}

std::string IpcStatus::toString() const {
  std::string out;
  out.reserve(1024);

  out += "IPC Status:\n\n";

  out += shm.toString();
  out += "\n\n";

  out += sem.toString();
  out += "\n\n";

  out += msg.toString();
  out += "\n\n";

  out += posixMq.toString();
  out += "\n\n";

  out += fmt::format("RT Score: {}/100", rtScore());
  if (isNearAnyLimit()) {
    out += " (WARNING: near limits)";
  }
  out += "\n";

  return out;
}

/* ----------------------------- API ----------------------------- */

ShmLimits getShmLimits() noexcept {
  ShmLimits limits{};

  limits.shmmax = readUint64FromFile("/proc/sys/kernel/shmmax", 0);
  limits.shmall = readUint64FromFile("/proc/sys/kernel/shmall", 0);
  limits.shmmni = readUint32FromFile("/proc/sys/kernel/shmmni", 0);
  limits.pageSize = getPageSize();

  limits.valid = (limits.shmmax > 0 && limits.shmmni > 0);
  return limits;
}

SemLimits getSemLimits() noexcept {
  SemLimits limits{};

  // /proc/sys/kernel/sem contains: semmsl semmns semopm semmni
  std::array<char, 64> buf{};
  if (readFileToBuffer("/proc/sys/kernel/sem", buf.data(), buf.size()) > 0) {
    const int PARSED = std::sscanf(buf.data(), "%u %u %u %u", &limits.semmsl, &limits.semmns,
                                   &limits.semopm, &limits.semmni);
    limits.valid = (PARSED == 4);
  }

  return limits;
}

ShmStatus getShmStatus() noexcept {
  ShmStatus status{};
  status.limits = getShmLimits();

  // Parse /proc/sysvipc/shm for segment information
  std::array<char, 16384> buf{};
  const std::size_t LEN = readFileToBuffer("/proc/sysvipc/shm", buf.data(), buf.size());
  if (LEN == 0) {
    return status;
  }

  // Skip header line
  const char* line = buf.data();
  const char* nextLine = std::strchr(line, '\n');
  if (nextLine == nullptr) {
    return status;
  }
  line = nextLine + 1;

  // Parse each segment line
  while (line < buf.data() + LEN && status.segmentCount < MAX_IPC_ENTRIES) {
    nextLine = std::strchr(line, '\n');
    const std::size_t LINE_LEN =
        (nextLine != nullptr) ? static_cast<std::size_t>(nextLine - line) : std::strlen(line);

    if (LINE_LEN > 0) {
      // Copy line to temporary buffer for parsing
      std::array<char, 256> lineBuf{};
      const std::size_t COPY_LEN = (LINE_LEN < lineBuf.size() - 1) ? LINE_LEN : lineBuf.size() - 1;
      std::memcpy(lineBuf.data(), line, COPY_LEN);
      lineBuf[COPY_LEN] = '\0';

      ShmSegment seg{};
      if (parseShmLine(lineBuf.data(), seg)) {
        status.segments[status.segmentCount] = seg;
        status.totalBytes += seg.size;
        ++status.segmentCount;
      }
    }

    if (nextLine == nullptr) {
      break;
    }
    line = nextLine + 1;
  }

  return status;
}

SemStatus getSemStatus() noexcept {
  SemStatus status{};
  status.limits = getSemLimits();

  // Count lines in /proc/sysvipc/sem (minus header)
  const std::size_t LINES = countFileLines("/proc/sysvipc/sem");
  status.arraysInUse = (LINES > 1) ? static_cast<std::uint32_t>(LINES - 1) : 0;

  // Parse to count total semaphores
  std::array<char, 8192> buf{};
  const std::size_t LEN = readFileToBuffer("/proc/sysvipc/sem", buf.data(), buf.size());
  if (LEN > 0) {
    // Skip header
    const char* line = std::strchr(buf.data(), '\n');
    if (line != nullptr) {
      ++line;
      while (*line != '\0') {
        // Each line has: key semid perms nsems ...
        // We want nsems (4th field)
        std::uint32_t nsems = 0;
        if (std::sscanf(line, "%*d %*d %*o %u", &nsems) == 1) {
          status.semsInUse += nsems;
        }
        const char* nextLine = std::strchr(line, '\n');
        if (nextLine == nullptr) {
          break;
        }
        line = nextLine + 1;
      }
    }
  }

  return status;
}

MsgStatus getMsgStatus() noexcept {
  MsgStatus status{};

  // Read limits
  status.limits.msgmax = readUint64FromFile("/proc/sys/kernel/msgmax", 0);
  status.limits.msgmnb = readUint64FromFile("/proc/sys/kernel/msgmnb", 0);
  status.limits.msgmni = readUint32FromFile("/proc/sys/kernel/msgmni", 0);
  status.limits.valid = (status.limits.msgmni > 0);

  // Count lines in /proc/sysvipc/msg (minus header)
  const std::size_t LINES = countFileLines("/proc/sysvipc/msg");
  status.queuesInUse = (LINES > 1) ? static_cast<std::uint32_t>(LINES - 1) : 0;

  // Parse to count total messages and bytes
  std::array<char, 8192> buf{};
  const std::size_t LEN = readFileToBuffer("/proc/sysvipc/msg", buf.data(), buf.size());
  if (LEN > 0) {
    const char* line = std::strchr(buf.data(), '\n');
    if (line != nullptr) {
      ++line;
      while (*line != '\0') {
        // Format: key msqid perms cbytes qnum ...
        std::uint64_t cbytes = 0;
        std::uint64_t qnum = 0;
        if (std::sscanf(line, "%*d %*d %*o %lu %lu", &cbytes, &qnum) == 2) {
          status.totalBytes += cbytes;
          status.totalMessages += qnum;
        }
        const char* nextLine = std::strchr(line, '\n');
        if (nextLine == nullptr) {
          break;
        }
        line = nextLine + 1;
      }
    }
  }

  return status;
}

PosixMqStatus getPosixMqStatus() noexcept {
  PosixMqStatus status{};

  // Read limits
  status.limits.queuesMax = readUint32FromFile("/proc/sys/fs/mqueue/queues_max", 0);
  status.limits.msgMax = readUint32FromFile("/proc/sys/fs/mqueue/msg_max", 0);
  status.limits.msgsizeMax = readUint64FromFile("/proc/sys/fs/mqueue/msgsize_max", 0);
  status.limits.valid = (status.limits.queuesMax > 0);

  // Count queues in /dev/mqueue
  status.queuesInUse = static_cast<std::uint32_t>(countDirEntries("/dev/mqueue"));

  return status;
}

IpcStatus getIpcStatus() noexcept {
  IpcStatus status{};

  status.shm = getShmStatus();
  status.sem = getSemStatus();
  status.msg = getMsgStatus();
  status.posixMq = getPosixMqStatus();

  return status;
}

} // namespace system

} // namespace seeker