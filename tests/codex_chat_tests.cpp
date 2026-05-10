#include "codex_chat.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path);
    file << content;
}

bool wait_for_output(cuddle::CodexConversationManager& manager,
                     const std::string& id,
                     const std::string& owner,
                     const std::string& expected) {
    std::uint64_t cursor = 0;
    std::string seen;
    for (int i = 0; i < 30; ++i) {
        auto snapshot = manager.read_conversation(id, owner, cursor);
        if (snapshot) {
            seen += snapshot->data;
            cursor = snapshot->cursor;
            if (seen.find(expected) != std::string::npos) {
                return true;
            }
        }
        usleep(50000);
    }
    return false;
}

bool wait_for_session_id(cuddle::CodexConversationManager& manager,
                         const std::string& id,
                         const std::string& owner) {
    for (int i = 0; i < 40; ++i) {
        auto conversation = manager.find_conversation(id, owner);
        if (conversation && !conversation->codex_session_id.empty()) {
            return true;
        }
        usleep(50000);
    }
    return false;
}

}

int main() {
    const auto temp_root = std::filesystem::temp_directory_path() / ("cuddlepanel-codex-chat-tests-" + std::to_string(getpid()));
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root / "project");

    const auto fake_codex = temp_root / "fake-codex.sh";
    write_file(fake_codex,
               "#!/bin/sh\n"
               "set -eu\n"
               "CODEX_HOME_DIR=\"${CODEX_HOME:-$HOME/.codex}\"\n"
               "mkdir -p \"$CODEX_HOME_DIR\"\n"
               "SESSION_INDEX=\"$CODEX_HOME_DIR/session_index.jsonl\"\n"
               "if [ \"${1:-}\" = \"resume\" ]; then\n"
               "  SESSION_ID=\"$2\"\n"
               "  shift 2\n"
               "else\n"
               "  SESSION_ID=\"session-$$\"\n"
               "  printf '{\"id\":\"%s\",\"updated_at\":\"test\"}\\n' \"$SESSION_ID\" >> \"$SESSION_INDEX\"\n"
               "fi\n"
               "while [ \"$#\" -gt 0 ]; do\n"
               "  case \"$1\" in\n"
               "    --cd) cd \"$2\"; shift 2 ;;\n"
                "    --sandbox|--ask-for-approval|--model) shift 2 ;;\n"
                "    --no-alt-screen|--skip-git-repo-check) shift ;;\n"
                "    *) shift ;;\n"
                "  esac\n"
                "done\n"
               "printf 'ready in %s [%s]\\n' \"$PWD\" \"$SESSION_ID\"\n"
               "while IFS= read -r line; do\n"
                "  printf 'codex heard: %s\\n' \"$line\"\n"
                "done\n");
    std::filesystem::permissions(fake_codex,
                                 std::filesystem::perms::owner_read |
                                 std::filesystem::perms::owner_write |
                                 std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace);

    setenv("CUDDLEPANEL_CODEX_BIN", fake_codex.c_str(), 1);
    setenv("CUDDLEPANEL_CODEX_WORKDIR", temp_root.c_str(), 1);
    setenv("CODEX_HOME", (temp_root / "codex-home").c_str(), 1);

    cuddle::CodexProjectStore projects((temp_root / "projects.db").string());
    assert(projects.load());
    std::string error;
    auto project = projects.create_project("Demo", (temp_root / "project").string(), &error);
    assert(project);

    std::string conversation_id;
    std::string original_session_id;
    {
        cuddle::CodexConversationManager manager(projects, (temp_root / "conversations.db").string());
        assert(manager.load());

        auto conversation = manager.create_conversation("alice", "", project->id, &error);
        assert(conversation);
        conversation_id = conversation->id;
        assert(wait_for_session_id(manager, conversation->id, "alice"));
        assert(wait_for_output(manager, conversation->id, "alice", "ready in"));

        assert(manager.send_message(conversation->id, "alice", "approve"));
        assert(wait_for_output(manager, conversation->id, "alice", "codex heard: approve"));

        auto transcript = manager.transcript_for(conversation->id, "alice");
        assert(transcript);
        assert(transcript->find("ready in") != std::string::npos);
        assert(transcript->find("codex heard: approve") != std::string::npos);

        auto audit = manager.audit_history_for(conversation->id, "alice");
        assert(audit);
        assert(!audit->empty());

        const auto stored_before_restart = manager.find_conversation(conversation->id, "alice");
        assert(stored_before_restart);
        assert(!stored_before_restart->codex_session_id.empty());
        original_session_id = stored_before_restart->codex_session_id;
    }

    {
        cuddle::CodexConversationManager manager(projects, (temp_root / "conversations.db").string());
        assert(manager.load());

        auto stored_after_restart = manager.find_conversation(conversation_id, "alice");
        assert(stored_after_restart);
        assert(stored_after_restart->codex_session_id == original_session_id);

        assert(wait_for_output(manager, conversation_id, "alice", original_session_id));
        assert(manager.send_message(conversation_id, "alice", "resume-check"));
        assert(wait_for_output(manager, conversation_id, "alice", "codex heard: resume-check"));

        assert(manager.close_conversation(conversation_id, "alice"));
        auto stored = manager.find_conversation(conversation_id, "alice");
        assert(stored);
        assert(stored->closed);

        auto final_audit = manager.audit_history_for(conversation_id, "alice");
        assert(final_audit);
        bool saw_resume = false;
        bool saw_close = false;
        for (const auto& event : *final_audit) {
            if (event.kind == "resumed") {
                saw_resume = true;
            }
            if (event.kind == "closed") {
                saw_close = true;
            }
        }
        assert(saw_resume);
        assert(saw_close);
    }

    std::filesystem::remove_all(temp_root);
    std::cout << "codex chat tests passed" << std::endl;
    return 0;
}
