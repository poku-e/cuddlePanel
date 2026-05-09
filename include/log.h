#pragma once

#include <string>

namespace cuddle {

enum class LogLevel {
    Info,
    Warning,
    Error,
};

void init_logging(const std::string& path = "data/server.log");
void log_message(LogLevel level, const std::string& message);
void log_errno(LogLevel level, const std::string& context, int error_number);

}
