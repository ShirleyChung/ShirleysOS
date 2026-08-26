# 確認產生的映像沒有超過載入器一次能讀取的大小。
# 靜默截斷會變成很難追查的開機失敗，因此在建置階段就中止。
#
# Check that the produced image is no larger than the loader can read in one
# go. A silent truncation turns into a boot failure that is very hard to trace,
# so fail at build time instead.
if(NOT EXISTS "${IMAGE}")
  message(FATAL_ERROR "Image was not produced: ${IMAGE}")
endif()
file(SIZE "${IMAGE}" size)
if(size GREATER MAX_BYTES)
  message(FATAL_ERROR
    "${IMAGE} is ${size} bytes but the boot loader can only read ${MAX_BYTES} bytes. "
    "Extend arch/x86_64/boot.S to issue more than one BIOS read.")
endif()
message(STATUS "Image ${IMAGE}: ${size} of ${MAX_BYTES} bytes")
