#include "nginx_store.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
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

std::optional<std::string> read_text_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return std::nullopt;
    }
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

bool write_text_file(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.good()) {
        return false;
    }
    file << content;
    file.close();
    chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    return true;
}

NginxActionResult capture_command(const std::vector<std::string>& args) {
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

std::string nginx_binary_path() {
    const char* configured = std::getenv("CUDDLEPANEL_NGINX_BIN");
    return configured && *configured ? configured : "/usr/sbin/nginx";
}

std::string nginx_reload_service() {
    const char* configured = std::getenv("CUDDLEPANEL_NGINX_RELOAD_SERVICE");
    return configured && *configured ? configured : "nginx";
}

bool path_exists_or_link(const std::string& path) {
    const auto status = std::filesystem::symlink_status(path);
    return status.type() != std::filesystem::file_type::not_found;
}

std::string site_name_from_filename(const std::string& filename) {
    if (filename.size() > 5 && filename.rfind(".conf") == filename.size() - 5) {
        return filename.substr(0, filename.size() - 5);
    }
    return filename;
}

}

NginxStore::NginxStore(std::string db_path, std::string available_dir, std::string enabled_dir)
    : db_path_(std::move(db_path)),
      available_dir_(std::move(available_dir)),
      enabled_dir_(std::move(enabled_dir)) {}

bool NginxStore::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    sites_.clear();

    std::ifstream file(db_path_);
    if (!file.good()) {
        return true;
    }

    std::string line;
    while (std::getline(file, line)) {
        auto parts = split(line, '\t');
        if (parts.size() != 2 && parts.size() != 3) {
            continue;
        }
        const std::string description = normalize_nginx_description(parts.size() == 3 ? parts[2] : "");
        if (!valid_nginx_site_name(parts[0]) || !valid_nginx_filename(parts[1]) || !valid_nginx_description(description)) {
            continue;
        }
        sites_.push_back(NginxSiteEntry{parts[0], parts[1], description});
    }
    return true;
}

bool NginxStore::save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::create_directories(std::filesystem::path(db_path_).parent_path());

    std::ofstream file(db_path_, std::ios::trunc);
    if (!file.good()) {
        return false;
    }
    for (const auto& site : sites_) {
        file << site.name << '\t'
             << site.filename << '\t'
             << normalize_nginx_description(site.description) << '\n';
    }
    file.close();
    chmod(db_path_.c_str(), S_IRUSR | S_IWUSR);
    return true;
}

std::vector<NginxSiteEntry> NginxStore::sites() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return discover_sites_locked();
}

std::optional<NginxSiteEntry> NginxStore::find_site(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return resolve_site_locked(name);
}

std::optional<NginxSiteRecord> NginxStore::read_site(const std::string& name) const {
    std::optional<NginxSiteEntry> entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entry = resolve_site_locked(name);
    }
    if (!entry) {
        return std::nullopt;
    }

    auto available_path = site_available_path(entry->filename);
    auto enabled_path = site_enabled_path(entry->filename);
    if (!available_path || !enabled_path) {
        return std::nullopt;
    }

    auto content = read_text_file(*available_path);
    if (!content) {
        return std::nullopt;
    }

    return NginxSiteRecord{
        entry->name,
        entry->filename,
        entry->description,
        *content,
        path_exists_or_link(*enabled_path)
    };
}

bool NginxStore::create_site(const std::string& name,
                             const std::string& filename,
                             const std::string& description,
                             const std::string& content) {
    const std::string normalized = normalize_nginx_description(description);
    const std::string chosen_name = name.empty() ? site_name_from_filename(filename) : name;
    if (!valid_nginx_site_name(chosen_name) || !valid_nginx_filename(filename) ||
        !valid_nginx_description(normalized) || !valid_nginx_content(content)) {
        return false;
    }

    auto available_path = site_available_path(filename);
    if (!available_path) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& site : sites_) {
            if (site.name == chosen_name || site.filename == filename) {
                return false;
            }
        }
        if (std::filesystem::exists(*available_path)) {
            return false;
        }
        std::filesystem::create_directories(available_dir_);
        std::filesystem::create_directories(enabled_dir_);
        if (!write_text_file(*available_path, content)) {
            return false;
        }
        sites_.push_back(NginxSiteEntry{chosen_name, filename, normalized});
    }
    return save();
}

bool NginxStore::update_site(const std::string& current_name,
                             const std::string& new_name,
                             const std::string& filename,
                             const std::string& description,
                             const std::string& content) {
    const std::string normalized = normalize_nginx_description(description);
    const std::string chosen_name = new_name.empty() ? site_name_from_filename(filename) : new_name;
    if (!valid_nginx_site_name(chosen_name) ||
        !valid_nginx_filename(filename) || !valid_nginx_description(normalized) ||
        !valid_nginx_content(content)) {
        return false;
    }

    std::optional<NginxSiteEntry> current;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current = resolve_site_locked(current_name);
        if (!current) {
            return false;
        }
        for (const auto& site : sites_) {
            if (site.filename != current->filename && site.filename == filename) {
                return false;
            }
            if (site.filename != current->filename && site.name == chosen_name) {
                return false;
            }
        }
    }

    const std::string old_filename = current->filename;
    bool was_enabled = false;
    {
        auto old_enabled = site_enabled_path(old_filename);
        was_enabled = old_enabled && path_exists_or_link(*old_enabled);
    }

    auto new_available = site_available_path(filename);
    auto new_enabled = site_enabled_path(filename);
    auto old_available = site_available_path(old_filename);
    auto old_enabled = site_enabled_path(old_filename);
    if (!new_available || !new_enabled || !old_available || !old_enabled) {
        return false;
    }

    std::filesystem::create_directories(available_dir_);
    std::filesystem::create_directories(enabled_dir_);
    if (old_filename != filename && std::filesystem::exists(*new_available)) {
        return false;
    }
    if (old_filename != filename && std::filesystem::exists(*old_available)) {
        std::filesystem::rename(*old_available, *new_available);
    }
    if (!write_text_file(*new_available, content)) {
        return false;
    }
    if (old_filename != filename && path_exists_or_link(*old_enabled)) {
        std::filesystem::remove(*old_enabled);
    }
    if (was_enabled) {
        if (path_exists_or_link(*new_enabled)) {
            std::filesystem::remove(*new_enabled);
        }
        std::filesystem::create_symlink(std::filesystem::absolute(*new_available), *new_enabled);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        bool metadata_updated = false;
        for (auto& site : sites_) {
            if (site.filename != old_filename) {
                continue;
            }
            site.name = chosen_name;
            site.filename = filename;
            site.description = normalized;
            metadata_updated = true;
            break;
        }
        if (!metadata_updated) {
            sites_.push_back(NginxSiteEntry{chosen_name, filename, normalized});
        }
    }

    return save();
}

bool NginxStore::set_enabled(const std::string& name, bool enabled) {
    std::optional<NginxSiteEntry> entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entry = resolve_site_locked(name);
    }
    if (!entry) {
        return false;
    }

    auto available_path = site_available_path(entry->filename);
    auto enabled_path = site_enabled_path(entry->filename);
    if (!available_path || !enabled_path || !std::filesystem::exists(*available_path)) {
        return false;
    }

    std::filesystem::create_directories(enabled_dir_);
    if (enabled) {
        if (path_exists_or_link(*enabled_path)) {
            return true;
        }
        std::filesystem::create_symlink(std::filesystem::absolute(*available_path), *enabled_path);
        return true;
    }

    if (path_exists_or_link(*enabled_path)) {
        std::filesystem::remove(*enabled_path);
    }
    return true;
}

const std::string& NginxStore::available_dir() const {
    return available_dir_;
}

const std::string& NginxStore::enabled_dir() const {
    return enabled_dir_;
}

std::optional<std::string> NginxStore::site_available_path(const std::string& filename) const {
    if (!valid_nginx_filename(filename)) {
        return std::nullopt;
    }
    return (std::filesystem::path(available_dir_) / filename).string();
}

std::optional<std::string> NginxStore::site_enabled_path(const std::string& filename) const {
    if (!valid_nginx_filename(filename)) {
        return std::nullopt;
    }
    return (std::filesystem::path(enabled_dir_) / filename).string();
}

std::optional<NginxSiteEntry> NginxStore::metadata_for_filename_locked(const std::string& filename) const {
    for (const auto& site : sites_) {
        if (site.filename == filename) {
            return site;
        }
    }
    return std::nullopt;
}

std::optional<NginxSiteEntry> NginxStore::resolve_site_locked(const std::string& identifier) const {
    if (identifier.empty()) {
        return std::nullopt;
    }

    auto build_entry_from_filename = [this](const std::string& filename) -> std::optional<NginxSiteEntry> {
        auto available_path = site_available_path(filename);
        if (!available_path || !std::filesystem::exists(*available_path)) {
            return std::nullopt;
        }
        if (auto metadata = metadata_for_filename_locked(filename)) {
            return metadata;
        }
        return NginxSiteEntry{site_name_from_filename(filename), filename, ""};
    };

    if (valid_nginx_filename(identifier)) {
        if (auto entry = build_entry_from_filename(identifier)) {
            return entry;
        }
    }

    for (const auto& site : sites_) {
        if (site.name != identifier) {
            continue;
        }
        auto available_path = site_available_path(site.filename);
        if (available_path && std::filesystem::exists(*available_path)) {
            return site;
        }
    }

    const std::string candidate_filename = identifier + ".conf";
    if (valid_nginx_filename(candidate_filename)) {
        return build_entry_from_filename(candidate_filename);
    }
    return std::nullopt;
}

std::vector<NginxSiteEntry> NginxStore::discover_sites_locked() const {
    std::vector<NginxSiteEntry> discovered;
    std::error_code ec;
    if (!std::filesystem::exists(available_dir_, ec) || ec) {
        return discovered;
    }

    std::filesystem::directory_iterator end;
    for (std::filesystem::directory_iterator it(available_dir_, ec); !ec && it != end; it.increment(ec)) {
        std::error_code status_ec;
        const auto type = it->symlink_status(status_ec).type();
        if (status_ec || (type != std::filesystem::file_type::regular && type != std::filesystem::file_type::symlink)) {
            continue;
        }

        const std::string filename = it->path().filename().string();
        if (!valid_nginx_filename(filename)) {
            continue;
        }

        if (auto metadata = metadata_for_filename_locked(filename)) {
            discovered.push_back(*metadata);
        } else {
            discovered.push_back(NginxSiteEntry{site_name_from_filename(filename), filename, ""});
        }
    }

    std::sort(discovered.begin(), discovered.end(), [](const NginxSiteEntry& a, const NginxSiteEntry& b) {
        if (a.filename == b.filename) {
            return a.name < b.name;
        }
        return a.filename < b.filename;
    });
    return discovered;
}

bool valid_nginx_site_name(const std::string& name) {
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

bool valid_nginx_filename(const std::string& filename) {
    if (filename.empty() || filename.size() > 128) {
        return false;
    }
    for (unsigned char c : filename) {
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-')) {
            return false;
        }
    }
    if (filename == "." || filename == "..") {
        return false;
    }
    return filename.find("..") == std::string::npos;
}

std::string normalize_nginx_description(const std::string& description) {
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

bool valid_nginx_description(const std::string& description) {
    return description.size() <= 200;
}

bool valid_nginx_content(const std::string& content) {
    if (content.size() > 256 * 1024) {
        return false;
    }
    return content.find('\0') == std::string::npos;
}

NginxActionResult nginx_test_config() {
    return capture_command({nginx_binary_path(), "-t"});
}

NginxActionResult nginx_reload() {
    return capture_command({"/bin/systemctl", "reload", nginx_reload_service()});
}

}
