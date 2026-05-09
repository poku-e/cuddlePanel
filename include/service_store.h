#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cuddle {

struct ServiceEntry {
    std::string name;
    std::string unit;
    std::string description;
};

struct ServiceStatus {
    std::string state;
    std::string detail;
};

struct ServiceActionResult {
    bool ok = false;
    std::string output;
};

class ServiceStore {
public:
    explicit ServiceStore(std::string path);

    bool load();
    bool save() const;
    std::vector<ServiceEntry> services() const;
    std::optional<ServiceEntry> find_service(const std::string& name) const;
    bool create_service(const std::string& name, const std::string& unit, const std::string& description);
    bool update_service(const std::string& current_name,
                        const std::string& new_name,
                        const std::string& unit,
                        const std::string& description);

private:
    std::string path_;
    mutable std::mutex mutex_;
    std::vector<ServiceEntry> services_;
};

bool valid_service_name(const std::string& name);
bool valid_service_unit(const std::string& unit);
std::string normalize_service_description(const std::string& description);
bool valid_service_description(const std::string& description);

ServiceStatus query_service_status(const std::string& unit);
ServiceActionResult run_service_action(const std::string& unit, const std::string& action);

}
