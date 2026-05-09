#include "terminal_manager.h"

#include <fcntl.h>
#include <filesystem>
#include <grp.h>
#include <pwd.h>
#include <pty.h>
#include <signal.h>
#include <sodium.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

namespace cuddle {
namespace {

constexpr size_t kTerminalBufferLimit = 256 * 1024;

std::int64_t unix_now() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

std::string random_session_id() {
    std::array<unsigned char, 16> bytes{};
    randombytes_buf(bytes.data(), bytes.size());
    std::string out(bytes.size() * 2, '\0');
    sodium_bin2hex(out.data(), out.size() + 1, bytes.data(), bytes.size());
    return out;
}

bool drop_terminal_privileges(const TerminalRuntimePolicy& policy) {
    passwd* pw = getpwnam(policy.run_as_user.c_str());
    if (!pw) {
        return false;
    }

    gid_t target_gid = pw->pw_gid;
    if (!policy.run_as_group.empty()) {
        group* gr = getgrnam(policy.run_as_group.c_str());
        if (!gr) {
            return false;
        }
        target_gid = gr->gr_gid;
    }

    if (initgroups(pw->pw_name, target_gid) != 0) {
        return false;
    }
    if (setgid(target_gid) != 0) {
        return false;
    }
    if (setuid(pw->pw_uid) != 0) {
        return false;
    }
    return true;
}

void configure_child_environment(const TerminalRuntimePolicy& policy) {
    clearenv();
    setenv("TERM", "xterm-256color", 1);
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
    setenv("LANG", "C.UTF-8", 1);
    setenv("LC_ALL", "C.UTF-8", 1);
    setenv("SHELL", policy.shell_path.c_str(), 1);
    setenv("HOME", policy.working_directory.c_str(), 1);
    setenv("USER", policy.run_as_user.c_str(), 1);
    setenv("LOGNAME", policy.run_as_user.c_str(), 1);
}

}

TerminalManager::TerminalManager() = default;

TerminalManager::~TerminalManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, session] : sessions_) {
        (void)id;
        if (!session.closed && session.pid > 0) {
            kill(session.pid, SIGTERM);
        }
        if (session.master_fd >= 0) {
            close(session.master_fd);
        }
    }
}

std::optional<std::string> TerminalManager::create_session(const std::string& owner,
                                                           std::uint16_t rows,
                                                           std::uint16_t cols) {
    const auto policy = terminal_runtime_policy();
    if (policy.shell_path.empty() || policy.run_as_user.empty() || policy.working_directory.empty()) {
        return std::nullopt;
    }
    if (policy.shell_path.front() != '/' || access(policy.shell_path.c_str(), X_OK) != 0) {
        return std::nullopt;
    }
    if (!std::filesystem::is_directory(policy.working_directory)) {
        return std::nullopt;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        expire_stale_sessions_locked(unix_now());
        std::uint32_t active_for_owner = 0;
        for (const auto& [id, session] : sessions_) {
            (void)id;
            if (session.owner == owner && !session.closed) {
                ++active_for_owner;
            }
        }
        if (active_for_owner >= policy.max_sessions_per_user) {
            return std::nullopt;
        }
    }

    winsize size{};
    size.ws_row = rows > 0 ? rows : 24;
    size.ws_col = cols > 0 ? cols : 80;

    int master_fd = -1;
    pid_t pid = forkpty(&master_fd, nullptr, nullptr, &size);
    if (pid < 0) {
        return std::nullopt;
    }

    if (pid == 0) {
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
        configure_child_environment(policy);
        if (chdir(policy.working_directory.c_str()) != 0) {
            _exit(126);
        }
        if (!drop_terminal_privileges(policy)) {
            _exit(126);
        }
        execl(policy.shell_path.c_str(), policy.shell_path.c_str(), "-l", static_cast<char*>(nullptr));
        _exit(127);
    }

    int flags = fcntl(master_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto session_id = random_session_id();
    const auto now = unix_now();
    sessions_[session_id] = Session{owner, master_fd, pid, false, -1, "", 0, now, now};
    return session_id;
}

std::optional<TerminalSnapshot> TerminalManager::read_session(const std::string& session_id,
                                                              const std::string& owner,
                                                              std::uint64_t cursor) {
    std::lock_guard<std::mutex> lock(mutex_);
    expire_stale_sessions_locked(unix_now());
    auto found = sessions_.find(session_id);
    if (found == sessions_.end() || found->second.owner != owner) {
        return std::nullopt;
    }

    auto& session = found->second;
    refresh_session_output(session);
    reap_if_exited(session);
    session.last_activity_at = unix_now();

    TerminalSnapshot snapshot;
    snapshot.ok = true;
    snapshot.closed = session.closed;
    snapshot.exit_code = session.exit_code;

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

bool TerminalManager::write_session(const std::string& session_id,
                                    const std::string& owner,
                                    const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    expire_stale_sessions_locked(unix_now());
    auto found = sessions_.find(session_id);
    if (found == sessions_.end() || found->second.owner != owner) {
        return false;
    }

    auto& session = found->second;
    refresh_session_output(session);
    reap_if_exited(session);
    if (session.closed || session.master_fd < 0) {
        return false;
    }
    const ssize_t written = write(session.master_fd, data.data(), data.size());
    session.last_activity_at = unix_now();
    return written >= 0;
}

bool TerminalManager::resize_session(const std::string& session_id,
                                     const std::string& owner,
                                     std::uint16_t rows,
                                     std::uint16_t cols) {
    std::lock_guard<std::mutex> lock(mutex_);
    expire_stale_sessions_locked(unix_now());
    auto found = sessions_.find(session_id);
    if (found == sessions_.end() || found->second.owner != owner) {
        return false;
    }

    auto& session = found->second;
    if (session.closed || session.master_fd < 0) {
        return false;
    }
    winsize size{};
    size.ws_row = rows > 0 ? rows : 24;
    size.ws_col = cols > 0 ? cols : 80;
    session.last_activity_at = unix_now();
    return ioctl(session.master_fd, TIOCSWINSZ, &size) == 0;
}

bool TerminalManager::close_session(const std::string& session_id, const std::string& owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    expire_stale_sessions_locked(unix_now());
    auto found = sessions_.find(session_id);
    if (found == sessions_.end() || found->second.owner != owner) {
        return false;
    }

    auto& session = found->second;
    close_session_locked(session);
    return true;
}

void TerminalManager::refresh_session_output(Session& session) {
    if (session.closed || session.master_fd < 0) {
        return;
    }

    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t read_count = read(session.master_fd, buffer.data(), buffer.size());
        if (read_count > 0) {
            session.buffer.append(buffer.data(), static_cast<size_t>(read_count));
            session.stream_offset += static_cast<std::uint64_t>(read_count);
            session.last_activity_at = unix_now();
            if (session.buffer.size() > kTerminalBufferLimit) {
                const std::size_t overflow = session.buffer.size() - kTerminalBufferLimit;
                session.buffer.erase(0, overflow);
            }
            continue;
        }
        if (read_count == 0) {
            close_session_locked(session);
        }
        if (read_count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        break;
    }
}

void TerminalManager::reap_if_exited(Session& session) {
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
    close_session_locked(session);
}

std::string terminal_shell_path() {
    const char* configured = std::getenv("CUDDLEPANEL_TERMINAL_SHELL");
    return configured && *configured ? configured : "/bin/bash";
}

TerminalRuntimePolicy terminal_runtime_policy() {
    TerminalRuntimePolicy policy;
    policy.shell_path = terminal_shell_path();

    const char* run_user = std::getenv("CUDDLEPANEL_TERMINAL_RUN_AS_USER");
    policy.run_as_user = run_user && *run_user ? run_user : "nobody";

    const char* run_group = std::getenv("CUDDLEPANEL_TERMINAL_RUN_AS_GROUP");
    policy.run_as_group = run_group && *run_group ? run_group : "nogroup";

    const char* workdir = std::getenv("CUDDLEPANEL_TERMINAL_WORKDIR");
    policy.working_directory = workdir && *workdir ? workdir : "/tmp";

    const char* max_sessions = std::getenv("CUDDLEPANEL_TERMINAL_MAX_SESSIONS_PER_USER");
    if (max_sessions && *max_sessions) {
        policy.max_sessions_per_user = static_cast<std::uint32_t>(std::strtoul(max_sessions, nullptr, 10));
    }
    const char* idle_timeout = std::getenv("CUDDLEPANEL_TERMINAL_IDLE_TIMEOUT_SECONDS");
    if (idle_timeout && *idle_timeout) {
        policy.idle_timeout_seconds = static_cast<std::uint32_t>(std::strtoul(idle_timeout, nullptr, 10));
    }
    const char* max_runtime = std::getenv("CUDDLEPANEL_TERMINAL_MAX_SESSION_SECONDS");
    if (max_runtime && *max_runtime) {
        policy.max_session_seconds = static_cast<std::uint32_t>(std::strtoul(max_runtime, nullptr, 10));
    }
    if (policy.max_sessions_per_user == 0) {
        policy.max_sessions_per_user = 1;
    }
    if (policy.idle_timeout_seconds == 0) {
        policy.idle_timeout_seconds = 300;
    }
    if (policy.max_session_seconds == 0) {
        policy.max_session_seconds = 1800;
    }
    return policy;
}

void TerminalManager::close_session_locked(Session& session) {
    if (!session.closed && session.pid > 0) {
        kill(session.pid, SIGTERM);
    }
    if (session.master_fd >= 0) {
        close(session.master_fd);
        session.master_fd = -1;
    }
    session.closed = true;
}

void TerminalManager::expire_stale_sessions_locked(std::int64_t now) {
    const auto policy = terminal_runtime_policy();
    for (auto& [id, session] : sessions_) {
        (void)id;
        if (session.closed) {
            continue;
        }
        const bool idle_expired = (now - session.last_activity_at) > static_cast<std::int64_t>(policy.idle_timeout_seconds);
        const bool lifetime_expired = (now - session.started_at) > static_cast<std::int64_t>(policy.max_session_seconds);
        if (idle_expired || lifetime_expired) {
            close_session_locked(session);
        }
    }
}

}
