# 宣告最低 CMake 版本與本專案使用的語言。
# Declare the minimum CMake version and the languages this project uses.
cmake_minimum_required(VERSION 3.20)
project(ShirleyOS LANGUAGES C CXX ASM)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)

# CPU 架構與機器平台是兩個獨立的維度。SHIRLEY_TARGET 只是常用組合的簡寫，
# 需要新組合時可以直接指定 SHIRLEY_ARCH 與 SHIRLEY_PLATFORM。
#
# CPU architecture and machine platform are independent axes. SHIRLEY_TARGET is
# only a shorthand for the common pairs; a new combination can set
# SHIRLEY_ARCH and SHIRLEY_PLATFORM directly.
if(NOT SHIRLEY_TARGET)
  set(SHIRLEY_TARGET host)
endif()
if(NOT SHIRLEY_ARCH OR NOT SHIRLEY_PLATFORM)
  if(SHIRLEY_TARGET STREQUAL host)
    set(SHIRLEY_ARCH host)
    set(SHIRLEY_PLATFORM host)
  elseif(SHIRLEY_TARGET STREQUAL x86_64)
    set(SHIRLEY_ARCH x86_64)
    set(SHIRLEY_PLATFORM qemu_x86_64)
  elseif(SHIRLEY_TARGET STREQUAL arm64)
    set(SHIRLEY_ARCH arm64)
    set(SHIRLEY_PLATFORM qemu_arm64)
  elseif(SHIRLEY_TARGET STREQUAL apple_silicon)
    set(SHIRLEY_ARCH arm64)
    set(SHIRLEY_PLATFORM apple_silicon)
  else()
    message(FATAL_ERROR "Unknown SHIRLEY_TARGET: ${SHIRLEY_TARGET}")
  endif()
endif()
message(STATUS "ShirleyOS architecture=${SHIRLEY_ARCH} platform=${SHIRLEY_PLATFORM}")

# 與架構、平台都無關的核心程式碼。
# Kernel code that depends on neither the architecture nor the platform.
set(SHIRLEY_KERNEL_SOURCES
  kernel/kernel_main.cpp
  kernel/format/number.cpp
  kernel/io/console_stream.cpp
  kernel/io/standard_streams.cpp
  kernel/memory/page_allocator.cpp
  kernel/process/scheduler.cpp
  kernel/runtime/compiler_runtime.cpp)

# 韌體資料格式，可被多個機器平台共用。
# Firmware data formats, shared by several machine platforms.
set(SHIRLEY_FIRMWARE_E820 platform/firmware/e820.cpp)
set(SHIRLEY_FIRMWARE_FDT platform/firmware/fdt.cpp)
set(SHIRLEY_FIRMWARE_APPLE platform/firmware/apple_boot_args.cpp)

if(SHIRLEY_ARCH STREQUAL host)
  # 主機目標只建置可以在開發機上測試的架構中立元件。
  # The host target builds only the architecture-neutral pieces that can be
  # tested on the development machine.
  add_library(shirley-kernel-core STATIC
    kernel/format/number.cpp
    kernel/io/standard_streams.cpp
    kernel/memory/page_allocator.cpp
    kernel/process/scheduler.cpp
    drivers/block/ram_disk.cpp
    ${SHIRLEY_FIRMWARE_E820}
    ${SHIRLEY_FIRMWARE_FDT}
    ${SHIRLEY_FIRMWARE_APPLE})
  target_include_directories(shirley-kernel-core PUBLIC include)

  enable_testing()
  foreach(test host_smoke core_services_smoke platform_model_smoke)
    string(REPLACE "_" "-" test_name ${test})
    add_executable(shirley-${test_name} tests/${test}.cpp)
    target_link_libraries(shirley-${test_name} PRIVATE shirley-kernel-core)
    target_include_directories(shirley-${test_name} PRIVATE include)
    add_test(NAME ${test_name} COMMAND shirley-${test_name})
  endforeach()
  return()
endif()

find_program(SHIRLEY_LLD ld.lld REQUIRED)
find_program(SHIRLEY_OBJCOPY llvm-objcopy REQUIRED)

# 架構層原始碼。
# Architecture layer sources.
if(SHIRLEY_ARCH STREQUAL x86_64)
  set(SHIRLEY_ARCH_SOURCES
    arch/x86_64/arch.cpp
    arch/x86_64/cpu.cpp
    arch/x86_64/gdt.cpp
    arch/x86_64/idt.cpp
    arch/x86_64/paging.cpp
    arch/x86_64/entry.S
    arch/x86_64/interrupt.S
    arch/x86_64/segment.S)
  # 核心不使用浮點運算，關閉 SSE 可避免編譯器在例外處理前產生 SIMD 指令。
  # The kernel uses no floating point, and disabling SSE stops the compiler
  # emitting SIMD instructions before exception handling is ready.
  set(SHIRLEY_ARCH_FLAGS -mno-red-zone -mno-sse -mno-sse2 -mno-mmx)
elseif(SHIRLEY_ARCH STREQUAL arm64)
  set(SHIRLEY_ARCH_SOURCES
    arch/arm64/arch.cpp
    arch/arm64/cpu.cpp
    arch/arm64/exception.cpp
    arch/arm64/paging.cpp
    arch/arm64/entry.S
    arch/arm64/exception.S)
  # 同樣的理由：核心程式碼不得使用 FP/SIMD 暫存器。
  # Same reason: kernel code must not touch the FP/SIMD registers.
  set(SHIRLEY_ARCH_FLAGS -mgeneral-regs-only)
else()
  message(FATAL_ERROR "Unknown SHIRLEY_ARCH: ${SHIRLEY_ARCH}")
endif()

# 平台層原始碼；每個平台自行決定要使用哪些韌體格式。
# Platform layer sources; each platform picks the firmware formats it needs.
if(SHIRLEY_PLATFORM STREQUAL qemu_x86_64)
  set(SHIRLEY_PLATFORM_SOURCES
    platform/qemu_x86_64/boot_protocol.cpp
    platform/qemu_x86_64/console.cpp
    platform/qemu_x86_64/platform.cpp
    platform/pc/pic.cpp
    ${SHIRLEY_FIRMWARE_E820})
elseif(SHIRLEY_PLATFORM STREQUAL qemu_arm64)
  set(SHIRLEY_PLATFORM_SOURCES
    platform/qemu_arm64/boot_protocol.cpp
    platform/qemu_arm64/console.cpp
    platform/qemu_arm64/platform.cpp
    ${SHIRLEY_FIRMWARE_FDT})
elseif(SHIRLEY_PLATFORM STREQUAL apple_silicon)
  set(SHIRLEY_PLATFORM_SOURCES
    platform/apple_silicon/boot_protocol.cpp
    platform/apple_silicon/console.cpp
    platform/apple_silicon/interrupt_controller.cpp
    platform/apple_silicon/platform.cpp
    ${SHIRLEY_FIRMWARE_APPLE})
else()
  message(FATAL_ERROR "Unknown SHIRLEY_PLATFORM: ${SHIRLEY_PLATFORM}")
endif()
set(SHIRLEY_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/platform/${SHIRLEY_PLATFORM}/kernel.ld)

set(SHIRLEY_KERNEL_FLAGS
  -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-pic
  -Wall -Wextra ${SHIRLEY_ARCH_FLAGS})

add_library(shirley-kernel-objects OBJECT
  ${SHIRLEY_KERNEL_SOURCES} ${SHIRLEY_ARCH_SOURCES} ${SHIRLEY_PLATFORM_SOURCES})
target_include_directories(shirley-kernel-objects PRIVATE
  kernel/freestanding include arch/${SHIRLEY_ARCH} platform/${SHIRLEY_PLATFORM})
target_compile_options(shirley-kernel-objects PRIVATE
  $<$<COMPILE_LANGUAGE:C,CXX>:${SHIRLEY_KERNEL_FLAGS}>)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shirley-kernel.elf
  COMMAND ${SHIRLEY_LLD} -T ${SHIRLEY_LINKER_SCRIPT} --build-id=none
          -o ${CMAKE_CURRENT_BINARY_DIR}/shirley-kernel.elf $<TARGET_OBJECTS:shirley-kernel-objects>
  # 目標名稱只建立順序相依；要讓物件檔更新後真的重新連結，
  # 必須把物件檔本身也列為相依。
  # The target name only creates an ordering dependency; the object files
  # themselves must be listed so an updated object actually relinks.
  DEPENDS shirley-kernel-objects $<TARGET_OBJECTS:shirley-kernel-objects>
          ${SHIRLEY_LINKER_SCRIPT}
  COMMAND_EXPAND_LISTS)
add_custom_target(shirley-kernel ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/shirley-kernel.elf)

if(SHIRLEY_PLATFORM STREQUAL qemu_x86_64)
  # x86_64 走 BIOS 開機磁區，因此核心要轉成純二進位再組成磁碟映像。
  # x86_64 boots through a BIOS boot sector, so the kernel becomes a flat
  # binary that is then assembled into a disk image.
  add_library(shirley-boot-objects OBJECT arch/x86_64/boot.S)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/boot.bin
    COMMAND ${SHIRLEY_LLD} -T ${CMAKE_SOURCE_DIR}/platform/qemu_x86_64/boot.ld --build-id=none
            -o ${CMAKE_CURRENT_BINARY_DIR}/boot.elf $<TARGET_OBJECTS:shirley-boot-objects>
    COMMAND ${SHIRLEY_OBJCOPY} -O binary ${CMAKE_CURRENT_BINARY_DIR}/boot.elf
            ${CMAKE_CURRENT_BINARY_DIR}/boot.bin
    DEPENDS shirley-boot-objects $<TARGET_OBJECTS:shirley-boot-objects>
            ${CMAKE_SOURCE_DIR}/platform/qemu_x86_64/boot.ld
    COMMAND_EXPAND_LISTS)

  # 開機磁區以單次 BIOS 呼叫讀取 127 個磁區，因此核心固定補零到那個大小；
  # 內容超出時 objcopy 會產生更大的檔案，由大小檢查及早失敗。
  # The boot sector reads 127 sectors in a single BIOS call, so the kernel is
  # always padded to exactly that size. If the content overflows, objcopy
  # produces a larger file and the size check fails the build.
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin
    COMMAND ${SHIRLEY_OBJCOPY} -O binary --pad-to=0x1fe00
            ${CMAKE_CURRENT_BINARY_DIR}/shirley-kernel.elf ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin
    COMMAND ${CMAKE_COMMAND} -DIMAGE=${CMAKE_CURRENT_BINARY_DIR}/kernel.bin -DMAX_BYTES=65024
            -P ${CMAKE_SOURCE_DIR}/cmake/check-image-size.cmake
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/shirley-kernel.elf
            ${CMAKE_SOURCE_DIR}/cmake/check-image-size.cmake)

  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shirley-x86_64.img
    COMMAND ${CMAKE_COMMAND} -DBOOT=${CMAKE_CURRENT_BINARY_DIR}/boot.bin
            -DKERNEL=${CMAKE_CURRENT_BINARY_DIR}/kernel.bin
            -DIMAGE=${CMAKE_CURRENT_BINARY_DIR}/shirley-x86_64.img
            -P ${CMAKE_SOURCE_DIR}/cmake/make-disk-image.cmake
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/boot.bin ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin
            ${CMAKE_SOURCE_DIR}/cmake/make-disk-image.cmake)
  add_custom_target(shirley-disk ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/shirley-x86_64.img)
endif()
