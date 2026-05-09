#include "terminal_manager.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

int main() {
    setenv("CUDDLEPANEL_TERMINAL_SHELL", "/bin/cat", 1);
    setenv("CUDDLEPANEL_TERMINAL_RUN_AS_USER", "nobody", 1);
    setenv("CUDDLEPANEL_TERMINAL_RUN_AS_GROUP", "nogroup", 1);
    setenv("CUDDLEPANEL_TERMINAL_WORKDIR", "/tmp", 1);
    setenv("CUDDLEPANEL_TERMINAL_MAX_SESSIONS_PER_USER", "1", 1);
    cuddle::TerminalManager terminal;
    auto session_id = terminal.create_session("admin", 24, 80);
    assert(session_id);
    assert(!terminal.create_session("admin", 24, 80));

    assert(terminal.resize_session(*session_id, "admin", 30, 100));
    assert(terminal.write_session(*session_id, "admin", "hello terminal\n"));

    bool got_output = false;
    for (int i = 0; i < 20; ++i) {
        auto maybe = terminal.read_session(*session_id, "admin", 0);
        assert(maybe);
        if (maybe->data.find("hello terminal") != std::string::npos) {
            got_output = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    assert(got_output);
    assert(!terminal.write_session(*session_id, "other-user", "bad"));
    assert(terminal.close_session(*session_id, "admin"));

    std::cout << "terminal tests passed" << std::endl;
    return 0;
}
