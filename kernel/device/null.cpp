#include "shirley/device.hpp"

namespace shirley::device {
namespace {

// 讀取永遠是檔案結尾：傳輸 0 個位元組而不是回報錯誤。空的輸入佇列也是這樣
// 表示的，因此使用端不需要為 null 準備另一條路徑。
//
// A read is always end of file: zero bytes transferred rather than an error.
// An empty input queue says the same thing, so a consumer needs no separate
// path for null.
io::Result null_read(Device&, void*, std::size_t) { return {0, io::Error::None}; }

// 寫入一律成功並丟棄內容，而且要回報寫進去的長度：回報 0 會讓呼叫端以為
// 裝置滿了，然後永遠重試同一段位元組。
//
// A write always succeeds, discards the bytes, and reports the full length.
// Reporting zero would make a caller believe the device is full and retry the
// same bytes forever.
io::Result null_write(Device&, const void*, std::size_t length) { return {length, io::Error::None}; }

constexpr Operations null_operations{nullptr, nullptr, null_read, null_write, nullptr};

constinit Device null_device{"null", Type::Character, null_operations};

} // namespace

bool null_initialize() { return register_device(null_device) == Status::Ok; }

} // namespace shirley::device
