#pragma once

#include <cstddef>
#include <cstdint>

// 磁碟格式用到的校驗和。
// Checksums that on-disk formats use.
namespace shirley::checksum {

// IEEE 802.3 的 CRC-32（反射多項式 0xEDB88320），與 zlib 的 crc32() 相同，GPT
// 用它檢查標頭與分割區項目表。previous 是前一段的結果，因此一份資料可以分段
// 計算：crc32(b, crc32(a)) 等於 a 與 b 接起來一次算的值。第一段傳 0。
//
// The IEEE 802.3 CRC-32 (reflected polynomial 0xEDB88320), the same as zlib's
// crc32(); GPT checks its header and partition entry array with it. previous
// is the result for the bytes before, so data can be checksummed in pieces:
// crc32(b, crc32(a)) equals the value for a and b joined. Pass 0 for the first
// piece.
std::uint32_t crc32(const void* data, std::size_t length, std::uint32_t previous = 0);

// Castagnoli 的 CRC-32C（反射多項式 0x82F63B78），與 iSCSI 及 SSE4.2 的 crc32
// 指令相同，previous 的用法與 crc32() 一樣。ext4 的 metadata_csum 用它，但存的是
// 沒有做最後反相的值，也就是這裡結果的位元反相。
//
// The Castagnoli CRC-32C (reflected polynomial 0x82F63B78), the same as iSCSI's
// and SSE4.2's crc32 instruction; previous works as for crc32(). ext4's
// metadata_csum uses it, but stores the value without the final inversion,
// which is the bitwise complement of the result here.
std::uint32_t crc32c(const void* data, std::size_t length, std::uint32_t previous = 0);

} // namespace shirley::checksum
