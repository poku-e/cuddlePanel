#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cuddle {

struct NginxSiteEntry {
    std::string name;
    std::string filename;
    std::string description;
};

struct NginxSiteRecord {
    std::string name;
    std::string filename;
    std::string description;
    std::string content;
    bool enabled = false;
};

struct NginxActionResult {
    bool ok = false;
    std::string output;
};

class NginxStore {
public:
    NginxStore(std::string db_path, std::string available_dir, std::string enabled_dir);

    bool load();
    bool save() const;
    std::vector<NginxSiteEntry> sites() const;
    std::optional<NginxSiteEntry> find_site(const std::string& name) const;
    std::optional<NginxSiteRecord> read_site(const std::string& name) const;
    bool create_site(const std::string& name,
                     const std::string& filename,
                     const std::string& description,
                     const std::string& content);
    bool update_site(const std::string& current_name,
                     const std::string& new_name,
                     const std::string& filename,
                     const std::string& description,
                     const std::string& content);
    bool set_enabled(const std::string& name, bool enabled);

    const std::string& available_dir() const;
    const std::string& enabled_dir() const;

private:
    std::optional<std::string> site_available_path(const std::string& filename) const;
    std::optional<std::string> site_enabled_path(const std::string& filename) const;
    std::optional<NginxSiteEntry> metadata_for_filename_locked(const std::string& filename) const;
    std::optional<NginxSiteEntry> resolve_site_locked(const std::string& identifier) const;
    std::vector<NginxSiteEntry> discover_sites_locked() const;

    std::string db_path_;
    std::string available_dir_;
    std::string enabled_dir_;
    mutable std::mutex mutex_;
    std::vector<NginxSiteEntry> sites_;
};

bool valid_nginx_site_name(const std::string& name);
bool valid_nginx_filename(const std::string& filename);
std::string normalize_nginx_description(const std::string& description);
bool valid_nginx_description(const std::string& description);
bool valid_nginx_content(const std::string& content);
NginxActionResult nginx_test_config();
NginxActionResult nginx_reload();

}
