#include "shirley/input_queue.hpp"

namespace shirley::io {
namespace {

// 容量是 2 的冪次，因此環狀索引可以用遮罩取代除法。
// The capacity is a power of two, so the ring index is a mask rather than a
// division.
constexpr std::size_t index_mask = InputQueue::capacity - 1;
static_assert((InputQueue::capacity & index_mask) == 0, "capacity 必須是 2 的冪次 / must be a power of two");

} // namespace

bool InputQueue::push(char value) {
    const auto head = head_;
    const auto next = (head + 1) & index_mask;
    // 保留一個空格區分「滿」與「空」，否則兩者的索引會完全相同。
    // One slot is always left empty to tell "full" apart from "empty"; without
    // it the two states have identical indices.
    if (next == tail_) return false;
    buffer_[head] = value;
    // 資料必須先寫進緩衝區才能公開新的 head，消費者才不會讀到還沒寫好的位置。
    // The data has to reach the buffer before the new head is published, or
    // the consumer could read a slot that has not been written yet.
    head_ = next;
    return true;
}

std::size_t InputQueue::available() const { return (head_ - tail_) & index_mask; }
bool InputQueue::empty() const { return head_ == tail_; }
void InputQueue::clear() { tail_ = head_; }

Result InputQueue::read(void* buffer, std::size_t length) {
    if (length != 0 && buffer == nullptr) return {0, Error::InvalidArgument};
    auto* out = static_cast<char*>(buffer);
    std::size_t transferred = 0;
    while (transferred < length && tail_ != head_) {
        out[transferred++] = buffer_[tail_];
        tail_ = (tail_ + 1) & index_mask;
    }
    return {transferred, Error::None};
}

Result InputQueue::write(const void*, std::size_t) { return {0, Error::Unsupported}; }

} // namespace shirley::io
