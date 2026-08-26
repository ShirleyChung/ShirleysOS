#pragma once

#include <cstddef>
#include <cstdint>

namespace shirley::scheduler {

// 工作識別碼；0 保留給「無效工作」。
// A task identifier; 0 is reserved to mean "no task".
using TaskId = std::uint32_t;
constexpr TaskId invalid_task = 0;

// 工作狀態，以及工作主體回傳的下一個狀態。
// Task state, which is also what a task body returns to request its next state.
enum class TaskState : std::uint8_t { Ready, Running, Blocked, Finished };
using TaskEntry = TaskState (*)(void* context);

void initialize();
// 建立工作；表格已滿或 entry 為空時回傳 invalid_task。
// Create a task; returns invalid_task when the table is full or entry is null.
TaskId create(TaskEntry entry, void* context = nullptr);
// 以輪替方式執行下一個可執行的工作；沒有工作可跑時回傳 false。
// Run the next runnable task round-robin; returns false when none can run.
bool run_next();
bool block(TaskId task);
bool wake(TaskId task);
TaskId current_task();
TaskState state(TaskId task);
std::size_t task_count();
std::size_t runnable_count();

} // namespace shirley::scheduler
