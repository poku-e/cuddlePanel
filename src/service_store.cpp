#include "service_store.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
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

std::string systemctl_binary() {
    const char* configured = std::getenv("CUDDLEPANEL_SYSTEMCTL_BIN");
    return configured && *configured ? configured : "/bin/systemctl";
}

std::string trim_copy(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::map<std::string, std::string> parse_key_value_output(const std::string& output) {
    std::map<std::string, std::string> values;
    std::stringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        values[line.substr(0, pos)] = line.substr(pos + 1);
    }
    return values;
}

std::string service_display_name(const std::string& unit) {
    if (unit.size() > 8 && unit.rfind(".service") == unit.size() - 8) {
        return unit.substr(0, unit.size() - 8);
    }
    return unit;
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

std::optional<DiscoveredService> discover_service(const std::string& unit) {
    if (!valid_service_unit(unit)) {
        return std::nullopt;
    }
    const auto result = capture_command({
        systemctl_binary(),
        "show",
        unit,
        "--no-pager",
        "--property=Id,Description,LoadState,ActiveState,SubState,UnitFileState,FragmentPath"
    });
    const auto values = parse_key_value_output(result.output);
    const auto id_it = values.find("Id");
    const std::string discovered_unit = id_it == values.end() ? unit : id_it->second;
    if (!valid_service_unit(discovered_unit)) {
        return std::nullopt;
    }
    return DiscoveredService{
        service_display_name(discovered_unit),
        discovered_unit,
        values.count("Description") ? values.at("Description") : "",
        values.count("ActiveState") ? values.at("ActiveState") : "unknown",
        values.count("SubState") ? values.at("SubState") : "",
        values.count("UnitFileState") ? values.at("UnitFileState") : "unknown",
        values.count("LoadState") ? values.at("LoadState") : "unknown",
        values.count("FragmentPath") ? values.at("FragmentPath") : ""
    };
}

std::vector<DiscoveredService> discover_services() {
    const auto result = capture_command({
        systemctl_binary(),
        "list-unit-files",
        "--type=service",
        "--no-legend",
        "--no-pager",
        "--plain"
    });
    std::vector<DiscoveredService> services;
    std::stringstream stream(result.output);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim_copy(line);
        if (line.empty()) {
            continue;
        }
        std::stringstream line_stream(line);
        std::string unit;
        std::string unit_file_state;
        line_stream >> unit >> unit_file_state;
        if (!valid_service_unit(unit)) {
            continue;
        }
        auto detail = discover_service(unit);
        if (!detail) {
            continue;
        }
        if (detail->unit_file_state.empty()) {
            detail->unit_file_state = unit_file_state;
        }
        services.push_back(*detail);
    }
    std::sort(services.begin(), services.end(), [](const DiscoveredService& left, const DiscoveredService& right) {
        if (left.active_state != right.active_state) {
            if (left.active_state == "active") return true;
            if (right.active_state == "active") return false;
        }
        return left.unit < right.unit;
    });
    return services;
}

ServiceStatus query_service_status(const std::string& unit) {
    if (!valid_service_unit(unit)) {
        return {"invalid", "invalid unit name"};
    }
    const auto result = capture_command({systemctl_binary(), "is-active", unit});
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
    if (action != "start" && action != "stop" && action != "restart" &&
        action != "enable" && action != "disable") {
        return {false, "invalid action"};
    }
    return capture_command({systemctl_binary(), action, unit});
}

}
