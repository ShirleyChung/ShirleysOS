#include "shirley/input_queue.hpp"
#include "shirley/io.hpp"

namespace shirley::io {
namespace {

// 主控台的共用輸入佇列。生產者是中斷處理常式，消費者是 shell 的行編輯器；
// InputQueue 本身已經是單生產者、單消費者的免鎖佇列，所以這裡不需要再加
// 任何保護。多個輸入裝置共用同一個佇列並不違反那個前提：中斷處理常式彼此
// 之間不會同時執行，處理常式執行時中斷是關閉的。
//
// The console's shared input queue. The producer is an interrupt handler and
// the consumer is the shell's line editor. InputQueue is already a lock-free
// single-producer, single-consumer queue, so nothing further is needed here.
// Several input devices sharing one queue does not break that premise: an
// interrupt handler runs with interrupts disabled, so two of them never run at
// the same time.
InputQueue queue;

} // namespace

InputQueue& console_input() { return queue; }

bool console_input_push(char value) { return queue.push(value); }

void attach_console_input() { set_standard_input(&queue); }

} // namespace shirley::io
