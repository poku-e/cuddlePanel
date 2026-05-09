#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace cuddle {

struct TerminalRuntimePolicy {
    std::string shell_path;
    std::string run_as_user;
    std::string run_as_group;
    std::string working_directory;
    std::uint32_t max_sessions_per_user = 2;
    std::uint32_t idle_timeout_seconds = 15 * 60;
    std::uint32_t max_session_seconds = 2 * 60 * 60;
};

struct TerminalSnapshot {
    bool ok = false;
    bool closed = false;
    bool truncated = false;
    std::string data;
    std::uint64_t cursor = 0;
    std::int32_t exit_code = -1;
};

class TerminalManager {
public:
    TerminalManager();
    ~TerminalManager();

    std::optional<std::string> create_session(const std::string& owner,
                                              std::uint16_t rows,
                                              std::uint16_t cols);
    std::optional<TerminalSnapshot> read_session(const std::string& session_id,
                                                 const std::string& owner,
                                                 std::uint64_t cursor);
    bool write_session(const std::string& session_id,
                       const std::string& owner,
                       const std::string& data);
    bool resize_session(const std::string& session_id,
                        const std::string& owner,
                        std::uint16_t rows,
                        std::uint16_t cols);
    bool close_session(const std::string& session_id, const std::string& owner);

private:
    struct Session {
        std::string owner;
        int master_fd = -1;
        int pid = -1;
        bool closed = false;
        std::int32_t exit_code = -1;
        std::string buffer;
        std::uint64_t stream_offset = 0;
        std::int64_t started_at = 0;
        std::int64_t last_activity_at = 0;
    };

    void refresh_session_output(Session& session);
    void reap_if_exited(Session& session);
    void close_session_locked(Session& session);
    void expire_stale_sessions_locked(std::int64_t now);

    mutable std::mutex mutex_;
    std::map<std::string, Session> sessions_;
};

std::string terminal_shell_path();
TerminalRuntimePolicy terminal_runtime_policy();

}
