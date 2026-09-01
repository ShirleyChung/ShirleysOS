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
  elseif(SHIRLEY_TARGET STREQUAL x86_64_uefi)
    set(SHIRLEY_ARCH x86_64)
    set(SHIRLEY_PLATFORM qemu_x86_64_uefi)
  elseif(SHIRLEY_TARGET STREQUAL arm64)
    set(SHIRLEY_ARCH arm64)
    set(SHIRLEY_PLATFORM qemu_arm64)
  elseif(SHIRLEY_TARGET STREQUAL arm64_uefi)
    set(SHIRLEY_ARCH arm64)
    set(SHIRLEY_PLATFORM qemu_arm64_uefi)
  elseif(SHIRLEY_TARGET STREQUAL apple_silicon)
    set(SHIRLEY_ARCH arm64)
    set(SHIRLEY_PLATFORM apple_silicon)
  else()
    message(FATAL_ERROR "Unknown SHIRLEY_TARGET: ${SHIRLEY_TARGET}")
  endif()
endif()
message(STATUS "ShirleyOS architecture=${SHIRLEY_ARCH} platform=${SHIRLEY_PLATFORM}")

# 根檔案系統：rootfs/ 在建置時被打包成唯讀映像，並以 C++ 位元組陣列的形式
# 產生出來，因此核心不需要磁碟驅動程式就有檔案系統可掛載。主機建置也用同一
# 份產生的映像，測試才能檢驗真正會開機的那個映像。
#
# The root file system: rootfs/ is packed into a read-only image at build time
# and emitted as a C++ byte array, so the kernel needs no disk driver to have a
# file system to mount. The host build uses the same generated image, which is
# what lets the tests check the very image that boots.
set(SHIRLEY_ROOTFS_DIR ${CMAKE_SOURCE_DIR}/rootfs)
set(SHIRLEY_ROOTFS_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/rootfs_image.cpp)
file(GLOB_RECURSE SHIRLEY_ROOTFS_FILES ${SHIRLEY_ROOTFS_DIR}/*)

# 核心目標會把編出來的 user 程式一起打包成 /bin/hello，因此開機之後那個程式
# 是檔案系統裡的一個檔案，由 VFS 讀出、交給 ELF loader 執行，而不是連結進
# 核心的一段位元組。它是建置產物，所以不放在 rootfs/ 裡；每個架構編出來的
# 內容也本來就不一樣。
#
# 主機建置沒有交叉工具鏈，編不出 user 程式，因此它的映像就是 rootfs/ 本身。
# 主機測試檢查的是那些內容；/bin/hello 這條路由 QEMU 的開機測試驗證。
#
# A kernel target packs the user program it builds in as /bin/hello, so after
# boot that program is a file in the file system, read by the VFS and handed to
# the ELF loader rather than linked into the kernel. It is a build product, so
# it does not live in rootfs/, and each architecture builds a different one
# anyway.
#
# The host build has no cross toolchain and builds no user program, so its
# image is rootfs/ exactly. The host tests check that content; the /bin/hello
# path is covered by the QEMU boot tests.
set(SHIRLEY_ROOTFS_EXTRA)
set(SHIRLEY_ROOTFS_EXTRA_FILES)
if(NOT SHIRLEY_ARCH STREQUAL host)
  set(SHIRLEY_USER_DIR ${CMAKE_CURRENT_BINARY_DIR}/user-hello)
  set(SHIRLEY_USER_ELF ${SHIRLEY_USER_DIR}/hello.elf)
  set(SHIRLEY_INIT_DIR ${CMAKE_CURRENT_BINARY_DIR}/user-init)
  set(SHIRLEY_INIT_ELF ${SHIRLEY_INIT_DIR}/init.elf)
  # Use a pipe in the command-line value; a literal semicolon is a CMake list
  # separator and would be emitted as a second shell command by Ninja.
  set(SHIRLEY_ROOTFS_EXTRA "bin/hello=${SHIRLEY_USER_ELF},bin/init=${SHIRLEY_INIT_ELF}")
  set(SHIRLEY_ROOTFS_EXTRA_FILES ${SHIRLEY_USER_ELF} ${SHIRLEY_INIT_ELF})
endif()

add_custom_command(OUTPUT ${SHIRLEY_ROOTFS_SOURCE}
  COMMAND ${CMAKE_COMMAND} -DROOTFS=${SHIRLEY_ROOTFS_DIR} -DOUTPUT=${SHIRLEY_ROOTFS_SOURCE}
          "-DEXTRA=${SHIRLEY_ROOTFS_EXTRA}"
          -P ${CMAKE_SOURCE_DIR}/cmake/make-rootfs.cmake
  DEPENDS ${SHIRLEY_ROOTFS_FILES} ${SHIRLEY_ROOTFS_EXTRA_FILES}
          ${CMAKE_SOURCE_DIR}/cmake/make-rootfs.cmake
  COMMENT "Packing rootfs/ into a SHRFS1 image")

# 與架構、平台都無關的核心程式碼。
# Kernel code that depends on neither the architecture nor the platform.
set(SHIRLEY_KERNEL_SOURCES
  kernel/console/console.cpp
  kernel/console/console_input.cpp
  kernel/device/device.cpp
  kernel/device/null.cpp
  boot/common/elf64.cpp
  kernel/elf/user_loader.cpp
  kernel/user/launch.cpp
  kernel/syscall/syscall.cpp
  kernel/kernel_main.cpp
  kernel/format/number.cpp
  kernel/fs/shrfs.cpp
  kernel/fs/shrfs_vfs.cpp
  kernel/fs/devfs.cpp
  kernel/fs/vfs.cpp
  kernel/fs/rootfs_mount.cpp
  kernel/interrupt/irq.cpp
  kernel/io/console_stream.cpp
  kernel/io/input_queue.cpp
  kernel/io/standard_streams.cpp
  kernel/memory/page_allocator.cpp
  kernel/process/process.cpp
  kernel/process/scheduler.cpp
  kernel/runtime/compiler_runtime.cpp
  kernel/shell/shell.cpp
  kernel/text/string.cpp
  drivers/block/ram_disk.cpp
  ${SHIRLEY_ROOTFS_SOURCE})

# 與硬體無關的裝置驅動程式邏輯，主機建置與核心建置共用。
# Hardware-independent device driver logic, shared by the host build and the
# kernel build.
set(SHIRLEY_DRIVER_SOURCES
  drivers/input/scancode.cpp)

# 韌體資料格式，可被多個機器平台共用。
# Firmware data formats, shared by several machine platforms.
set(SHIRLEY_FIRMWARE_E820 platform/firmware/e820.cpp)
set(SHIRLEY_FIRMWARE_FDT platform/firmware/fdt.cpp)
set(SHIRLEY_FIRMWARE_APPLE platform/firmware/apple_boot_args.cpp)
set(SHIRLEY_FIRMWARE_UEFI platform/firmware/uefi.cpp)

if(SHIRLEY_ARCH STREQUAL host)
  # 主機目標只建置可以在開發機上測試的架構中立元件。
  # The host target builds only the architecture-neutral pieces that can be
  # tested on the development machine.
  add_library(shirley-kernel-core STATIC
    kernel/console/console.cpp
    kernel/console/console_input.cpp
    kernel/device/device.cpp
    kernel/device/null.cpp
    kernel/elf/user_loader.cpp
    kernel/format/number.cpp
    kernel/fs/shrfs.cpp
    kernel/fs/shrfs_vfs.cpp
    kernel/fs/devfs.cpp
    kernel/fs/vfs.cpp
    kernel/fs/rootfs_mount.cpp
    kernel/io/input_queue.cpp
    kernel/io/standard_streams.cpp
    kernel/memory/page_allocator.cpp
    kernel/process/scheduler.cpp
    kernel/text/string.cpp
    drivers/block/ram_disk.cpp
    ${SHIRLEY_ROOTFS_SOURCE}
    ${SHIRLEY_DRIVER_SOURCES}
    boot/common/elf64.cpp
    ${SHIRLEY_FIRMWARE_E820}
    ${SHIRLEY_FIRMWARE_FDT}
    ${SHIRLEY_FIRMWARE_APPLE}
    ${SHIRLEY_FIRMWARE_UEFI})
  target_include_directories(shirley-kernel-core PUBLIC include)
  target_compile_definitions(shirley-kernel-core PRIVATE SHIRLEY_HOST)

  enable_testing()
  foreach(test host_smoke core_services_smoke platform_model_smoke boot_loader_smoke input_smoke
               file_system_smoke device_smoke vfs_smoke)
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
  # UEFI 應用程式使用 Microsoft x64 呼叫慣例，因此以 windows 目標編譯。
  # A UEFI application uses the Microsoft x64 calling convention, so it is
  # compiled for a windows target.
  set(SHIRLEY_EFI_TARGET x86_64-unknown-windows)
  set(SHIRLEY_EFI_MACHINE x64)
  set(SHIRLEY_EFI_FLAGS -mno-red-zone)
  set(SHIRLEY_EFI_NAME BOOTX64.EFI)
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
  set(SHIRLEY_EFI_TARGET aarch64-unknown-windows)
  set(SHIRLEY_EFI_MACHINE arm64)
  set(SHIRLEY_EFI_FLAGS)
  set(SHIRLEY_EFI_NAME BOOTAA64.EFI)
else()
  message(FATAL_ERROR "Unknown SHIRLEY_ARCH: ${SHIRLEY_ARCH}")
endif()

# 最小 user 程式以靜態 ELF 形式建置，只連接 ShirleyOS libc 與 syscall trampoline。
# Build the minimal user program as a static ELF, linking only ShirleyOS libc
# and the architecture syscall trampoline.
if(SHIRLEY_ARCH STREQUAL x86_64)
  set(SHIRLEY_USER_TARGET x86_64-none-elf)
  set(SHIRLEY_USER_START ${CMAKE_SOURCE_DIR}/libc/src/start_x86_64.S)
  set(SHIRLEY_USER_LINKER ${CMAKE_SOURCE_DIR}/user/hello/x86_64.ld)
  set(SHIRLEY_USER_SYSCALL ${CMAKE_SOURCE_DIR}/libc/arch/x86_64/syscall.S)
else()
  set(SHIRLEY_USER_TARGET aarch64-none-elf)
  set(SHIRLEY_USER_START ${CMAKE_SOURCE_DIR}/libc/src/start_arm64.S)
  set(SHIRLEY_USER_LINKER ${CMAKE_SOURCE_DIR}/user/hello/arm64.ld)
  set(SHIRLEY_USER_SYSCALL ${CMAKE_SOURCE_DIR}/libc/arch/arm64/syscall.S)
endif()

function(shirley_user_program name main_source output directory)
  set(objects)
  foreach(source
      ${main_source}
      ${CMAKE_SOURCE_DIR}/libc/src/stdio/printf.c
      ${CMAKE_SOURCE_DIR}/libc/src/unistd/write.c
      ${CMAKE_SOURCE_DIR}/libc/src/unistd/read.c
      ${CMAKE_SOURCE_DIR}/libc/src/unistd/open.c
      ${CMAKE_SOURCE_DIR}/libc/src/unistd/close.c
      ${CMAKE_SOURCE_DIR}/libc/src/unistd/_exit.c
      ${SHIRLEY_USER_START}
      ${SHIRLEY_USER_SYSCALL})
    get_filename_component(stem ${source} NAME_WE)
    get_filename_component(extension ${source} EXT)
    set(object ${directory}/${stem}${extension}.o)
    add_custom_command(OUTPUT ${object}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${directory}
      COMMAND ${CMAKE_C_COMPILER} --target=${SHIRLEY_USER_TARGET}
              -ffreestanding -fno-builtin -fno-stack-protector -fno-pic
              -I${CMAKE_SOURCE_DIR}/libc/include
              -c ${source} -o ${object}
      DEPENDS ${source})
    list(APPEND objects ${object})
  endforeach()
  add_custom_command(OUTPUT ${output}
    COMMAND ${SHIRLEY_LLD} -T ${SHIRLEY_USER_LINKER} --build-id=none -z max-page-size=4096
            -o ${output} ${objects}
    DEPENDS ${objects} ${SHIRLEY_USER_LINKER})
  add_custom_target(shirley-user-${name} ALL DEPENDS ${output})
endfunction()

# -z max-page-size=4096：aarch64 的連結器預設以 64 KiB 為最大頁面大小對齊
# 節區，於是一個 2 KiB 的程式會被墊成 70 KiB 的檔案。核心的分頁一律是 4 KiB，
# 那些填充毫無用處；而現在這個 ELF 要放進根檔案系統映像，多出來的每一個
# 位元組都會跟著核心一起被載入。
#
# -z max-page-size=4096: the aarch64 linker aligns sections to a 64 KiB maximum
# page size by default, which pads a 2 KiB program into a 70 KiB file. The
# kernel pages at 4 KiB and that padding buys nothing — and now that this ELF
# goes into the root file system image, every extra byte is loaded with the
# kernel.
shirley_user_program(hello ${CMAKE_SOURCE_DIR}/user/hello/main.c ${SHIRLEY_USER_ELF} ${SHIRLEY_USER_DIR})
shirley_user_program(init ${CMAKE_SOURCE_DIR}/user/init/main.c ${SHIRLEY_INIT_ELF} ${SHIRLEY_INIT_DIR})

# 平台層原始碼；每個平台自行決定要使用哪些韌體格式與共用裝置驅動程式。
# Platform layer sources; each platform picks the firmware formats and shared
# device drivers it needs.
set(SHIRLEY_UEFI_PLATFORM OFF)
if(SHIRLEY_PLATFORM STREQUAL qemu_x86_64)
  set(SHIRLEY_PLATFORM_SOURCES
    platform/qemu_x86_64/boot_protocol.cpp
    platform/qemu_x86_64/platform.cpp
    platform/pc/serial_console.cpp
    platform/pc/serial_device.cpp
    platform/pc/serial_input.cpp
    platform/pc/pic.cpp
    platform/pc/pit.cpp
    platform/pc/ps2_keyboard.cpp
    ${SHIRLEY_DRIVER_SOURCES}
    ${SHIRLEY_FIRMWARE_E820})
elseif(SHIRLEY_PLATFORM STREQUAL qemu_x86_64_uefi)
  # 同一台 QEMU PC，只是換成 UEFI 韌體，因此沿用 platform/pc/ 的裝置驅動程式。
  # The same QEMU PC with UEFI firmware instead, so the platform/pc/ device
  # drivers are reused unchanged.
  set(SHIRLEY_PLATFORM_SOURCES
    platform/qemu_x86_64_uefi/boot_protocol.cpp
    platform/qemu_x86_64_uefi/platform.cpp
    platform/pc/serial_console.cpp
    platform/pc/serial_device.cpp
    platform/pc/serial_input.cpp
    platform/pc/pic.cpp
    platform/pc/pit.cpp
    platform/pc/ps2_keyboard.cpp
    ${SHIRLEY_DRIVER_SOURCES})
  set(SHIRLEY_UEFI_PLATFORM ON)
elseif(SHIRLEY_PLATFORM STREQUAL qemu_arm64)
  set(SHIRLEY_PLATFORM_SOURCES
    platform/qemu_arm64/boot_protocol.cpp
    platform/qemu_arm64/console.cpp
    platform/qemu_arm64/platform.cpp
    platform/arm/gicv2.cpp
    platform/arm/generic_timer.cpp
    platform/arm/pl011_input.cpp
    ${SHIRLEY_FIRMWARE_FDT})
elseif(SHIRLEY_PLATFORM STREQUAL qemu_arm64_uefi)
  # 同一台 QEMU virt，只是換成 EDK2 韌體，因此沿用同一顆 PL011 驅動程式與
  # 同一組 ARM 共用硬體驅動程式。
  # The same QEMU virt machine with EDK2 firmware instead, so the very same
  # PL011 driver and the same shared ARM hardware drivers are reused.
  set(SHIRLEY_PLATFORM_SOURCES
    platform/qemu_arm64_uefi/boot_protocol.cpp
    platform/qemu_arm64/console.cpp
    platform/qemu_arm64_uefi/platform.cpp
    platform/arm/gicv2.cpp
    platform/arm/generic_timer.cpp
    platform/arm/pl011_input.cpp)
  set(SHIRLEY_UEFI_PLATFORM ON)
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

# -fno-threadsafe-statics：函式內的 static 物件預設會產生 __cxa_guard_* 呼叫，
# 那是 libstdc++ 的執行期常式，裸機核心沒有可連結的版本。核心目前只在單一
# 核心上執行，第一次初始化不可能被另一個執行緒插隊，因此關掉這層保護是安全
# 的；等到有第二顆核心時，這個假設要跟著重新檢視。
#
# -fno-threadsafe-statics: a function-local static object otherwise emits
# __cxa_guard_* calls, which are libstdc++ runtime routines a freestanding
# kernel has no implementation to link against. The kernel runs on a single
# core today, so no other thread can race the first initialization and dropping
# the guard is safe. That assumption needs revisiting when a second core
# appears.
set(SHIRLEY_KERNEL_FLAGS
  -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-pic
  -fno-threadsafe-statics
  -Wall -Wextra ${SHIRLEY_ARCH_FLAGS})

# 開啟後鍵盤驅動程式會印出每一個原始掃描碼。預設關閉：正常開機的主控台
# 不應該被逐鍵的除錯訊息淹沒。
#
# Turning this on makes the keyboard driver print every raw scancode. It is off
# by default: an ordinary boot's console should not be buried under a debug
# line per keystroke.
option(SHIRLEY_DEBUG_SCANCODES "Log every raw PS/2 scancode" OFF)
if(SHIRLEY_DEBUG_SCANCODES)
  list(APPEND SHIRLEY_KERNEL_FLAGS -DSHIRLEY_DEBUG_SCANCODES)
endif()

add_library(shirley-kernel-objects OBJECT
  ${SHIRLEY_KERNEL_SOURCES} ${SHIRLEY_ARCH_SOURCES} ${SHIRLEY_PLATFORM_SOURCES})
if(SHIRLEY_ARCH STREQUAL x86_64)
  target_compile_definitions(shirley-kernel-objects PRIVATE SHIRLEY_ARCH_X86_64)
else()
  target_compile_definitions(shirley-kernel-objects PRIVATE SHIRLEY_ARCH_ARM64)
endif()
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

  # 開機磁區分四次 BIOS 呼叫讀取 256 個磁區，因此核心固定補零到 128 KiB；
  # 內容超出時 objcopy 會產生更大的檔案，由大小檢查及早失敗。這裡的數字必須
  # 與 arch/x86_64/boot.S 的 sectors_per_read 與 kernel_reads 一致。
  # The boot sector reads 256 sectors in four BIOS calls, so the kernel is
  # always padded to exactly 128 KiB. If the content overflows, objcopy
  # produces a larger file and the size check fails the build. These numbers
  # must agree with sectors_per_read and kernel_reads in arch/x86_64/boot.S.
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin
    COMMAND ${SHIRLEY_OBJCOPY} -O binary --pad-to=0x30000
            ${CMAKE_CURRENT_BINARY_DIR}/shirley-kernel.elf ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin
    COMMAND ${CMAKE_COMMAND} -DIMAGE=${CMAKE_CURRENT_BINARY_DIR}/kernel.bin -DMAX_BYTES=131072
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

if(SHIRLEY_UEFI_PLATFORM)
  # UEFI 應用程式是 PE32+ 而不是 ELF，使用的呼叫慣例也和核心不同，因此以另一組
  # 編譯器目標與連結器單獨建置，不能放進核心的物件庫。
  # A UEFI application is PE32+ rather than ELF and uses a different calling
  # convention from the kernel, so it is built with its own compiler target and
  # linker instead of joining the kernel's object library.
  find_program(SHIRLEY_LLD_LINK lld-link REQUIRED)

  set(SHIRLEY_EFI_SOURCES
    boot/uefi/main.cpp
    boot/common/elf64.cpp
    boot/common/runtime.cpp
    ${SHIRLEY_FIRMWARE_UEFI})
  set(SHIRLEY_EFI_OBJECTS)
  foreach(source ${SHIRLEY_EFI_SOURCES})
    get_filename_component(stem ${source} NAME_WE)
    set(object ${CMAKE_CURRENT_BINARY_DIR}/efi-${stem}.obj)
    add_custom_command(OUTPUT ${object}
      COMMAND ${CMAKE_CXX_COMPILER} --target=${SHIRLEY_EFI_TARGET} -std=c++20
              -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fshort-wchar
              -Wall -Wextra ${SHIRLEY_EFI_FLAGS}
              -I${CMAKE_SOURCE_DIR}/kernel/freestanding -I${CMAKE_SOURCE_DIR}/include
              -I${CMAKE_SOURCE_DIR}/boot/uefi
              -c ${CMAKE_SOURCE_DIR}/${source} -o ${object}
      DEPENDS ${CMAKE_SOURCE_DIR}/${source})
    list(APPEND SHIRLEY_EFI_OBJECTS ${object})
  endforeach()

  set(SHIRLEY_EFI_IMAGE ${CMAKE_CURRENT_BINARY_DIR}/${SHIRLEY_EFI_NAME})
  add_custom_command(OUTPUT ${SHIRLEY_EFI_IMAGE}
    COMMAND ${SHIRLEY_LLD_LINK} -subsystem:efi_application -entry:efi_main -nodefaultlib
            -machine:${SHIRLEY_EFI_MACHINE} -out:${SHIRLEY_EFI_IMAGE} ${SHIRLEY_EFI_OBJECTS}
    DEPENDS ${SHIRLEY_EFI_OBJECTS})

  # QEMU 可以直接把一個目錄當成 FAT 磁碟區提供給韌體，因此開發時不需要真的
  # 產生 FAT 映像；ESP 就是這個目錄的內容。
  # QEMU can serve a directory to the firmware as a FAT volume, so development
  # needs no real FAT image; the ESP is simply this directory.
  set(SHIRLEY_ESP ${CMAKE_CURRENT_BINARY_DIR}/esp)
  add_custom_command(OUTPUT ${SHIRLEY_ESP}/EFI/BOOT/${SHIRLEY_EFI_NAME}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHIRLEY_ESP}/EFI/BOOT
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHIRLEY_ESP}/shirley
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHIRLEY_EFI_IMAGE}
            ${SHIRLEY_ESP}/EFI/BOOT/${SHIRLEY_EFI_NAME}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_BINARY_DIR}/shirley-kernel.elf
            ${SHIRLEY_ESP}/shirley/kernel.elf
    DEPENDS ${SHIRLEY_EFI_IMAGE} ${CMAKE_CURRENT_BINARY_DIR}/shirley-kernel.elf)
  add_custom_target(shirley-esp ALL DEPENDS ${SHIRLEY_ESP}/EFI/BOOT/${SHIRLEY_EFI_NAME})
endif()
