/**
 * @file GpioInfo.cpp
 * @brief GPIO chip enumeration and line information implementation.
 */

#include "src/device/inc/GpioInfo.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace seeker {

namespace device {

/* ----------------------------- Constants ----------------------------- */

namespace {

constexpr std::size_t GPIO_DEV_PREFIX_LEN = 13;

} // namespace

/* ----------------------------- File Helpers ----------------------------- */

namespace {

/// @brief Build device path for GPIO chip.
void buildGpioChipPath(std::int32_t chipNum, char* buffer, std::size_t bufSize) {
  if (buffer == nullptr || bufSize == 0) {
    return;
  }
  std::snprintf(buffer, bufSize, "/dev/gpiochip%d", chipNum);
}

/// @brief Check if file exists.
bool fileExists(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  struct stat st{};
  return (stat(path, &st) == 0);
}

/// @brief Check if file is accessible for reading.
bool isAccessible(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  return (access(path, R_OK) == 0);
}

/// @brief Safe string copy.
void safeCopy(char* dest, std::size_t destSize, const char* src) {
  if (dest == nullptr || destSize == 0) {
    return;
  }
  if (src == nullptr) {
    dest[0] = '\0';
    return;
  }
  std::snprintf(dest, destSize, "%s", src);
}

/// @brief Query GPIO chip info via ioctl.
bool queryChipInfo(const char* path, struct gpiochip_info& info) {
  const int FD = open(path, O_RDONLY | O_CLOEXEC);
  if (FD < 0) {
    return false;
  }

  std::memset(&info, 0, sizeof(info));
  const bool SUCCESS = (ioctl(FD, GPIO_GET_CHIPINFO_IOCTL, &info) == 0);
  close(FD);
  return SUCCESS;
}

/// @brief Query GPIO line info via ioctl (v2 API).
bool queryLineInfo(const char* chipPath, std::uint32_t offset, struct gpio_v2_line_info& info) {
  const int FD = open(chipPath, O_RDONLY | O_CLOEXEC);
  if (FD < 0) {
    return false;
  }

  std::memset(&info, 0, sizeof(info));
  info.offset = offset;
  const bool SUCCESS = (ioctl(FD, GPIO_V2_GET_LINEINFO_IOCTL, &info) == 0);
  close(FD);
  return SUCCESS;
}

/// @brief Parse flags from gpio_v2_line_info.
GpioLineFlags parseLineFlags(const struct gpio_v2_line_info& info) {
  GpioLineFlags flags{};
  const std::uint64_t F = info.flags;

  flags.used = (F & GPIO_V2_LINE_FLAG_USED) != 0;
  flags.activeLow = (F & GPIO_V2_LINE_FLAG_ACTIVE_LOW) != 0;

  if (F & GPIO_V2_LINE_FLAG_INPUT) {
    flags.direction = GpioDirection::INPUT;
  } else if (F & GPIO_V2_LINE_FLAG_OUTPUT) {
    flags.direction = GpioDirection::OUTPUT;
  }

  if (F & GPIO_V2_LINE_FLAG_OPEN_DRAIN) {
    flags.drive = GpioDrive::OPEN_DRAIN;
  } else if (F & GPIO_V2_LINE_FLAG_OPEN_SOURCE) {
    flags.drive = GpioDrive::OPEN_SOURCE;
  } else if (flags.direction == GpioDirection::OUTPUT) {
    flags.drive = GpioDrive::PUSH_PULL;
  }

  if (F & GPIO_V2_LINE_FLAG_BIAS_PULL_UP) {
    flags.bias = GpioBias::PULL_UP;
  } else if (F & GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN) {
    flags.bias = GpioBias::PULL_DOWN;
  } else if (F & GPIO_V2_LINE_FLAG_BIAS_DISABLED) {
    flags.bias = GpioBias::DISABLED;
  }

  if (F & GPIO_V2_LINE_FLAG_EDGE_RISING) {
    if (F & GPIO_V2_LINE_FLAG_EDGE_FALLING) {
      flags.edge = GpioEdge::BOTH;
    } else {
      flags.edge = GpioEdge::RISING;
    }
  } else if (F & GPIO_V2_LINE_FLAG_EDGE_FALLING) {
    flags.edge = GpioEdge::FALLING;
  }

  return flags;
}

/// @brief Count used lines on a chip.
std::uint32_t countUsedLines(const char* chipPath, std::uint32_t numLines) {
  std::uint32_t used = 0;
  const std::uint32_t MAX_CHECK =
      (numLines < MAX_GPIO_LINES_DETAILED) ? numLines : MAX_GPIO_LINES_DETAILED;

  for (std::uint32_t i = 0; i < MAX_CHECK; ++i) {
    struct gpio_v2_line_info info{};
    if (queryLineInfo(chipPath, i, info)) {
      if (info.flags & GPIO_V2_LINE_FLAG_USED) {
        ++used;
      }
    }
  }
  return used;
}

} // namespace

/* ----------------------------- GpioDirection toString ----------------------------- */

const char* toString(GpioDirection dir) noexcept {
  switch (dir) {
  case GpioDirection::INPUT:
    return "input";
  case GpioDirection::OUTPUT:
    return "output";
  case GpioDirection::UNKNOWN:
  default:
    return "unknown";
  }
}

/* ----------------------------- GpioDrive toString ----------------------------- */

const char* toString(GpioDrive drive) noexcept {
  switch (drive) {
  case GpioDrive::PUSH_PULL:
    return "push-pull";
  case GpioDrive::OPEN_DRAIN:
    return "open-drain";
  case GpioDrive::OPEN_SOURCE:
    return "open-source";
  case GpioDrive::UNKNOWN:
  default:
    return "unknown";
  }
}

/* ----------------------------- GpioBias toString ----------------------------- */

const char* toString(GpioBias bias) noexcept {
  switch (bias) {
  case GpioBias::DISABLED:
    return "disabled";
  case GpioBias::PULL_UP:
    return "pull-up";
  case GpioBias::PULL_DOWN:
    return "pull-down";
  case GpioBias::UNKNOWN:
  default:
    return "unknown";
  }
}

/* ----------------------------- GpioEdge toString ----------------------------- */

const char* toString(GpioEdge edge) noexcept {
  switch (edge) {
  case GpioEdge::RISING:
    return "rising";
  case GpioEdge::FALLING:
    return "falling";
  case GpioEdge::BOTH:
    return "both";
  case GpioEdge::NONE:
  default:
    return "none";
  }
}

/* ----------------------------- GpioLineFlags Methods ----------------------------- */

bool GpioLineFlags::hasSpecialConfig() const noexcept {
  return activeLow || (drive != GpioDrive::UNKNOWN && drive != GpioDrive::PUSH_PULL) ||
         (bias != GpioBias::UNKNOWN && bias != GpioBias::DISABLED) || (edge != GpioEdge::NONE);
}

std::string GpioLineFlags::toString() const {
  std::string result;
  result.reserve(128);

  result += "direction=";
  result += device::toString(direction);

  if (used) {
    result += " [used]";
  }
  if (activeLow) {
    result += " [active-low]";
  }
  if (drive != GpioDrive::UNKNOWN) {
    result += " drive=";
    result += device::toString(drive);
  }
  if (bias != GpioBias::UNKNOWN) {
    result += " bias=";
    result += device::toString(bias);
  }
  if (edge != GpioEdge::NONE) {
    result += " edge=";
    result += device::toString(edge);
  }

  return result;
}

/* ----------------------------- GpioLineInfo Methods ----------------------------- */

bool GpioLineInfo::hasName() const noexcept { return name[0] != '\0'; }

bool GpioLineInfo::isUsed() const noexcept { return flags.used; }

std::string GpioLineInfo::toString() const {
  std::string result;
  result.reserve(256);

  char buf[32];
  std::snprintf(buf, sizeof(buf), "line %3u: ", offset);
  result += buf;

  if (name[0] != '\0') {
    result += "\"";
    result += name.data();
    result += "\"";
  } else {
    result += "(unnamed)";
  }

  if (consumer[0] != '\0') {
    result += " consumer=\"";
    result += consumer.data();
    result += "\"";
  }

  result += " ";
  result += flags.toString();

  return result;
}

/* ----------------------------- GpioChipInfo Methods ----------------------------- */

bool GpioChipInfo::isUsable() const noexcept { return exists && accessible; }

std::string GpioChipInfo::toString() const {
  std::string result;
  result.reserve(256);

  result += name.data();
  result += " [";
  result += label.data();
  result += "] ";

  char buf[64];
  std::snprintf(buf, sizeof(buf), "%u lines", numLines);
  result += buf;

  if (linesUsed > 0) {
    std::snprintf(buf, sizeof(buf), " (%u used)", linesUsed);
    result += buf;
  }

  if (!exists) {
    result += " [not found]";
  } else if (!accessible) {
    result += " [no access]";
  }

  return result;
}

/* ----------------------------- GpioChipList Methods ----------------------------- */

const GpioChipInfo* GpioChipList::find(const char* name) const noexcept {
  if (name == nullptr || name[0] == '\0') {
    return nullptr;
  }

  const char* searchName = name;
  if (std::strncmp(name, "/dev/", 5) == 0) {
    searchName = name + 5;
  }

  for (std::size_t i = 0; i < count; ++i) {
    if (std::strcmp(chips[i].name.data(), searchName) == 0) {
      return &chips[i];
    }
  }
  return nullptr;
}

const GpioChipInfo* GpioChipList::findByNumber(std::int32_t chipNum) const noexcept {
  for (std::size_t i = 0; i < count; ++i) {
    if (chips[i].chipNumber == chipNum) {
      return &chips[i];
    }
  }
  return nullptr;
}

bool GpioChipList::empty() const noexcept { return count == 0; }

std::uint32_t GpioChipList::totalLines() const noexcept {
  std::uint32_t total = 0;
  for (std::size_t i = 0; i < count; ++i) {
    total += chips[i].numLines;
  }
  return total;
}

std::uint32_t GpioChipList::totalUsed() const noexcept {
  std::uint32_t total = 0;
  for (std::size_t i = 0; i < count; ++i) {
    total += chips[i].linesUsed;
  }
  return total;
}

std::string GpioChipList::toString() const {
  std::string result;
  result.reserve(count * 128 + 64);

  char buf[64];
  std::snprintf(buf, sizeof(buf), "GPIO chips: %zu\n", count);
  result += buf;

  for (std::size_t i = 0; i < count; ++i) {
    result += "  ";
    result += chips[i].toString();
    result += "\n";
  }

  return result;
}

/* ----------------------------- GpioLineList Methods ----------------------------- */

const GpioLineInfo* GpioLineList::findByOffset(std::uint32_t offset) const noexcept {
  for (std::size_t i = 0; i < count; ++i) {
    if (lines[i].offset == offset) {
      return &lines[i];
    }
  }
  return nullptr;
}

const GpioLineInfo* GpioLineList::findByName(const char* name) const noexcept {
  if (name == nullptr || name[0] == '\0') {
    return nullptr;
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (std::strcmp(lines[i].name.data(), name) == 0) {
      return &lines[i];
    }
  }
  return nullptr;
}

bool GpioLineList::empty() const noexcept { return count == 0; }

std::size_t GpioLineList::countUsed() const noexcept {
  std::size_t used = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (lines[i].flags.used) {
      ++used;
    }
  }
  return used;
}

std::size_t GpioLineList::countInputs() const noexcept {
  std::size_t inputs = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (lines[i].flags.direction == GpioDirection::INPUT) {
      ++inputs;
    }
  }
  return inputs;
}

std::size_t GpioLineList::countOutputs() const noexcept {
  std::size_t outputs = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (lines[i].flags.direction == GpioDirection::OUTPUT) {
      ++outputs;
    }
  }
  return outputs;
}

std::string GpioLineList::toString() const {
  std::string result;
  result.reserve(count * 128 + 64);

  char buf[64];
  std::snprintf(buf, sizeof(buf), "GPIO lines (chip %d): %zu\n", chipNumber, count);
  result += buf;

  for (std::size_t i = 0; i < count; ++i) {
    result += "  ";
    result += lines[i].toString();
    result += "\n";
  }

  return result;
}

/* ----------------------------- API ----------------------------- */

GpioChipInfo getGpioChipInfo(std::int32_t chipNum) noexcept {
  GpioChipInfo result{};

  if (chipNum < 0) {
    return result;
  }

  char pathBuf[GPIO_PATH_SIZE];
  buildGpioChipPath(chipNum, pathBuf, sizeof(pathBuf));

  result.chipNumber = chipNum;
  safeCopy(result.path.data(), GPIO_PATH_SIZE, pathBuf);

  char nameBuf[GPIO_NAME_SIZE];
  std::snprintf(nameBuf, sizeof(nameBuf), "gpiochip%d", chipNum);
  safeCopy(result.name.data(), GPIO_NAME_SIZE, nameBuf);

  result.exists = fileExists(pathBuf);
  if (!result.exists) {
    return result;
  }

  result.accessible = isAccessible(pathBuf);
  if (!result.accessible) {
    return result;
  }

  struct gpiochip_info info{};
  if (queryChipInfo(pathBuf, info)) {
    safeCopy(result.label.data(), GPIO_LABEL_SIZE, info.label);
    result.numLines = info.lines;
    result.linesUsed = countUsedLines(pathBuf, info.lines);
  }

  return result;
}

GpioChipInfo getGpioChipInfoByName(const char* name) noexcept {
  GpioChipInfo result{};

  if (name == nullptr || name[0] == '\0') {
    return result;
  }

  std::int32_t chipNum = -1;
  if (parseGpioChipNumber(name, chipNum)) {
    return getGpioChipInfo(chipNum);
  }

  return result;
}

GpioLineInfo getGpioLineInfo(std::int32_t chipNum, std::uint32_t lineOffset) noexcept {
  GpioLineInfo result{};
  result.offset = lineOffset;

  if (chipNum < 0) {
    return result;
  }

  char pathBuf[GPIO_PATH_SIZE];
  buildGpioChipPath(chipNum, pathBuf, sizeof(pathBuf));

  if (!fileExists(pathBuf) || !isAccessible(pathBuf)) {
    return result;
  }

  struct gpio_v2_line_info info{};
  if (queryLineInfo(pathBuf, lineOffset, info)) {
    result.offset = info.offset;
    safeCopy(result.name.data(), GPIO_NAME_SIZE, info.name);
    safeCopy(result.consumer.data(), GPIO_LABEL_SIZE, info.consumer);
    result.flags = parseLineFlags(info);
  }

  return result;
}

GpioLineList getGpioLines(std::int32_t chipNum) noexcept {
  GpioLineList result{};
  result.chipNumber = chipNum;

  if (chipNum < 0) {
    return result;
  }

  char pathBuf[GPIO_PATH_SIZE];
  buildGpioChipPath(chipNum, pathBuf, sizeof(pathBuf));

  if (!fileExists(pathBuf) || !isAccessible(pathBuf)) {
    return result;
  }

  struct gpiochip_info chipInfo{};
  if (!queryChipInfo(pathBuf, chipInfo)) {
    return result;
  }

  const std::uint32_t MAX_LINES =
      (chipInfo.lines < MAX_GPIO_LINES_DETAILED) ? chipInfo.lines : MAX_GPIO_LINES_DETAILED;

  for (std::uint32_t i = 0; i < MAX_LINES; ++i) {
    struct gpio_v2_line_info lineInfo{};
    if (queryLineInfo(pathBuf, i, lineInfo)) {
      GpioLineInfo& line = result.lines[result.count];
      line.offset = lineInfo.offset;
      safeCopy(line.name.data(), GPIO_NAME_SIZE, lineInfo.name);
      safeCopy(line.consumer.data(), GPIO_LABEL_SIZE, lineInfo.consumer);
      line.flags = parseLineFlags(lineInfo);
      ++result.count;
    }
  }

  return result;
}

GpioChipList getAllGpioChips() noexcept {
  GpioChipList result{};

  DIR* dir = opendir("/dev");
  if (dir == nullptr) {
    return result;
  }

  struct dirent* entry = nullptr;
  while ((entry = readdir(dir)) != nullptr && result.count < MAX_GPIO_CHIPS) {
    if (std::strncmp(entry->d_name, "gpiochip", 8) != 0) {
      continue;
    }

    std::int32_t chipNum = -1;
    if (!parseGpioChipNumber(entry->d_name, chipNum)) {
      continue;
    }

    result.chips[result.count] = getGpioChipInfo(chipNum);
    if (result.chips[result.count].exists) {
      ++result.count;
    }
  }

  closedir(dir);
  return result;
}

bool gpioChipExists(std::int32_t chipNum) noexcept {
  if (chipNum < 0) {
    return false;
  }

  char pathBuf[GPIO_PATH_SIZE];
  buildGpioChipPath(chipNum, pathBuf, sizeof(pathBuf));
  return fileExists(pathBuf);
}

bool parseGpioChipNumber(const char* name, std::int32_t& outChipNum) noexcept {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }

  const char* numStart = name;

  if (std::strncmp(name, "/dev/gpiochip", GPIO_DEV_PREFIX_LEN) == 0) {
    numStart = name + GPIO_DEV_PREFIX_LEN;
  } else if (std::strncmp(name, "gpiochip", 8) == 0) {
    numStart = name + 8;
  } else {
    return false;
  }

  if (*numStart == '\0') {
    return false;
  }

  char* endPtr = nullptr;
  const long NUM = std::strtol(numStart, &endPtr, 10);

  if (endPtr == numStart || *endPtr != '\0') {
    return false;
  }
  if (NUM < 0 || NUM > 999) {
    return false;
  }

  outChipNum = static_cast<std::int32_t>(NUM);
  return true;
}

bool findGpioLine(std::int32_t gpioNum, std::int32_t& outChipNum,
                  std::uint32_t& outOffset) noexcept {
  if (gpioNum < 0) {
    return false;
  }

  GpioChipList chips = getAllGpioChips();

  std::int32_t baseNum = 0;
  for (std::size_t i = 0; i < chips.count; ++i) {
    const GpioChipInfo& chip = chips.chips[i];
    if (!chip.isUsable()) {
      continue;
    }

    if (gpioNum >= baseNum && static_cast<std::uint32_t>(gpioNum - baseNum) < chip.numLines) {
      outChipNum = chip.chipNumber;
      outOffset = static_cast<std::uint32_t>(gpioNum - baseNum);
      return true;
    }

    baseNum += static_cast<std::int32_t>(chip.numLines);
  }

  return false;
}

} // namespace device

} // namespace seeker