cmake_minimum_required(VERSION 3.16)
project(cuddlePanel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../bin)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CUDDLEPANEL_ENABLE_UPX "Compress the final server binary with UPX when available." ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)
find_package(OpenSSL REQUIRED)
find_program(UPX_EXECUTABLE upx)

include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
if(IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

add_library(cuddle_core
    src/auth.cpp
    src/deploy_runner.cpp
    src/http.cpp
    src/nginx_store.cpp
    src/service_store.cpp
    src/system_admin.cpp
    src/terminal_manager.cpp
    src/template.cpp
    src/totp.cpp
    src/user_store.cpp
)

target_include_directories(cuddle_core PUBLIC include ${SODIUM_INCLUDE_DIRS})
target_link_libraries(cuddle_core PUBLIC ${SODIUM_LIBRARIES} OpenSSL::Crypto)
target_compile_options(cuddle_core PRIVATE -Wall -Wextra -Wpedantic)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cuddle_core PRIVATE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>
    )
endif()

add_executable(server src/main.cpp)
target_link_libraries(server PRIVATE cuddle_core)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(server PRIVATE
        $<$<CONFIG:Release>:-Wl,--gc-sections>
        $<$<CONFIG:Release>:-Wl,--strip-all>
    )
endif()

if(CUDDLEPANEL_ENABLE_UPX)
    if(UPX_EXECUTABLE)
        add_custom_command(TARGET server POST_BUILD
            COMMAND "${UPX_EXECUTABLE}" --best --lzma "$<TARGET_FILE:server>"
            COMMENT "Packing server with UPX"
            VERBATIM
        )
    else()
        message(WARNING "CUDDLEPANEL_ENABLE_UPX is ON but 'upx' was not found. The final server binary will not be packed.")
    endif()
endif()

enable_testing()
add_executable(auth_tests tests/auth_tests.cpp)
target_link_libraries(auth_tests PRIVATE cuddle_core)
add_test(NAME auth_tests COMMAND auth_tests)

add_executable(service_tests tests/service_tests.cpp)
target_link_libraries(service_tests PRIVATE cuddle_core)
add_test(NAME service_tests COMMAND service_tests)

add_executable(nginx_tests tests/nginx_tests.cpp)
target_link_libraries(nginx_tests PRIVATE cuddle_core)
add_test(NAME nginx_tests COMMAND nginx_tests)

add_executable(deploy_tests tests/deploy_tests.cpp)
target_link_libraries(deploy_tests PRIVATE cuddle_core)
add_test(NAME deploy_tests COMMAND deploy_tests)

add_executable(terminal_tests tests/terminal_tests.cpp)
target_link_libraries(terminal_tests PRIVATE cuddle_core)
add_test(NAME terminal_tests COMMAND terminal_tests)

add_executable(system_admin_tests tests/system_admin_tests.cpp)
target_link_libraries(system_admin_tests PRIVATE cuddle_core)
add_test(NAME system_admin_tests COMMAND system_admin_tests)
