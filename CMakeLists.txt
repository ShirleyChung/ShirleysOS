# 宣告最低 CMake 版本與本專案使用的語言。
cmake_minimum_required(VERSION 3.20)
project(ShirleyOS LANGUAGES C CXX ASM)
if(NOT SHIRLEY_TARGET)
  set(SHIRLEY_TARGET host)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
if(SHIRLEY_TARGET STREQUAL arm64)
  find_program(SHIRLEY_LLD ld.lld REQUIRED)
  find_program(SHIRLEY_OBJCOPY llvm-objcopy REQUIRED)
  # ARM64 直接連結核心 ELF。
  add_executable(shirley-kernel kernel/kernel_main.cpp arch/arm64/arch.cpp arch/arm64/entry.S platform/qemu_arm64/console.cpp)
  target_include_directories(shirley-kernel PRIVATE kernel/freestanding include)
  target_compile_options(shirley-kernel PRIVATE -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-pic)
  target_link_options(shirley-kernel PRIVATE -nostdlib -static -fuse-ld=${SHIRLEY_LLD} -Wl,-T,${CMAKE_SOURCE_DIR}/platform/qemu_arm64/kernel.ld -Wl,--build-id=none)
elseif(SHIRLEY_TARGET STREQUAL x86_64)
  find_program(SHIRLEY_LLD ld.lld REQUIRED)
  find_program(SHIRLEY_OBJCOPY llvm-objcopy REQUIRED)
  # x86_64 先建立核心與開機程式，再組合成磁碟映像。
  add_library(shirley-kernel-objects OBJECT kernel/kernel_main.cpp arch/x86_64/arch.cpp platform/qemu_x86_64/console.cpp)
  target_include_directories(shirley-kernel-objects PRIVATE kernel/freestanding include)
  target_compile_options(shirley-kernel-objects PRIVATE -target x86_64-none-elf -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-pic -mno-red-zone)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shirley-kernel.elf ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin
    COMMAND ${SHIRLEY_LLD} -T ${CMAKE_SOURCE_DIR}/platform/qemu_x86_64/kernel.ld -o ${CMAKE_CURRENT_BINARY_DIR}/shirley-kernel.elf $<TARGET_OBJECTS:shirley-kernel-objects>
    COMMAND ${SHIRLEY_OBJCOPY} -O binary ${CMAKE_CURRENT_BINARY_DIR}/shirley-kernel.elf ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin
    DEPENDS shirley-kernel-objects platform/qemu_x86_64/kernel.ld
    COMMAND_EXPAND_LISTS)
  add_custom_target(shirley-kernel ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/shirley-kernel.elf)
  add_library(shirley-boot-objects OBJECT arch/x86_64/boot.S)
  target_compile_options(shirley-boot-objects PRIVATE -target x86_64-none-elf)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/boot.bin
    COMMAND ${SHIRLEY_LLD} -T ${CMAKE_SOURCE_DIR}/platform/qemu_x86_64/boot.ld -o ${CMAKE_CURRENT_BINARY_DIR}/boot.elf $<TARGET_OBJECTS:shirley-boot-objects>
    COMMAND ${SHIRLEY_OBJCOPY} -O binary ${CMAKE_CURRENT_BINARY_DIR}/boot.elf ${CMAKE_CURRENT_BINARY_DIR}/boot.bin
    DEPENDS shirley-boot-objects platform/qemu_x86_64/boot.ld
    COMMAND_EXPAND_LISTS)
  add_custom_target(shirley-boot ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/boot.bin)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shirley-x86_64.img
    COMMAND dd if=/dev/zero of=${CMAKE_CURRENT_BINARY_DIR}/shirley-x86_64.img bs=512 count=128
    COMMAND dd if=${CMAKE_CURRENT_BINARY_DIR}/boot.bin of=${CMAKE_CURRENT_BINARY_DIR}/shirley-x86_64.img bs=512 count=1 conv=notrunc
    COMMAND dd if=${CMAKE_CURRENT_BINARY_DIR}/kernel.bin of=${CMAKE_CURRENT_BINARY_DIR}/shirley-x86_64.img bs=512 seek=1 conv=notrunc
    DEPENDS shirley-boot shirley-kernel)
  add_custom_target(shirley-disk ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/shirley-x86_64.img)
else()
  # 主機目標只建置可測試的核心記憶體元件。
  add_library(shirley-kernel-core STATIC kernel/memory/page_allocator.cpp)
  target_include_directories(shirley-kernel-core PUBLIC include)
  add_executable(shirley-host-smoke tests/host_smoke.cpp)
  target_link_libraries(shirley-host-smoke PRIVATE shirley-kernel-core)
  target_include_directories(shirley-host-smoke PRIVATE include)
  enable_testing()
  add_test(NAME host-smoke COMMAND shirley-host-smoke)
endif()
