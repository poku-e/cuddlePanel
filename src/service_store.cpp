#include "service_store.h"

#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace cuddle {
namespace {

std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, delimiter)) {
        parts.push_back(item);
    }
    return parts;
}

ServiceActionResult capture_command(const std::vector<std::string>& args) {
    std::array<char, 256> buffer{};
    std::string output;
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return {false, "unable to execute command"};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return {false, "unable to execute command"};
    }

    if (pid == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(127);
    }

    close(pipe_fds[1]);
    ssize_t read_count = 0;
    while ((read_count = read(pipe_fds[0], buffer.data(), buffer.size())) > 0) {
        output.append(buffer.data(), static_cast<size_t>(read_count));
    }
    close(pipe_fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (output.empty()) {
        output = ok ? "command completed successfully" : "command failed";
    }
    return {ok, output};
}

}

ServiceStore::ServiceStore(std::string path) : path_(std::move(path)) {}

bool ServiceStore::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    services_.clear();

    std::ifstream file(path_);
    if (!file.good()) {
        return true;
    }

    std::string line;
    while (std::getline(file, line)) {
        auto parts = split(line, '\t');
        if (parts.size() != 3) {
            continue;
        }
        const std::string description = normalize_service_description(parts[2]);
        if (!valid_service_name(parts[0]) || !valid_service_unit(parts[1]) || !valid_service_description(description)) {
            continue;
        }
        services_.push_back(ServiceEntry{parts[0], parts[1], description});
    }
    return true;
}

bool ServiceStore::save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path());

    std::ofstream file(path_, std::ios::trunc);
    if (!file.good()) {
        return false;
    }

    for (const auto& service : services_) {
        file << service.name << '\t'
             << service.unit << '\t'
             << normalize_service_description(service.description) << '\n';
    }
    file.close();
    chmod(path_.c_str(), S_IRUSR | S_IWUSR);
    return true;
}

std::vector<ServiceEntry> ServiceStore::services() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return services_;
}

std::optional<ServiceEntry> ServiceStore::find_service(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& service : services_) {
        if (service.name == name) {
            return service;
        }
    }
    return std::nullopt;
}

bool ServiceStore::create_service(const std::string& name, const std::string& unit, const std::string& description) {
    const std::string normalized = normalize_service_description(description);
    if (!valid_service_name(name) || !valid_service_unit(unit) || !valid_service_description(normalized)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& service : services_) {
            if (service.name == name || service.unit == unit) {
                return false;
            }
        }
        services_.push_back(ServiceEntry{name, unit, normalized});
    }
    return save();
}

bool ServiceStore::update_service(const std::string& current_name,
                                  const std::string& new_name,
                                  const std::string& unit,
                                  const std::string& description) {
    const std::string normalized = normalize_service_description(description);
    if (!valid_service_name(current_name) || !valid_service_name(new_name) ||
        !valid_service_unit(unit) || !valid_service_description(normalized)) {
        return false;
    }

    bool updated = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& service : services_) {
            if (service.name == new_name && service.name != current_name) {
                return false;
            }
            if (service.unit == unit && service.name != current_name) {
                return false;
            }
        }
        for (auto& service : services_) {
            if (service.name != current_name) {
                continue;
            }
            service.name = new_name;
            service.unit = unit;
            service.description = normalized;
            updated = true;
            break;
        }
    }
    return updated && save();
}

bool valid_service_name(const std::string& name) {
    if (name.size() < 3 || name.size() > 64) {
        return false;
    }
    for (unsigned char c : name) {
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool valid_service_unit(const std::string& unit) {
    if (unit.size() < 8 || unit.size() > 128) {
        return false;
    }
    if (unit.rfind(".service") != unit.size() - 8) {
        return false;
    }
    for (unsigned char c : unit) {
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == '@')) {
            return false;
        }
    }
    return true;
}

std::string normalize_service_description(const std::string& description) {
    std::string normalized;
    normalized.reserve(description.size());
    bool previous_space = false;
    for (unsigned char c : description) {
        const bool space = std::isspace(c) != 0;
        if (space) {
            if (!previous_space && !normalized.empty()) {
                normalized += ' ';
            }
        } else {
            normalized += static_cast<char>(c);
        }
        previous_space = space;
    }
    while (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }
    return normalized;
}

bool valid_service_description(const std::string& description) {
    return description.size() <= 200;
}

ServiceStatus query_service_status(const std::string& unit) {
    if (!valid_service_unit(unit)) {
        return {"invalid", "invalid unit name"};
    }
    const auto result = capture_command({"/bin/systemctl", "is-active", unit});
    std::string state = result.output;
    while (!state.empty() && (state.back() == '\n' || state.back() == '\r')) {
        state.pop_back();
    }
    if (state.empty()) {
        state = result.ok ? "unknown" : "error";
    }
    return {state, result.output};
}

ServiceActionResult run_service_action(const std::string& unit, const std::string& action) {
    if (!valid_service_unit(unit)) {
        return {false, "invalid unit name"};
    }
    if (action != "start" && action != "stop" && action != "restart") {
        return {false, "invalid action"};
    }
    return capture_command({"/bin/systemctl", action, unit});
}

}
