#include "log.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace cuddle {
namespace {

std::mutex log_mutex;
std::ofstream log_file;
bool log_file_failed = false;

const char* level_text(LogLevel level) {
    switch (level) {
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

std::string timestamp_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc_time{};
#if defined(_WIN32)
    gmtime_s(&utc_time, &seconds);
#else
    gmtime_r(&seconds, &utc_time);
#endif
    std::ostringstream out;
    out << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

void write_line_unlocked(const std::string& line, bool echo_stderr) {
    if (echo_stderr) {
        std::cerr << line << std::endl;
    } else {
        std::cout << line << std::endl;
    }

    if (log_file.is_open()) {
        log_file << line << '\n';
        log_file.flush();
        return;
    }

    if (!log_file_failed) {
        std::cerr << timestamp_utc() << " [WARN] file logging is unavailable" << std::endl;
        log_file_failed = true;
    }
}

}

void init_logging(const std::string& path) {
    std::lock_guard<std::mutex> lock(log_mutex);
    log_file_failed = false;
    if (log_file.is_open()) {
        log_file.close();
    }

    std::error_code ec;
    const auto log_path = std::filesystem::path(path);
    if (log_path.has_parent_path()) {
        std::filesystem::create_directories(log_path.parent_path(), ec);
    }

    log_file.open(path, std::ios::app);
    if (!log_file.good()) {
        log_file.close();
        log_file_failed = true;
        std::cerr << timestamp_utc() << " [WARN] unable to open log file at " << path << std::endl;
        return;
    }
}

void log_message(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    const std::string line = timestamp_utc() + " [" + level_text(level) + "] " + message;
    write_line_unlocked(line, level != LogLevel::Info);
}

void log_errno(LogLevel level, const std::string& context, int error_number) {
    std::ostringstream out;
    out << context << ": " << std::strerror(error_number) << " (errno=" << error_number << ")";
    log_message(level, out.str());
}

}
