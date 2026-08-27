#pragma once

#include "shirley/io.hpp"

namespace shirley::io {

// 中斷處理常式與一般核心程式之間的字元佇列，本身就是一個可讀的位元組串流，
// 因此可以直接當成標準輸入。
//
// 只有一個生產者（中斷處理常式）與一個消費者（一般核心程式）：生產者只寫
// head，消費者只寫 tail，兩者都只讀對方寫的那一個索引，所以不需要鎖。索引
// 標記為 volatile，避免編譯器把中斷會改變的值快取在暫存器裡。
//
// A character queue between an interrupt handler and ordinary kernel code.
// It is itself a readable byte stream, so it can serve as standard input
// directly.
//
// There is exactly one producer (the interrupt handler) and one consumer
// (ordinary kernel code): the producer only writes head, the consumer only
// writes tail, and each only reads the index the other writes, so no lock is
// needed. The indices are volatile so the compiler cannot cache in a register
// a value an interrupt can change.
class InputQueue final : public ByteStream {
public:
    static constexpr std::size_t capacity = 256;

    // 由中斷處理常式呼叫。佇列已滿時捨棄這個字元並回傳 false；生產者不能
    // 去動消費者持有的 tail，否則就破壞了免鎖的前提。
    //
    // Called from an interrupt handler. A full queue drops the character and
    // returns false: the producer must not touch tail, which the consumer
    // owns, or the lock-free premise no longer holds.
    bool push(char value);

    std::size_t available() const;
    bool empty() const;
    void clear();

    // 取出目前已排入的字元，不會等待。佇列為空時回傳 0 個位元組而不是錯誤。
    // Take whatever characters are queued without waiting. An empty queue
    // yields zero bytes rather than an error.
    Result read(void* buffer, std::size_t length) override;
    // 佇列只用於輸入。
    // The queue is input-only.
    Result write(const void* buffer, std::size_t length) override;

private:
    char buffer_[capacity]{};
    volatile std::size_t head_ = 0;
    volatile std::size_t tail_ = 0;
};

} // namespace shirley::io
