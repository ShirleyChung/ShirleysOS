#pragma once

#include <cstddef>
#include <cstdint>

namespace shirley::scheduler {

using TaskId = std::uint32_t;
constexpr TaskId invalid_task = 0;

enum class TaskState : std::uint8_t { Ready, Running, Blocked, Finished };
using TaskEntry = TaskState (*)(void* context);

void initialize();
TaskId create(TaskEntry entry, void* context = nullptr);
bool run_next();
bool block(TaskId task);
bool wake(TaskId task);
TaskId current_task();
TaskState state(TaskId task);
std::size_t task_count();
std::size_t runnable_count();

} // namespace shirley::scheduler
