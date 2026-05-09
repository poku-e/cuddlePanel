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
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <vector>

namespace cuddle {
namespace {

constexpr size_t kConversationBufferLimit = 512 * 1024;

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
        if (parts.size() != 9) {
            continue;
        }
        CodexConversationRecord record;
        record.id = parts[0];
        record.owner = parts[1];
        record.title = parts[2];
        record.project_id = parts[3];
        record.project_name = parts[4];
        record.working_directory = parts[5];
        record.maintenance_mode = parts[6] == "1";
        record.closed = parts[7] == "1";
        record.created_at = std::strtoll(parts[8].c_str(), nullptr, 10);
        record.updated_at = record.created_at;
        if (!record.id.empty() && !record.owner.empty() && !record.title.empty() && valid_absolute_path(record.working_directory)) {
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
             << (conversation.maintenance_mode ? "1" : "0") << '\t'
             << (conversation.closed ? "1" : "0") << '\t'
             << conversation.created_at << '\n';
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

std::optional<std::string> CodexConversationManager::spawn_session_locked(const CodexConversationRecord& conversation,
                                                                          std::string* error_message) {
    const auto config = codex_runtime_config();
    if (!valid_absolute_path(config.binary_path) || access(config.binary_path.c_str(), X_OK) != 0) {
        if (error_message) {
            *error_message = "codex binary is missing or not executable";
        }
        return std::nullopt;
    }

    std::vector<std::string> args = {
        config.binary_path,
        "--no-alt-screen",
        "--sandbox", "danger-full-access",
        "--ask-for-approval", "on-request",
        "--cd", conversation.working_directory
    };
    if (!config.model.empty()) {
        args.insert(args.end(), {"--model", config.model});
    }
    if (conversation.maintenance_mode || !is_git_repo_root(conversation.working_directory)) {
        args.push_back("--skip-git-repo-check");
    }

    int master_fd = -1;
    winsize size{};
    size.ws_row = 30;
    size.ws_col = 100;
    const pid_t pid = forkpty(&master_fd, nullptr, nullptr, &size);
    if (pid < 0) {
        if (error_message) {
            *error_message = "unable to start Codex session";
        }
        return std::nullopt;
    }

    if (pid == 0) {
        if (chdir(conversation.working_directory.c_str()) != 0) {
            _exit(126);
        }
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
    log_message(LogLevel::Info, "started Codex conversation " + conversation.id + " in " + conversation.working_directory);
    return conversation.id;
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
        if (!spawn_session_locked(conversation, error_message)) {
            return std::nullopt;
        }
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
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found_conversation = mutable_conversation_locked(conversation_id, owner);
    if (!found_conversation) {
        return std::nullopt;
    }
    auto& conversation = **found_conversation;
    auto found_session = live_session_locked(conversation_id);
    if (!found_session) {
        CodexConversationSnapshot snapshot;
        snapshot.ok = true;
        snapshot.closed = true;
        snapshot.cursor = 0;
        snapshot.exit_code = 0;
        snapshot.title = conversation.title;
        return snapshot;
    }

    auto& session = **found_session;
    refresh_session_output_locked(conversation_id, session, conversation);
    reap_if_exited_locked(conversation_id, session, conversation);

    CodexConversationSnapshot snapshot;
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
    return snapshot;
}

bool CodexConversationManager::send_message(const std::string& conversation_id,
                                            const std::string& owner,
                                            const std::string& message) {
    const std::string cleaned_message = trim_copy(message);
    if (!valid_message(cleaned_message)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto found_conversation = mutable_conversation_locked(conversation_id, owner);
    if (!found_conversation) {
        return false;
    }
    auto& conversation = **found_conversation;
    auto found_session = live_session_locked(conversation_id);
    if (!found_session) {
        return false;
    }
    auto& session = **found_session;
    if (session.closed || session.master_fd < 0) {
        return false;
    }
    const std::string payload = cleaned_message + "\n";
    const ssize_t written = write(session.master_fd, payload.data(), payload.size());
    if (written < 0) {
        return false;
    }
    conversation.updated_at = unix_now();
    return true;
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
            session.buffer.append(buffer.data(), static_cast<size_t>(read_count));
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
    (void)conversation_id;
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
