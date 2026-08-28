# 把 rootfs/ 目錄打包成唯讀的 SHRFS1 映像，並輸出成核心可以直接連結的
# C++ 位元組陣列。核心因此不需要任何外部工具或第二個磁碟裝置就有檔案系統：
# 映像本身就是核心映像的一段，開機後掛上 RAM disk 就能讀。
#
# 用法（script 模式）：
#   cmake -DROOTFS=<來源目錄> -DOUTPUT=<產生的 .cpp> -P cmake/make-rootfs.cmake
#
# Pack rootfs/ into a read-only SHRFS1 image and emit it as a C++ byte array
# the kernel can link directly. That means the file system needs no external
# tool and no second disk device: the image is simply part of the kernel image
# and becomes readable as soon as it is mounted through a RAM disk at boot.
#
# Usage (script mode):
#   cmake -DROOTFS=<source directory> -DOUTPUT=<generated .cpp> -P cmake/make-rootfs.cmake

if(NOT ROOTFS OR NOT OUTPUT)
  message(FATAL_ERROR "Usage: cmake -DROOTFS=<dir> -DOUTPUT=<file.cpp> -P make-rootfs.cmake")
endif()
if(NOT IS_DIRECTORY "${ROOTFS}")
  message(FATAL_ERROR "Root file system directory does not exist: ${ROOTFS}")
endif()

# 映像格式（全部為小端序）：
#   標頭 32 位元組： magic[8]="SHRFS1\0\0"、u32 版本、u32 項目數、
#                    u32 目錄表位移、u32 資料區位移、u64 映像大小
#   項目 80 位元組： name[56]、u32 旗標（bit0=目錄）、u32 父項目索引、
#                    u64 資料位移、u64 大小
# 項目 0 一定是根目錄，其父項目就是自己。
#
# The image format, little-endian throughout:
#   32-byte header: magic[8]="SHRFS1\0\0", u32 version, u32 entry count,
#                   u32 table offset, u32 data offset, u64 image size
#   80-byte entry:  name[56], u32 flags (bit 0 = directory), u32 parent index,
#                   u64 data offset, u64 size
# Entry 0 is always the root directory, and its parent is itself.
set(header_bytes 32)
set(entry_bytes 80)
set(name_bytes 56)
set(block_bytes 512)

# 以位元組為單位輸出數值。CMake 沒有二進位寫入，因此整份映像都以
# 「十進位位元組加逗號」的形式組成，最後直接變成 C++ 陣列的初始式。
#
# Emit a value one byte at a time. CMake cannot write binary files, so the
# whole image is assembled as comma-separated decimal bytes that become a C++
# array initializer verbatim.
function(shirley_bytes_u32 value out_variable)
  set(result "")
  foreach(shift 0 8 16 24)
    math(EXPR byte "(${value} >> ${shift}) & 255")
    string(APPEND result "${byte},")
  endforeach()
  set(${out_variable} "${result}" PARENT_SCOPE)
endfunction()

function(shirley_bytes_u64 value out_variable)
  set(result "")
  foreach(shift 0 8 16 24 32 40 48 56)
    math(EXPR byte "(${value} >> ${shift}) & 255")
    string(APPEND result "${byte},")
  endforeach()
  set(${out_variable} "${result}" PARENT_SCOPE)
endfunction()

# 把字串轉成固定長度、以零填滿的位元組欄位；過長的名稱是建置錯誤，
# 靜默截斷只會讓 ls 顯示出一個打不開的檔名。
#
# Turn a string into a fixed-length, zero-padded byte field. A name that does
# not fit is a build error: truncating it silently would only produce a name
# that ls shows but nothing can open.
function(shirley_bytes_name text out_variable)
  string(LENGTH "${text}" text_length)
  math(EXPR limit "${name_bytes} - 1")
  if(text_length GREATER limit)
    message(FATAL_ERROR "Name is longer than ${limit} bytes: ${text}")
  endif()
  set(result "")
  if(text_length GREATER 0)
    string(HEX "${text}" text_hex)
    string(REGEX REPLACE "(..)" "0x\\1," result "${text_hex}")
    # 名稱以位元組計算長度，非 ASCII 字元會佔用一個以上的位元組。
    # The field is measured in bytes, and a non-ASCII character takes more
    # than one of them.
    string(LENGTH "${text_hex}" hex_length)
    math(EXPR text_length "${hex_length} / 2")
    if(text_length GREATER limit)
      message(FATAL_ERROR "Name is longer than ${limit} bytes: ${text}")
    endif()
  endif()
  math(EXPR padding "${name_bytes} - ${text_length}")
  foreach(index RANGE 1 ${padding})
    string(APPEND result "0,")
  endforeach()
  set(${out_variable} "${result}" PARENT_SCOPE)
endfunction()

# 收集所有檔案，並補上它們所有的祖先目錄。根目錄以 "." 表示，因此清單裡
# 不會出現空字串——CMake 的清單無法可靠地保存空元素。
#
# Collect every file and add all of their ancestor directories. The root is
# spelled "." so that no element of the list is ever the empty string, which a
# CMake list cannot hold reliably.
file(GLOB_RECURSE found_files RELATIVE "${ROOTFS}" "${ROOTFS}/*")
list(SORT found_files)

set(paths ".")
set(names "/")
set(kinds 1)
set(parents 0)
set(sizes 0)
# 目錄沒有來源檔案，但清單必須逐項對齊，因此以 "-" 佔位：CMake 的清單無法
# 可靠地保存空元素，用空字串佔位會讓後面所有索引錯開一格。
# A directory has no source file, but the lists are indexed in lockstep, so it
# takes a "-" placeholder: a CMake list cannot hold an empty element reliably,
# and using one would shift every later index by one.
set(sources "-")

function(shirley_parent_of path out_variable)
  if(path MATCHES "/")
    string(REGEX REPLACE "/[^/]+$" "" parent "${path}")
  else()
    set(parent ".")
  endif()
  set(${out_variable} "${parent}" PARENT_SCOPE)
endfunction()

# 目錄必須先於自己的子項目登記，否則父項目索引會指向還不存在的位置。
# A directory has to be registered before anything inside it, or the parent
# index would point at an entry that does not exist yet.
foreach(file ${found_files})
  if(IS_DIRECTORY "${ROOTFS}/${file}")
    continue()
  endif()
  string(REPLACE "/" ";" components "${file}")
  list(LENGTH components component_count)
  math(EXPR directory_count "${component_count} - 1")
  set(prefix "")
  if(directory_count GREATER 0)
    math(EXPR last_directory "${directory_count} - 1")
    foreach(index RANGE 0 ${last_directory})
      list(GET components ${index} component)
      if(prefix STREQUAL "")
        set(prefix "${component}")
      else()
        set(prefix "${prefix}/${component}")
      endif()
      list(FIND paths "${prefix}" existing)
      if(existing EQUAL -1)
        shirley_parent_of("${prefix}" parent_path)
        list(FIND paths "${parent_path}" parent_index)
        list(APPEND paths "${prefix}")
        list(APPEND names "${component}")
        list(APPEND kinds 1)
        list(APPEND parents ${parent_index})
        list(APPEND sizes 0)
        list(APPEND sources "-")
      endif()
    endforeach()
  endif()
  list(GET components ${directory_count} leaf)
  shirley_parent_of("${file}" parent_path)
  list(FIND paths "${parent_path}" parent_index)
  file(SIZE "${ROOTFS}/${file}" file_size)
  list(APPEND paths "${file}")
  list(APPEND names "${leaf}")
  list(APPEND kinds 0)
  list(APPEND parents ${parent_index})
  list(APPEND sizes ${file_size})
  list(APPEND sources "${ROOTFS}/${file}")
endforeach()

list(LENGTH paths entry_count)
math(EXPR table_offset "${header_bytes}")
math(EXPR data_offset "${table_offset} + ${entry_count} * ${entry_bytes}")
# 資料區對齊區塊邊界，讓每個檔案的第一個位元組都落在乾淨的區塊位移上。
# Align the data region to a block boundary so every file's first byte sits at
# a clean block offset.
math(EXPR remainder "${data_offset} % ${block_bytes}")
if(NOT remainder EQUAL 0)
  math(EXPR data_offset "${data_offset} + ${block_bytes} - ${remainder}")
endif()

# 先算出每個檔案的資料位移，再開始輸出位元組。
# Work out every file's data offset before emitting a single byte.
set(offsets "")
set(cursor ${data_offset})
math(EXPR last_entry "${entry_count} - 1")
foreach(index RANGE 0 ${last_entry})
  list(GET kinds ${index} kind)
  if(kind EQUAL 1)
    list(APPEND offsets 0)
  else()
    list(APPEND offsets ${cursor})
    list(GET sizes ${index} size)
    math(EXPR cursor "${cursor} + ${size}")
  endif()
endforeach()

set(image_size ${cursor})
math(EXPR remainder "${image_size} % ${block_bytes}")
if(NOT remainder EQUAL 0)
  math(EXPR image_size "${image_size} + ${block_bytes} - ${remainder}")
endif()

# 標頭。 / The header.
set(image "0x53,0x48,0x52,0x46,0x53,0x31,0,0,")
shirley_bytes_u32(1 field)
string(APPEND image "${field}")
shirley_bytes_u32(${entry_count} field)
string(APPEND image "${field}")
shirley_bytes_u32(${table_offset} field)
string(APPEND image "${field}")
shirley_bytes_u32(${data_offset} field)
string(APPEND image "${field}")
shirley_bytes_u64(${image_size} field)
string(APPEND image "${field}")

# 目錄表。 / The entry table.
foreach(index RANGE 0 ${last_entry})
  list(GET names ${index} name)
  list(GET kinds ${index} kind)
  list(GET parents ${index} parent)
  list(GET offsets ${index} offset)
  list(GET sizes ${index} size)
  if(index EQUAL 0)
    # 根目錄沒有名字；它的路徑就是 "/"。
    # The root has no name of its own; its path is simply "/".
    shirley_bytes_name("" field)
  else()
    shirley_bytes_name("${name}" field)
  endif()
  string(APPEND image "${field}")
  shirley_bytes_u32(${kind} field)
  string(APPEND image "${field}")
  shirley_bytes_u32(${parent} field)
  string(APPEND image "${field}")
  shirley_bytes_u64(${offset} field)
  string(APPEND image "${field}")
  shirley_bytes_u64(${size} field)
  string(APPEND image "${field}")
endforeach()

# 目錄表與資料區之間的對齊填充。
# The alignment padding between the entry table and the data region.
math(EXPR table_end "${table_offset} + ${entry_count} * ${entry_bytes}")
math(EXPR padding "${data_offset} - ${table_end}")
if(padding GREATER 0)
  foreach(index RANGE 1 ${padding})
    string(APPEND image "0,")
  endforeach()
endif()

# 檔案內容。 / The file contents.
foreach(index RANGE 0 ${last_entry})
  list(GET kinds ${index} kind)
  if(kind EQUAL 1)
    continue()
  endif()
  list(GET sources ${index} source)
  list(GET sizes ${index} size)
  if(size GREATER 0)
    file(READ "${source}" content HEX)
    string(REGEX REPLACE "(..)" "0x\\1," content "${content}")
    string(APPEND image "${content}")
  endif()
endforeach()

# 尾端補零到區塊邊界，區塊裝置才能讀到最後一個位元組。
# Zero-pad the tail to a block boundary so the block device can reach the very
# last byte.
math(EXPR padding "${image_size} - ${cursor}")
if(padding GREATER 0)
  foreach(index RANGE 1 ${padding})
    string(APPEND image "0,")
  endforeach()
endif()

# 讓產生的原始碼可讀：每行 16 個位元組。
# Keep the generated source readable at sixteen bytes per line.
string(REGEX REPLACE "(([^,]+,){16})" "\\1\n    " image "${image}")

set(listing "")
foreach(index RANGE 0 ${last_entry})
  list(GET paths ${index} path)
  list(GET kinds ${index} kind)
  list(GET sizes ${index} size)
  if(index EQUAL 0)
    string(APPEND listing "//   / (directory)\n")
  elseif(kind EQUAL 1)
    string(APPEND listing "//   /${path} (directory)\n")
  else()
    string(APPEND listing "//   /${path} (${size} bytes)\n")
  endif()
endforeach()

file(WRITE "${OUTPUT}"
"// 由 cmake/make-rootfs.cmake 從 ${ROOTFS} 產生，請勿手動編輯。\n"
"// Generated from ${ROOTFS} by cmake/make-rootfs.cmake. Do not edit.\n"
"//\n"
"${listing}"
"\n"
"#include \"shirley/rootfs.hpp\"\n"
"\n"
"namespace shirley::fs {\n"
"namespace {\n"
"// 映像必須可寫入，區塊裝置介面才能原封不動地套用在它上面；對齊到區塊大小\n"
"// 則讓每一次區塊讀取都落在自然對齊的位址上。\n"
"// The image is writable so the block device interface applies to it\n"
"// unchanged, and block-aligned so every block read lands on a naturally\n"
"// aligned address.\n"
"alignas(512) unsigned char image[] = {\n    ${image}\n};\n"
"} // namespace\n"
"\n"
"void* rootfs_image() { return image; }\n"
"std::size_t rootfs_image_size() { return sizeof(image); }\n"
"\n"
"} // namespace shirley::fs\n")

message(STATUS "Root file system ${OUTPUT}: ${entry_count} entries, ${image_size} bytes")
