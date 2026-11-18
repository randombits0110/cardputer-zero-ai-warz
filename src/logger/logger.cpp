/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
 
// Formats log prefixes and writes severity-filtered messages to standard output.

#include "logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#ifdef _WIN32
#  include <io.h>
#  include <process.h>
#else
#  include <unistd.h>
#endif


#ifdef _WIN32
#  ifndef STDOUT_FILENO
#    define STDOUT_FILENO 1
#  endif

#  ifndef isatty
#    define isatty _isatty
#  endif
#endif

namespace logger {

constexpr const char* RESET   = "\033[0m";
constexpr const char* GRAY    = "\033[90m";
constexpr const char* CYAN    = "\033[36m";
constexpr const char* GREEN   = "\033[32m";
constexpr const char* YELLOW  = "\033[33m";
constexpr const char* RED     = "\033[31m";
constexpr const char* MAGENTA = "\033[35m";

// Choose the ANSI escape sequence for a message's severity.
const char* level_to_color(LogLevel level) {
    switch (level){
        case LogLevel::TRACE: return GRAY;
        case LogLevel::DEBUG: return CYAN;
        case LogLevel::INFO : return GREEN;
        case LogLevel::WARN : return YELLOW;
        case LogLevel::ERROR: return RED;
        case LogLevel::FATAL: return MAGENTA;
        default             : return RESET;
    }
}

// Choose the one-character severity marker used in log prefixes.
const char* level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "T";
        case LogLevel::DEBUG: return "D";
        case LogLevel::INFO : return "I";
        case LogLevel::WARN : return "W";
        case LogLevel::ERROR: return "E";
        case LogLevel::FATAL: return "F";
        default             : return "?";
    }
}

// Use TERM to recognize terminal families that support color.
bool term_supports_color() {
    const char* term = getenv("TERM");
    if (!term) return false;

    return
        strstr(term, "xterm") ||
        strstr(term, "color") ||
        strstr(term, "ansi")  ||
        strstr(term, "screen")||
        strstr(term, "tmux")  ||
        strstr(term, "rxvt");
}

// Return the last path component, recognizing both Unix and Windows separators.
const char* base_name(const char* path) {
    if (!path || path[0] == '\0') {
        return nullptr;
    }

    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    const char* sep = slash;
    if (backslash && (!sep || backslash > sep)) {
        sep = backslash;
    }
    return sep ? sep + 1 : path;
}

// Format the system's current local time for a log prefix.
std::string current_timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm_now{};
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif


    char buffer[32];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%d-%d-%d %02d:%02d:%02d",
                  tm_now.tm_year + 1900,
                  tm_now.tm_mon + 1,
                  tm_now.tm_mday,
                  tm_now.tm_hour,
                  tm_now.tm_min,
                  tm_now.tm_sec);
    return buffer;
}

// Combine severity with any configured tag, timestamp, and source location.
std::string make_prefix(LogLevel level,
                        const std::string& tag,
                        bool timestamp_enabled,
                        const char* file,
                        int line) {
    std::string prefix;
    if (!tag.empty()) {
        prefix += fmt::format("[{}]", tag);
    }

    prefix += fmt::format("[{}]", level_to_string(level));

    if (timestamp_enabled) {
        prefix += fmt::format("[{}]", current_timestamp());
    }

    const char* name = base_name(file);
    if (name && line > 0) {
        prefix += fmt::format("[{}:{}]", name, line);
    }

    return prefix;
}

// Keep a startup hook for callers. The logger's defaults are initialized statically.
void Logger::init(){
}

// Update the runtime severity threshold independently of the build setting.
void Logger::set_level(LogLevel level) {
    level_ = level;
}

// Return the threshold currently used by the log sink.
LogLevel Logger::level() {
    return level_;
}

// Copy the supplied tag so the caller can release its storage.
void Logger::set_tag(const char* tag) {
    tag_ = tag ? tag : "";
}

// Expose the current tag using the logger's own string storage.
const char* Logger::tag() {
    return tag_.c_str();
}

// Store the requested color policy for subsequent log messages.
void Logger::set_color_mode(ColorMode mode) {
    color_mode_ = mode;
}

// Store whether subsequent log prefixes should include the time.
void Logger::set_timestamp_enabled(bool enabled) {
    timestamp_enabled_ = enabled;
}

// Return the current timestamp setting.
bool Logger::timestamp_enabled() {
    return timestamp_enabled_;
}

// Accept messages at or above the runtime threshold unless logging is off.
bool Logger::should_log(LogLevel level) {
    return level >= level_ && level_ != LogLevel::OFF;
}

// Honor explicit color settings, then check NO_COLOR and the terminal in automatic mode.
bool Logger::should_use_color() {
    switch (color_mode_) {
        case ColorMode::DISABLE: return false;
        case ColorMode::ENABLE:  return true;
        case ColorMode::AUTO:
        default:
            break;
    }

    if ( getenv("NO_COLOR") ) return false;
    // must be a tty
    if ( isatty(STDOUT_FILENO) == 0 ) return false;
    return term_supports_color();
}

// Write an accepted message on its own line and reset terminal color after colored output.
void Logger::log(LogLevel level,
                 const std::string& msg,
                 const char* file,
                 int line) {
    if (!should_log(level)) return;

    std::string prefix = make_prefix(level, tag_, timestamp_enabled_, file, line);
    
    if (should_use_color()) {
        printf("%s%s %s%s\n",
               level_to_color(level),
               prefix.c_str(),
               msg.c_str(),
               RESET);
    } else {
        printf("%s %s\n",
               prefix.c_str(),
               msg.c_str());
    }
}

} // namespace logger
