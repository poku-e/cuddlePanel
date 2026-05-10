#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cuddle {

struct CodexProject {
    std::string id;
    std::string name;
    std::string root;
};

struct CodexConversationRecord {
    std::string id;
    std::string owner;
    std::string title;
    std::string project_id;
    std::string project_name;
    std::string working_directory;
    std::string codex_session_id;
    bool maintenance_mode = false;
    bool closed = false;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
};

struct CodexAuditEvent {
    std::int64_t timestamp = 0;
    std::string kind;
    std::string detail;
};

struct CodexConversationSnapshot {
    bool ok = false;
    bool closed = false;
    bool truncated = false;
    std::string data;
    std::uint64_t cursor = 0;
    std::int32_t exit_code = -1;
    std::string title;
};

class CodexProjectStore {
public:
    explicit CodexProjectStore(std::string path);

    bool load();
    bool save() const;
    std::vector<CodexProject> projects() const;
    std::optional<CodexProject> find_project(const std::string& id) const;
    std::optional<CodexProject> create_project(const std::string& name,
                                               const std::string& root,
                                               std::string* error_message = nullptr);

private:
    std::string path_;
    mutable std::mutex mutex_;
    std::vector<CodexProject> projects_;
};

class CodexConversationManager {
public:
    CodexConversationManager(const CodexProjectStore& projects, std::string metadata_path);
    ~CodexConversationManager();

    bool load();
    bool save() const;
    std::vector<CodexConversationRecord> conversations_for(const std::string& owner) const;
    std::optional<CodexConversationRecord> find_conversation(const std::string& conversation_id,
                                                             const std::string& owner) const;
    std::optional<CodexConversationRecord> create_conversation(const std::string& owner,
                                                               const std::string& title,
                                                               const std::string& project_id,
                                                               std::string* error_message = nullptr);
    std::optional<CodexConversationSnapshot> read_conversation(const std::string& conversation_id,
                                                               const std::string& owner,
                                                               std::uint64_t cursor);
    std::optional<std::string> transcript_for(const std::string& conversation_id,
                                              const std::string& owner) const;
    std::optional<std::vector<CodexAuditEvent>> audit_history_for(const std::string& conversation_id,
                                                                  const std::string& owner) const;
    bool send_message(const std::string& conversation_id,
                      const std::string& owner,
                      const std::string& message);
    bool close_conversation(const std::string& conversation_id,
                            const std::string& owner);

private:
    struct LiveSession {
        int master_fd = -1;
        int pid = -1;
        bool closed = false;
        std::int32_t exit_code = -1;
        std::string buffer;
        std::uint64_t stream_offset = 0;
    };

    const CodexProjectStore& projects_;
    std::string metadata_path_;
    mutable std::mutex mutex_;
    std::vector<CodexConversationRecord> conversations_;
    std::map<std::string, LiveSession> sessions_;

    std::optional<CodexConversationRecord*> mutable_conversation_locked(const std::string& conversation_id,
                                                                        const std::string& owner);
    std::optional<const CodexConversationRecord*> conversation_locked(const std::string& conversation_id,
                                                                      const std::string& owner) const;
    std::optional<LiveSession*> live_session_locked(const std::string& conversation_id);
    std::string transcript_path_locked(const std::string& conversation_id) const;
    std::string audit_log_path_locked(const std::string& conversation_id) const;
    void append_transcript_locked(const std::string& conversation_id, const std::string& chunk) const;
    void append_audit_event_locked(const std::string& conversation_id,
                                   const std::string& kind,
                                   const std::string& detail) const;
    std::optional<std::string> spawn_session_locked(const CodexConversationRecord& conversation,
                                                    bool resume_existing,
                                                    std::string* error_message);
    void refresh_session_output_locked(const std::string& conversation_id,
                                       LiveSession& session,
                                       CodexConversationRecord& conversation);
    void reap_if_exited_locked(const std::string& conversation_id,
                               LiveSession& session,
                               CodexConversationRecord& conversation);
    void close_live_session_locked(LiveSession& session);
};

std::string codex_maintenance_workdir();

}
