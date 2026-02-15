/**
 * @file GpuTelemetry.cpp
 * @brief GPU telemetry collection via NVML.
 * @note Queries temperature, power, clocks, and throttling status.
 */

#include "src/gpu/inc/GpuTelemetry.hpp"

#include <array> // std::array

#include <fmt/core.h>

#include "src/gpu/inc/compat_nvml_detect.hpp"

namespace seeker {

namespace gpu {

namespace {

#if COMPAT_NVML_AVAILABLE

/// RAII wrapper for NVML initialization.
class NvmlSession {
public:
  NvmlSession() noexcept : initialized_(nvmlInit_v2() == NVML_SUCCESS) {}
  ~NvmlSession() {
    if (initialized_)
      nvmlShutdown();
  }

  [[nodiscard]] bool valid() const noexcept { return initialized_; }

  NvmlSession(const NvmlSession&) = delete;
  NvmlSession& operator=(const NvmlSession&) = delete;

private:
  bool initialized_;
};

/// Query telemetry for a device via NVML.
inline GpuTelemetry queryNvmlTelemetry(nvmlDevice_t device, int deviceIndex) noexcept {
  GpuTelemetry telemetry{};
  telemetry.deviceIndex = deviceIndex;

  // Device name
  std::array<char, 256> name{};
  if (nvmlDeviceGetName(device, name.data(), static_cast<unsigned int>(name.size())) ==
      NVML_SUCCESS) {
    telemetry.name = name.data();
  }

  // Temperature
  unsigned int temp = 0;
#if COMPAT_NVML_API_VERSION >= 13
  nvmlTemperature_t tempQuery{};
  tempQuery.version = nvmlTemperature_v1;
  tempQuery.sensorType = NVML_TEMPERATURE_GPU;
  if (nvmlDeviceGetTemperatureV(device, &tempQuery) == NVML_SUCCESS) {
    telemetry.temperatureC = tempQuery.temperature;
  }
#else
  if (nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS) {
    telemetry.temperatureC = static_cast<int>(temp);
  }
#endif
  if (nvmlDeviceGetTemperatureThreshold(device, NVML_TEMPERATURE_THRESHOLD_SLOWDOWN, &temp) ==
      NVML_SUCCESS) {
    telemetry.temperatureSlowdownC = static_cast<int>(temp);
  }
  if (nvmlDeviceGetTemperatureThreshold(device, NVML_TEMPERATURE_THRESHOLD_SHUTDOWN, &temp) ==
      NVML_SUCCESS) {
    telemetry.temperatureShutdownC = static_cast<int>(temp);
  }
  if (nvmlDeviceGetTemperatureThreshold(device, NVML_TEMPERATURE_THRESHOLD_MEM_MAX, &temp) ==
      NVML_SUCCESS) {
    telemetry.temperatureMemoryC = static_cast<int>(temp);
  }

  // Power
  unsigned int power = 0;
  if (nvmlDeviceGetPowerUsage(device, &power) == NVML_SUCCESS) {
    telemetry.powerMilliwatts = power;
  }
  if (nvmlDeviceGetEnforcedPowerLimit(device, &power) == NVML_SUCCESS) {
    telemetry.powerLimitMilliwatts = power;
  }
  if (nvmlDeviceGetPowerManagementDefaultLimit(device, &power) == NVML_SUCCESS) {
    telemetry.powerDefaultMilliwatts = power;
  }
  unsigned int minLimit = 0, maxLimit = 0;
  if (nvmlDeviceGetPowerManagementLimitConstraints(device, &minLimit, &maxLimit) == NVML_SUCCESS) {
    telemetry.powerMaxMilliwatts = maxLimit;
  }

  // Clocks
  unsigned int clock = 0;
  if (nvmlDeviceGetClockInfo(device, NVML_CLOCK_SM, &clock) == NVML_SUCCESS) {
    telemetry.smClockMHz = static_cast<int>(clock);
  }
  if (nvmlDeviceGetMaxClockInfo(device, NVML_CLOCK_SM, &clock) == NVML_SUCCESS) {
    telemetry.smClockMaxMHz = static_cast<int>(clock);
  }
  if (nvmlDeviceGetClockInfo(device, NVML_CLOCK_MEM, &clock) == NVML_SUCCESS) {
    telemetry.memClockMHz = static_cast<int>(clock);
  }
  if (nvmlDeviceGetMaxClockInfo(device, NVML_CLOCK_MEM, &clock) == NVML_SUCCESS) {
    telemetry.memClockMaxMHz = static_cast<int>(clock);
  }
  if (nvmlDeviceGetClockInfo(device, NVML_CLOCK_GRAPHICS, &clock) == NVML_SUCCESS) {
    telemetry.graphicsClockMHz = static_cast<int>(clock);
  }
  if (nvmlDeviceGetClockInfo(device, NVML_CLOCK_VIDEO, &clock) == NVML_SUCCESS) {
    telemetry.videoClockMHz = static_cast<int>(clock);
  }

  // Performance state
  nvmlPstates_t pstate{};
  if (nvmlDeviceGetPerformanceState(device, &pstate) == NVML_SUCCESS) {
    telemetry.perfState = static_cast<int>(pstate);
  }

  // Throttle reasons (NVML 13.0 renamed to "EventReasons"; compat header aliases for older)
  unsigned long long reasons = 0;
  if (nvmlDeviceGetCurrentClocksEventReasons(device, &reasons) == NVML_SUCCESS) {
    telemetry.throttleReasons.gpuIdle = (reasons & nvmlClocksEventReasonGpuIdle) != 0;
    telemetry.throttleReasons.applicationClocks =
        (reasons & nvmlClocksEventReasonApplicationsClocksSetting) != 0;
    telemetry.throttleReasons.swPowerCap = (reasons & nvmlClocksEventReasonSwPowerCap) != 0;
    telemetry.throttleReasons.hwSlowdown = (reasons & nvmlClocksThrottleReasonHwSlowdown) != 0;
    telemetry.throttleReasons.syncBoost = (reasons & nvmlClocksEventReasonSyncBoost) != 0;
    telemetry.throttleReasons.swThermal = (reasons & nvmlClocksEventReasonSwThermalSlowdown) != 0;
    telemetry.throttleReasons.hwThermal =
        (reasons & nvmlClocksThrottleReasonHwThermalSlowdown) != 0;
    telemetry.throttleReasons.hwPowerBrake =
        (reasons & nvmlClocksThrottleReasonHwPowerBrakeSlowdown) != 0;
    telemetry.throttleReasons.displayClocks =
        (reasons & nvmlClocksEventReasonDisplayClockSetting) != 0;
  }

  // Utilization
  nvmlUtilization_t util{};
  if (nvmlDeviceGetUtilizationRates(device, &util) == NVML_SUCCESS) {
    telemetry.gpuUtilization = static_cast<int>(util.gpu);
    telemetry.memoryUtilization = static_cast<int>(util.memory);
  }

  unsigned int encoderUtil = 0, decoderUtil = 0;
  unsigned int samplingPeriod = 0;
  if (nvmlDeviceGetEncoderUtilization(device, &encoderUtil, &samplingPeriod) == NVML_SUCCESS) {
    telemetry.encoderUtilization = static_cast<int>(encoderUtil);
  }
  if (nvmlDeviceGetDecoderUtilization(device, &decoderUtil, &samplingPeriod) == NVML_SUCCESS) {
    telemetry.decoderUtilization = static_cast<int>(decoderUtil);
  }

  // Fan speed
  unsigned int fanSpeed = 0;
  if (nvmlDeviceGetFanSpeed(device, &fanSpeed) == NVML_SUCCESS) {
    telemetry.fanSpeedPercent = static_cast<int>(fanSpeed);
  }

  return telemetry;
}

#endif // COMPAT_NVML_AVAILABLE

} // namespace

/* ----------------------------- ThrottleReasons ----------------------------- */

bool ThrottleReasons::isThrottling() const noexcept {
  return swPowerCap || hwSlowdown || swThermal || hwThermal || hwPowerBrake;
}

bool ThrottleReasons::isThermalThrottling() const noexcept { return swThermal || hwThermal; }

bool ThrottleReasons::isPowerThrottling() const noexcept { return swPowerCap || hwPowerBrake; }

std::string ThrottleReasons::toString() const {
  std::string result;
  auto add = [&](bool flag, const char* name) {
    if (flag) {
      if (!result.empty())
        result += ",";
      result += name;
    }
  };

  add(gpuIdle, "idle");
  add(applicationClocks, "app_clocks");
  add(swPowerCap, "sw_power");
  add(hwSlowdown, "hw_slowdown");
  add(syncBoost, "sync_boost");
  add(swThermal, "sw_thermal");
  add(hwThermal, "hw_thermal");
  add(hwPowerBrake, "power_brake");
  add(displayClocks, "display");

  return result.empty() ? "none" : result;
}

/* ----------------------------- GpuTelemetry ----------------------------- */

bool GpuTelemetry::isThrottling() const noexcept { return throttleReasons.isThrottling(); }

std::string GpuTelemetry::toString() const {
  return fmt::format("[GPU {}] {} - {}C, {:.1f}W, SM {}MHz, Mem {}MHz, P{}, util {}%, throttle: {}",
                     deviceIndex, name, temperatureC, static_cast<double>(powerMilliwatts) / 1000.0,
                     smClockMHz, memClockMHz, perfState, gpuUtilization,
                     throttleReasons.toString());
}

/* ----------------------------- API ----------------------------- */

GpuTelemetry getGpuTelemetry(int deviceIndex) noexcept {
  GpuTelemetry telemetry{};
  telemetry.deviceIndex = deviceIndex;

#if COMPAT_NVML_AVAILABLE
  NvmlSession session;
  if (!session.valid()) {
    return telemetry;
  }

  nvmlDevice_t device{};
  if (nvmlDeviceGetHandleByIndex_v2(static_cast<unsigned int>(deviceIndex), &device) !=
      NVML_SUCCESS) {
    return telemetry;
  }

  telemetry = queryNvmlTelemetry(device, deviceIndex);
#endif

  return telemetry;
}

std::vector<GpuTelemetry> getAllGpuTelemetry() noexcept {
  std::vector<GpuTelemetry> result;

#if COMPAT_NVML_AVAILABLE
  NvmlSession session;
  if (!session.valid()) {
    return result;
  }

  unsigned int count = 0;
  if (nvmlDeviceGetCount_v2(&count) != NVML_SUCCESS) {
    return result;
  }

  result.reserve(count);
  for (unsigned int i = 0; i < count; ++i) {
    nvmlDevice_t device{};
    if (nvmlDeviceGetHandleByIndex_v2(i, &device) == NVML_SUCCESS) {
      result.push_back(queryNvmlTelemetry(device, static_cast<int>(i)));
    }
  }
#endif

  return result;
}

} // namespace gpu

} // namespace seeker
