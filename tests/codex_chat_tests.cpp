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

}

int main() {
    const auto temp_root = std::filesystem::temp_directory_path() / ("cuddlepanel-codex-chat-tests-" + std::to_string(getpid()));
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root / "project");

    const auto fake_codex = temp_root / "fake-codex.sh";
    write_file(fake_codex,
               "#!/bin/sh\n"
               "while [ \"$#\" -gt 0 ]; do\n"
               "  case \"$1\" in\n"
               "    --cd) cd \"$2\"; shift 2 ;;\n"
               "    --sandbox|--ask-for-approval|--model) shift 2 ;;\n"
               "    --no-alt-screen|--skip-git-repo-check) shift ;;\n"
               "    *) shift ;;\n"
               "  esac\n"
               "done\n"
               "printf 'ready in %s\\n' \"$PWD\"\n"
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

    cuddle::CodexProjectStore projects((temp_root / "projects.db").string());
    assert(projects.load());
    std::string error;
    auto project = projects.create_project("Demo", (temp_root / "project").string(), &error);
    assert(project);

    cuddle::CodexConversationManager manager(projects, (temp_root / "conversations.db").string());
    assert(manager.load());

    auto conversation = manager.create_conversation("alice", "", project->id, &error);
    assert(conversation);
    assert(wait_for_output(manager, conversation->id, "alice", "ready in"));

    assert(manager.send_message(conversation->id, "alice", "approve"));
    assert(wait_for_output(manager, conversation->id, "alice", "codex heard: approve"));

    assert(manager.close_conversation(conversation->id, "alice"));
    auto stored = manager.find_conversation(conversation->id, "alice");
    assert(stored);
    assert(stored->closed);

    std::filesystem::remove_all(temp_root);
    std::cout << "codex chat tests passed" << std::endl;
    return 0;
}
