#include "codex_chat.h"

#include "codex_runner.h"
#include "log.h"

#include <cerrno>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <grp.h>
#include <pty.h>
#include <signal.h>
#include <sodium.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <vector>

namespace cuddle {
namespace {

constexpr size_t kConversationBufferLimit = 512 * 1024;
constexpr int kSessionIndexPollIterations = 20;
constexpr useconds_t kSessionIndexPollSleepMicros = 100000;

std::int64_t unix_now() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

std::string random_id() {
    std::array<unsigned char, 16> bytes{};
    randombytes_buf(bytes.data(), bytes.size());
    std::string out(bytes.size() * 2, '\0');
    sodium_bin2hex(out.data(), out.size() + 1, bytes.data(), bytes.size());
    return out;
}

std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, delimiter)) {
        parts.push_back(item);
    }
    return parts;
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

bool valid_absolute_path(const std::string& value, size_t max_length = 512) {
    return !value.empty() &&
           value.size() <= max_length &&
           value.front() == '/' &&
           value.find('\0') == std::string::npos;
}

bool valid_title(const std::string& value) {
    return !value.empty() && value.size() <= 120 && value.find('\0') == std::string::npos;
}

bool valid_message(const std::string& value) {
    return !value.empty() && value.size() <= 16000 && value.find('\0') == std::string::npos;
}

void normalize_codex_term_env() {
    const char* term = std::getenv("TERM");
    if (term == nullptr || term[0] == '\0' || std::strcmp(term, "dumb") == 0) {
        setenv("TERM", "xterm-256color", 1);
    }
}

std::string derive_project_name(const std::string& root) {
    const auto path = std::filesystem::path(root);
    const auto name = path.filename().string();
    return name.empty() ? root : name;
}

bool ensure_file_permissions(const std::string& path) {
    return chmod(path.c_str(), S_IRUSR | S_IWUSR) == 0;
}

bool is_git_repo_root(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_directory(std::filesystem::path(path) / ".git", ec);
}

std::string codex_home_dir() {
    const char* override = std::getenv("CODEX_HOME");
    if (override != nullptr && override[0] != '\0') {
        return override;
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') {
        return std::string(home) + "/.codex";
    }
    return "/root/.codex";
}

std::string codex_session_index_path() {
    return codex_home_dir() + "/session_index.jsonl";
}

std::vector<std::string> read_session_index_ids() {
    std::vector<std::string> ids;
    std::ifstream file(codex_session_index_path());
    if (!file.good()) {
        return ids;
    }

    std::string line;
    while (std::getline(file, line)) {
        const auto token = std::string("\"id\":\"");
        const auto start = line.find(token);
        if (start == std::string::npos) {
            continue;
        }
        const auto id_start = start + token.size();
        const auto id_end = line.find('"', id_start);
        if (id_end == std::string::npos || id_end <= id_start) {
            continue;
        }
        ids.push_back(line.substr(id_start, id_end - id_start));
    }
    return ids;
}

std::optional<std::string> detect_new_session_id(const std::vector<std::string>& before_ids) {
    for (int i = 0; i < kSessionIndexPollIterations; ++i) {
        const auto after_ids = read_session_index_ids();
        for (const auto& id : after_ids) {
            if (std::find(before_ids.begin(), before_ids.end(), id) == before_ids.end()) {
                return id;
            }
        }
        usleep(kSessionIndexPollSleepMicros);
    }
    return std::nullopt;
}

std::string sanitize_detail(const std::string& detail) {
    std::string out;
    out.reserve(detail.size());
    for (char c : detail) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += ' '; break;
            default: out += c; break;
        }
    }
    return out;
}

}

CodexProjectStore::CodexProjectStore(std::string path) : path_(std::move(path)) {}

bool CodexProjectStore::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    projects_.clear();

    std::ifstream file(path_);
    if (!file.good()) {
        return true;
    }

    std::string line;
    while (std::getline(file, line)) {
        const auto parts = split(line, '\t');
        if (parts.size() != 3) {
            continue;
        }
        CodexProject project;
        project.id = parts[0];
        project.name = parts[1];
        project.root = parts[2];
        if (!project.id.empty() && !project.name.empty() && valid_absolute_path(project.root)) {
            projects_.push_back(project);
        }
    }
    return true;
}

bool CodexProjectStore::save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path());
    std::ofstream file(path_, std::ios::trunc);
    if (!file.good()) {
        return false;
    }
    for (const auto& project : projects_) {
        file << project.id << '\t' << project.name << '\t' << project.root << '\n';
    }
    file.close();
    return ensure_file_permissions(path_);
}

std::vector<CodexProject> CodexProjectStore::projects() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return projects_;
}

std::optional<CodexProject> CodexProjectStore::find_project(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& project : projects_) {
        if (project.id == id) {
            return project;
        }
    }
    return std::nullopt;
}

std::optional<CodexProject> CodexProjectStore::create_project(const std::string& name,
                                                              const std::string& root,
                                                              std::string* error_message) {
    const std::string cleaned_root = trim_copy(root);
    if (!valid_absolute_path(cleaned_root)) {
        if (error_message) {
            *error_message = "invalid project root";
        }
        return std::nullopt;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(cleaned_root, ec)) {
        if (error_message) {
            *error_message = "project root does not exist";
        }
        return std::nullopt;
    }

    const std::string cleaned_name = trim_copy(name).empty() ? derive_project_name(cleaned_root) : trim_copy(name);
    if (!valid_title(cleaned_name)) {
        if (error_message) {
            *error_message = "invalid project name";
        }
        return std::nullopt;
    }

    CodexProject project;
    project.id = random_id();
    project.name = cleaned_name;
    project.root = cleaned_root;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& existing : projects_) {
            if (existing.root == project.root) {
                if (error_message) {
                    *error_message = "project root already exists";
                }
                return std::nullopt;
            }
        }
        projects_.push_back(project);
    }

    if (!save()) {
        if (error_message) {
            *error_message = "unable to save project";
        }
        return std::nullopt;
    }
    return project;
}

CodexConversationManager::CodexConversationManager(const CodexProjectStore& projects, std::string metadata_path)
    : projects_(projects), metadata_path_(std::move(metadata_path)) {}

CodexConversationManager::~CodexConversationManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, session] : sessions_) {
        (void)id;
        close_live_session_locked(session);
    }
}

bool CodexConversationManager::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    conversations_.clear();

    std::ifstream file(metadata_path_);
    if (!file.good()) {
        return true;
    }

    std::string line;
    while (std::getline(file, line)) {
        const auto parts = split(line, '\t');
        if (parts.size() != 9 && parts.size() != 11) {
            continue;
        }
        CodexConversationRecord record;
        record.id = parts[0];
        record.owner = parts[1];
        record.title = parts[2];
        record.project_id = parts[3];
        record.project_name = parts[4];
        record.working_directory = parts[5];
        if (parts.size() == 11) {
            record.codex_session_id = parts[6];
            record.maintenance_mode = parts[7] == "1";
            record.closed = parts[8] == "1";
            record.created_at = std::strtoll(parts[9].c_str(), nullptr, 10);
            record.updated_at = std::strtoll(parts[10].c_str(), nullptr, 10);
        } else {
            record.maintenance_mode = parts[6] == "1";
            record.closed = parts[7] == "1";
            record.created_at = std::strtoll(parts[8].c_str(), nullptr, 10);
            record.updated_at = record.created_at;
        }
        if (!record.id.empty() &&
            !record.owner.empty() &&
            !record.title.empty() &&
            valid_absolute_path(record.working_directory)) {
            conversations_.push_back(record);
        }
    }
    return true;
}

bool CodexConversationManager::save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::create_directories(std::filesystem::path(metadata_path_).parent_path());
    std::ofstream file(metadata_path_, std::ios::trunc);
    if (!file.good()) {
        return false;
    }
    for (const auto& conversation : conversations_) {
        file << conversation.id << '\t'
             << conversation.owner << '\t'
             << conversation.title << '\t'
             << conversation.project_id << '\t'
             << conversation.project_name << '\t'
             << conversation.working_directory << '\t'
             << conversation.codex_session_id << '\t'
             << (conversation.maintenance_mode ? "1" : "0") << '\t'
             << (conversation.closed ? "1" : "0") << '\t'
             << conversation.created_at << '\t'
             << conversation.updated_at << '\n';
    }
    file.close();
    return ensure_file_permissions(metadata_path_);
}

std::vector<CodexConversationRecord> CodexConversationManager::conversations_for(const std::string& owner) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CodexConversationRecord> out;
    for (const auto& conversation : conversations_) {
        if (conversation.owner == owner) {
            out.push_back(conversation);
        }
    }
    return out;
}

std::optional<const CodexConversationRecord*> CodexConversationManager::conversation_locked(const std::string& conversation_id,
                                                                                            const std::string& owner) const {
    for (const auto& conversation : conversations_) {
        if (conversation.id == conversation_id && conversation.owner == owner) {
            return &conversation;
        }
    }
    return std::nullopt;
}

std::optional<CodexConversationRecord*> CodexConversationManager::mutable_conversation_locked(const std::string& conversation_id,
                                                                                              const std::string& owner) {
    for (auto& conversation : conversations_) {
        if (conversation.id == conversation_id && conversation.owner == owner) {
            return &conversation;
        }
    }
    return std::nullopt;
}

std::optional<CodexConversationRecord> CodexConversationManager::find_conversation(const std::string& conversation_id,
                                                                                   const std::string& owner) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = conversation_locked(conversation_id, owner);
    if (!found) {
        return std::nullopt;
    }
    return **found;
}

std::string CodexConversationManager::transcript_path_locked(const std::string& conversation_id) const {
    const auto directory = std::filesystem::path(metadata_path_).parent_path() / "codex_transcripts";
    return (directory / (conversation_id + ".log")).string();
}

std::string CodexConversationManager::audit_log_path_locked(const std::string& conversation_id) const {
    const auto directory = std::filesystem::path(metadata_path_).parent_path() / "codex_audit";
    return (directory / (conversation_id + ".log")).string();
}

void CodexConversationManager::append_transcript_locked(const std::string& conversation_id, const std::string& chunk) const {
    if (chunk.empty()) {
        return;
    }
    const auto path = transcript_path_locked(conversation_id);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream file(path, std::ios::app | std::ios::binary);
    if (!file.good()) {
        return;
    }
    file.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    file.close();
    (void)ensure_file_permissions(path);
}

void CodexConversationManager::append_audit_event_locked(const std::string& conversation_id,
                                                         const std::string& kind,
                                                         const std::string& detail) const {
    const auto path = audit_log_path_locked(conversation_id);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream file(path, std::ios::app);
    if (!file.good()) {
        return;
    }
    file << unix_now() << '\t' << kind << '\t' << sanitize_detail(detail) << '\n';
    file.close();
    (void)ensure_file_permissions(path);
}

std::optional<std::string> CodexConversationManager::spawn_session_locked(const CodexConversationRecord& conversation,
                                                                          bool resume_existing,
                                                                          std::string* error_message) {
    const auto config = codex_runtime_config();
    const bool supports_skip_git_repo_check = codex_cli_supports_skip_git_repo_check(config);
    if (!valid_absolute_path(config.binary_path) || access(config.binary_path.c_str(), X_OK) != 0) {
        if (error_message) {
            *error_message = "codex binary is missing or not executable";
        }
        return std::nullopt;
    }

    const auto before_ids = read_session_index_ids();
    std::vector<std::string> args;
    args.push_back(config.binary_path);
    if (resume_existing && !conversation.codex_session_id.empty()) {
        args.push_back("resume");
        args.push_back(conversation.codex_session_id);
        args.push_back("--no-alt-screen");
        if (!config.model.empty()) {
            args.push_back("--model");
            args.push_back(config.model);
        }
        args.push_back("--cd");
        args.push_back(conversation.working_directory);
        if (supports_skip_git_repo_check &&
            (conversation.maintenance_mode || !is_git_repo_root(conversation.working_directory))) {
            args.push_back("--skip-git-repo-check");
        }
    } else {
        args.push_back("--no-alt-screen");
        args.push_back("--sandbox");
        args.push_back("danger-full-access");
        args.push_back("--ask-for-approval");
        args.push_back("on-request");
        args.push_back("--cd");
        args.push_back(conversation.working_directory);
        if (!config.model.empty()) {
            args.push_back("--model");
            args.push_back(config.model);
        }
        if (supports_skip_git_repo_check &&
            (conversation.maintenance_mode || !is_git_repo_root(conversation.working_directory))) {
            args.push_back("--skip-git-repo-check");
        }
    }

    int master_fd = -1;
    winsize size{};
    size.ws_row = 30;
    size.ws_col = 100;
    const pid_t pid = forkpty(&master_fd, nullptr, nullptr, &size);
    if (pid < 0) {
        if (error_message) {
            *error_message = resume_existing ? "unable to resume Codex session" : "unable to start Codex session";
        }
        return std::nullopt;
    }

    if (pid == 0) {
        if (chdir(conversation.working_directory.c_str()) != 0) {
            _exit(126);
        }
        normalize_codex_term_env();
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(127);
    }

    int flags = fcntl(master_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
    }
    sessions_[conversation.id] = LiveSession{master_fd, pid, false, -1, "", 0};

    std::string session_id;
    if (resume_existing && !conversation.codex_session_id.empty()) {
        session_id = conversation.codex_session_id;
        append_audit_event_locked(conversation.id, "resumed", conversation.codex_session_id);
        log_message(LogLevel::Info, "resumed Codex conversation " + conversation.id + " with session " + conversation.codex_session_id);
    } else {
        session_id = detect_new_session_id(before_ids).value_or("");
        append_audit_event_locked(conversation.id, "started", conversation.working_directory);
        log_message(LogLevel::Info, "started Codex conversation " + conversation.id + " in " + conversation.working_directory);
        if (session_id.empty()) {
            log_message(LogLevel::Warning, "unable to detect Codex session id for conversation " + conversation.id);
        }
    }

    return session_id;
}

std::optional<CodexConversationRecord> CodexConversationManager::create_conversation(const std::string& owner,
                                                                                     const std::string& title,
                                                                                     const std::string& project_id,
                                                                                     std::string* error_message) {
    CodexConversationRecord conversation;
    conversation.id = random_id();
    conversation.owner = owner;
    conversation.project_id = trim_copy(project_id);
    conversation.created_at = unix_now();
    conversation.updated_at = conversation.created_at;

    if (conversation.project_id.empty()) {
        conversation.maintenance_mode = true;
        conversation.project_name = "Server maintenance";
        conversation.working_directory = codex_maintenance_workdir();
        conversation.title = trim_copy(title).empty() ? "Server maintenance" : trim_copy(title);
    } else {
        auto project = projects_.find_project(conversation.project_id);
        if (!project) {
            if (error_message) {
                *error_message = "project not found";
            }
            return std::nullopt;
        }
        conversation.project_name = project->name;
        conversation.working_directory = project->root;
        conversation.title = trim_copy(title).empty() ? project->name : trim_copy(title);
    }

    if (!valid_title(conversation.title)) {
        if (error_message) {
            *error_message = "invalid conversation title";
        }
        return std::nullopt;
    }
    if (!valid_absolute_path(conversation.working_directory) || !std::filesystem::is_directory(conversation.working_directory)) {
        if (error_message) {
            *error_message = "conversation working directory is invalid";
        }
        return std::nullopt;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto session_id = spawn_session_locked(conversation, false, error_message);
        if (!session_id && error_message && error_message->empty()) {
            *error_message = "unable to start conversation";
        }
        if (session_id && !session_id->empty()) {
            conversation.codex_session_id = *session_id;
        }
        append_audit_event_locked(conversation.id, "created", conversation.title);
        conversations_.push_back(conversation);
    }

    if (!save()) {
        if (error_message) {
            *error_message = "unable to save conversation";
        }
        return std::nullopt;
    }
    return conversation;
}

std::optional<CodexConversationSnapshot> CodexConversationManager::read_conversation(const std::string& conversation_id,
                                                                                     const std::string& owner,
                                                                                     std::uint64_t cursor) {
    CodexConversationSnapshot snapshot;
    bool should_save = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found_conversation = mutable_conversation_locked(conversation_id, owner);
        if (!found_conversation) {
            return std::nullopt;
        }
        auto& conversation = **found_conversation;
        auto found_session = live_session_locked(conversation_id);
        if (!found_session && !conversation.closed && !conversation.codex_session_id.empty()) {
            std::string error;
            if (spawn_session_locked(conversation, true, &error)) {
                conversation.updated_at = unix_now();
                should_save = true;
                found_session = live_session_locked(conversation_id);
            } else if (!error.empty()) {
                append_audit_event_locked(conversation_id, "resume_failed", error);
                log_message(LogLevel::Warning, "unable to resume Codex conversation " + conversation_id + ": " + error);
            }
        }
        if (!found_session) {
            if (!conversation.closed) {
                const std::string detail = conversation.codex_session_id.empty()
                    ? "missing stored codex session id"
                    : "codex session could not be resumed";
                append_audit_event_locked(conversation_id, "resume_unavailable", detail);
                conversation.closed = true;
                conversation.updated_at = unix_now();
                should_save = true;
            }
            snapshot.ok = true;
            snapshot.closed = true;
            snapshot.cursor = 0;
            snapshot.exit_code = 0;
            snapshot.title = conversation.title;
        } else {
            auto& session = **found_session;
            refresh_session_output_locked(conversation_id, session, conversation);
            const bool was_closed = conversation.closed;
            reap_if_exited_locked(conversation_id, session, conversation);
            if (conversation.closed != was_closed || session.exit_code >= 0) {
                should_save = true;
            }

            snapshot.ok = true;
            snapshot.closed = session.closed;
            snapshot.exit_code = session.exit_code;
            snapshot.title = conversation.title;

            const std::uint64_t earliest = session.stream_offset >= session.buffer.size()
                ? session.stream_offset - session.buffer.size()
                : 0;
            if (cursor < earliest) {
                snapshot.truncated = true;
                cursor = earliest;
            }
            if (cursor < session.stream_offset) {
                const std::size_t start = static_cast<std::size_t>(cursor - earliest);
                snapshot.data = session.buffer.substr(start);
            }
            snapshot.cursor = session.stream_offset;
        }
    }
    if (should_save) {
        (void)save();
    }
    return snapshot;
}

std::optional<std::string> CodexConversationManager::transcript_for(const std::string& conversation_id,
                                                                    const std::string& owner) const {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = conversation_locked(conversation_id, owner);
        if (!found) {
            return std::nullopt;
        }
        path = transcript_path_locked(conversation_id);
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return std::string();
    }
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

std::optional<std::vector<CodexAuditEvent>> CodexConversationManager::audit_history_for(const std::string& conversation_id,
                                                                                        const std::string& owner) const {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = conversation_locked(conversation_id, owner);
        if (!found) {
            return std::nullopt;
        }
        path = audit_log_path_locked(conversation_id);
    }
    std::vector<CodexAuditEvent> out;
    std::ifstream file(path);
    if (!file.good()) {
        return out;
    }
    std::string line;
    while (std::getline(file, line)) {
        const auto parts = split(line, '\t');
        if (parts.size() < 3) {
            continue;
        }
        CodexAuditEvent event;
        event.timestamp = std::strtoll(parts[0].c_str(), nullptr, 10);
        event.kind = parts[1];
        event.detail = parts[2];
        out.push_back(event);
    }
    return out;
}

bool CodexConversationManager::send_message(const std::string& conversation_id,
                                            const std::string& owner,
                                            const std::string& message) {
    const std::string cleaned_message = trim_copy(message);
    if (!valid_message(cleaned_message)) {
        return false;
    }

    bool should_save = false;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found_conversation = mutable_conversation_locked(conversation_id, owner);
        if (!found_conversation) {
            return false;
        }
        auto& conversation = **found_conversation;
        auto found_session = live_session_locked(conversation_id);
        if (!found_session && !conversation.closed && !conversation.codex_session_id.empty()) {
            std::string error;
            if (spawn_session_locked(conversation, true, &error)) {
                conversation.updated_at = unix_now();
                should_save = true;
                found_session = live_session_locked(conversation_id);
            } else {
                append_audit_event_locked(conversation_id, "resume_failed", error.empty() ? "unable to resume session" : error);
            }
        }
        if (!found_session) {
            if (!conversation.closed) {
                append_audit_event_locked(conversation_id,
                                          "resume_unavailable",
                                          conversation.codex_session_id.empty() ? "missing stored codex session id" : "codex session could not be resumed");
                conversation.closed = true;
                conversation.updated_at = unix_now();
                should_save = true;
            }
        } else {
            auto& session = **found_session;
            if (!session.closed && session.master_fd >= 0) {
                const std::string payload = cleaned_message + "\n";
                const ssize_t written = write(session.master_fd, payload.data(), payload.size());
                if (written >= 0) {
                    append_audit_event_locked(conversation_id, "message", cleaned_message);
                    conversation.updated_at = unix_now();
                    should_save = true;
                    ok = true;
                }
            }
        }
    }
    if (should_save) {
        (void)save();
    }
    return ok;
}

bool CodexConversationManager::close_conversation(const std::string& conversation_id,
                                                  const std::string& owner) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found_conversation = mutable_conversation_locked(conversation_id, owner);
        if (!found_conversation) {
            return false;
        }
        auto& conversation = **found_conversation;
        auto found_session = live_session_locked(conversation_id);
        if (found_session) {
            close_live_session_locked(**found_session);
        }
        append_audit_event_locked(conversation_id, "closed", "operator closed conversation");
        conversation.closed = true;
        conversation.updated_at = unix_now();
    }
    return save();
}

std::optional<CodexConversationManager::LiveSession*> CodexConversationManager::live_session_locked(const std::string& conversation_id) {
    auto found = sessions_.find(conversation_id);
    if (found == sessions_.end()) {
        return std::nullopt;
    }
    return &found->second;
}

void CodexConversationManager::refresh_session_output_locked(const std::string& conversation_id,
                                                             LiveSession& session,
                                                             CodexConversationRecord& conversation) {
    if (session.closed || session.master_fd < 0) {
        return;
    }
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t read_count = read(session.master_fd, buffer.data(), buffer.size());
        if (read_count > 0) {
            const std::string chunk(buffer.data(), static_cast<size_t>(read_count));
            session.buffer.append(chunk);
            append_transcript_locked(conversation_id, chunk);
            session.stream_offset += static_cast<std::uint64_t>(read_count);
            conversation.updated_at = unix_now();
            if (session.buffer.size() > kConversationBufferLimit) {
                const std::size_t overflow = session.buffer.size() - kConversationBufferLimit;
                session.buffer.erase(0, overflow);
            }
            continue;
        }
        if (read_count == 0) {
            close_live_session_locked(session);
        }
        if (read_count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        break;
    }
}

void CodexConversationManager::reap_if_exited_locked(const std::string& conversation_id,
                                                     LiveSession& session,
                                                     CodexConversationRecord& conversation) {
    if (session.pid <= 0 || session.exit_code >= 0) {
        return;
    }
    int status = 0;
    const pid_t result = waitpid(session.pid, &status, WNOHANG);
    if (result <= 0) {
        return;
    }
    if (WIFEXITED(status)) {
        session.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        session.exit_code = 128 + WTERMSIG(status);
    } else {
        session.exit_code = 0;
    }
    session.closed = true;
    close_live_session_locked(session);
    append_audit_event_locked(conversation_id, "process_exit", std::to_string(session.exit_code));
    conversation.closed = true;
    conversation.updated_at = unix_now();
    log_message(LogLevel::Info, "Codex conversation " + conversation_id + " closed with exit code " + std::to_string(session.exit_code));
}

void CodexConversationManager::close_live_session_locked(LiveSession& session) {
    if (!session.closed && session.pid > 0) {
        kill(session.pid, SIGTERM);
    }
    if (session.master_fd >= 0) {
        close(session.master_fd);
        session.master_fd = -1;
    }
    session.closed = true;
}

std::string codex_maintenance_workdir() {
    const auto config = codex_runtime_config();
    return config.working_directory.empty() ? "/root" : config.working_directory;
}

}
