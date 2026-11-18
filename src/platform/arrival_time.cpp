// Converts the device clock to destination local time for flight arrival art.

#include "arrival_time.h"

#include <algorithm>
#include <array>

#ifdef _WIN32
#include <cwchar>
#include <windows.h>
#else
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#endif

#if defined(__linux__) && !USE_DESKTOP
#include <fcntl.h>
#include <fstream>
#include <linux/rtc.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace platform {
namespace {

struct DestinationZone {
    std::string_view city;
#ifdef _WIN32
    const wchar_t* zone;
#else
    const char* zone;
#endif
};

constexpr std::array zones{
#ifdef _WIN32
    DestinationZone{"silicon-valley", L"Pacific Standard Time"},
    DestinationZone{"toronto", L"Eastern Standard Time"},
    DestinationZone{"london", L"GMT Standard Time"},
    DestinationZone{"new-york", L"Eastern Standard Time"},
    DestinationZone{"beijing", L"China Standard Time"},
    DestinationZone{"montreal", L"Eastern Standard Time"},
    DestinationZone{"paris", L"Romance Standard Time"},
    DestinationZone{"shenzhen", L"China Standard Time"},
#else
    DestinationZone{"silicon-valley", "America/Los_Angeles"},
    DestinationZone{"toronto", "America/Toronto"},
    DestinationZone{"london", "Europe/London"},
    DestinationZone{"new-york", "America/New_York"},
    DestinationZone{"beijing", "Asia/Shanghai"},
    DestinationZone{"montreal", "America/Toronto"},
    DestinationZone{"paris", "Europe/Paris"},
    DestinationZone{"shenzhen", "Asia/Shanghai"},
#endif
};

#if defined(__linux__) && !USE_DESKTOP
// Read a usable UTC time from the Linux RTC, leaving local-time RTCs to the system clock.
std::optional<std::time_t> hardware_clock() {
    // Linux already interprets the hardware clock for systems configured with a local-time RTC.
    std::ifstream adjtime("/etc/adjtime");
    std::string line;
    for (int i = 0; i < 3 && std::getline(adjtime, line); ++i) {
        if (i == 2 && line == "LOCAL") return std::nullopt;
    }

    const int fd = open("/dev/rtc0", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return std::nullopt;
    rtc_time rtc{};
    const bool read = ioctl(fd, RTC_RD_TIME, &rtc) == 0;
    close(fd);
    if (!read || rtc.tm_year < 100) return std::nullopt;

    std::tm utc{};
    utc.tm_year = rtc.tm_year;
    utc.tm_mon = rtc.tm_mon;
    utc.tm_mday = rtc.tm_mday;
    utc.tm_hour = rtc.tm_hour;
    utc.tm_min = rtc.tm_min;
    utc.tm_sec = rtc.tm_sec;
    const auto instant = timegm(&utc);
    if (instant == static_cast<std::time_t>(-1)) return std::nullopt;
    return instant;
}
#endif

} // namespace

// Prefer the device RTC when available and fall back to the operating system's wall clock.
std::time_t arrival_clock() {
#if defined(__linux__) && !USE_DESKTOP
    if (const auto instant = hardware_clock()) return *instant;
#endif
    return std::time(nullptr);
}

// Resolve the city's configured time zone and apply its rules for the given instant.
std::optional<int> destination_minute(std::string_view city, std::time_t instant) {
    const auto zone = std::find_if(zones.begin(), zones.end(), [city](const auto& entry) {
        return entry.city == city;
    });
    if (zone == zones.end() || instant == static_cast<std::time_t>(-1)) return std::nullopt;

#ifdef _WIN32
    std::tm utc_tm{};
    if (gmtime_s(&utc_tm, &instant) != 0) return std::nullopt;
    SYSTEMTIME utc{};
    utc.wYear = static_cast<WORD>(utc_tm.tm_year + 1900);
    utc.wMonth = static_cast<WORD>(utc_tm.tm_mon + 1);
    utc.wDay = static_cast<WORD>(utc_tm.tm_mday);
    utc.wHour = static_cast<WORD>(utc_tm.tm_hour);
    utc.wMinute = static_cast<WORD>(utc_tm.tm_min);
    utc.wSecond = static_cast<WORD>(utc_tm.tm_sec);

    DYNAMIC_TIME_ZONE_INFORMATION info{};
    for (DWORD index = 0; EnumDynamicTimeZoneInformation(index, &info) == ERROR_SUCCESS; ++index) {
        if (std::wcscmp(info.TimeZoneKeyName, zone->zone) != 0) continue;
        SYSTEMTIME local{};
        if (!SystemTimeToTzSpecificLocalTimeEx(&info, &utc, &local)) return std::nullopt;
        return local.wHour * 60 + local.wMinute;
    }
    return std::nullopt;
#else
    const auto path = std::filesystem::path("/usr/share/zoneinfo") / zone->zone;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return std::nullopt;
    const auto target = ":" + path.string();

    // The game reads destination time on the UI thread. Serialize conversions and restore TZ
    // before returning to the event loop, so logs and screenshots retain the device time zone.
    static std::mutex time_zone_mutex;
    const std::lock_guard<std::mutex> lock(time_zone_mutex);
    const char* current = std::getenv("TZ");
    const auto previous = current ? std::optional<std::string>{current} : std::nullopt;
    if (setenv("TZ", target.c_str(), 1) != 0) return std::nullopt;
    tzset();
    std::tm local{};
    const bool converted = localtime_r(&instant, &local) != nullptr;
    if (previous) setenv("TZ", previous->c_str(), 1);
    else unsetenv("TZ");
    tzset();
    if (!converted) return std::nullopt;
    return local.tm_hour * 60 + local.tm_min;
#endif
}

} // namespace platform
