cmake_minimum_required(VERSION 3.20)
project(ShirleyOS LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(shirley-kernel-core STATIC
    kernel/kernel_main.cpp
    kernel/console/console.cpp
    kernel/memory/page_allocator.cpp
)
target_include_directories(shirley-kernel-core PUBLIC include)
target_compile_options(shirley-kernel-core PRIVATE -ffreestanding)

add_executable(shirley-host-smoke tests/host_smoke.cpp)
target_link_libraries(shirley-host-smoke PRIVATE shirley-kernel-core)
target_include_directories(shirley-host-smoke PRIVATE include)

enable_testing()
add_test(NAME host-smoke COMMAND shirley-host-smoke)

add_custom_target(shirley-x86_64
    COMMAND ${CMAKE_COMMAND} -E echo "x86_64 firmware image is not built yet: provide the freestanding UEFI toolchain"
    SOURCES arch/x86_64/README.md platform/qemu_x86_64/README.md)
add_custom_target(shirley-arm64
    COMMAND ${CMAKE_COMMAND} -E echo "ARM64 firmware image is not built yet: provide the freestanding UEFI toolchain"
    SOURCES arch/arm64/README.md platform/qemu_arm64/README.md)
