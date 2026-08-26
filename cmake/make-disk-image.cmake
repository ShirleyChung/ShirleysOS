# 組出 BIOS 可開機的磁碟映像：第 0 個磁區是開機程式，核心從第 1 個磁區開始。
# 以 CMake 自己的 cat 串接而不是 dd，讓建置流程不依賴特定作業系統的工具。
#
# Assemble a BIOS-bootable disk image: sector 0 is the boot program and the
# kernel starts at sector 1. Concatenating with CMake's own cat rather than dd
# keeps the build free of OS-specific tools.
foreach(part BOOT KERNEL)
  if(NOT EXISTS "${${part}}")
    message(FATAL_ERROR "Missing disk image part: ${${part}}")
  endif()
endforeach()

file(SIZE "${BOOT}" boot_size)
if(NOT boot_size EQUAL 512)
  message(FATAL_ERROR "Boot sector must be exactly 512 bytes but is ${boot_size}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} -E cat "${BOOT}" "${KERNEL}"
                OUTPUT_FILE "${IMAGE}" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "Failed to assemble ${IMAGE}: ${status}")
endif()

file(SIZE "${IMAGE}" image_size)
message(STATUS "Disk image ${IMAGE}: ${image_size} bytes")
