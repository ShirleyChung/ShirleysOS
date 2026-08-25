#include "shirley/scheduler.hpp"

namespace shirley::scheduler {
namespace {

struct Task {
    TaskId id = invalid_task;
    TaskEntry entry = nullptr;
    void* context = nullptr;
    TaskState state = TaskState::Finished;
};

constexpr std::size_t max_tasks = 64;
Task tasks[max_tasks]{};
TaskId next_id = 1;
TaskId active = invalid_task;
std::size_t cursor = 0;

Task* find(TaskId id) {
    if (id == invalid_task) return nullptr;
    for (auto& task : tasks) if (task.id == id) return &task;
    return nullptr;
}

} // namespace

void initialize() {
    for (auto& task : tasks) task = {};
    next_id = 1;
    active = invalid_task;
    cursor = 0;
}

TaskId create(TaskEntry entry, void* context) {
    if (entry == nullptr) return invalid_task;
    for (auto& task : tasks) {
        if (task.id != invalid_task) continue;
        if (next_id == invalid_task) ++next_id;
        task = {next_id++, entry, context, TaskState::Ready};
        return task.id;
    }
    return invalid_task;
}

bool run_next() {
    for (std::size_t checked = 0; checked < max_tasks; ++checked) {
        const auto index = (cursor + checked) % max_tasks;
        auto& task = tasks[index];
        if (task.id == invalid_task || task.state != TaskState::Ready) continue;

        cursor = (index + 1) % max_tasks;
        active = task.id;
        task.state = TaskState::Running;
        const auto result = task.entry(task.context);
        active = invalid_task;

        if (result == TaskState::Finished) {
            task = {};
        } else if (result == TaskState::Blocked) {
            task.state = TaskState::Blocked;
        } else {
            task.state = TaskState::Ready;
        }
        return true;
    }
    return false;
}

bool block(TaskId id) {
    auto* task = find(id);
    if (task == nullptr || task->state == TaskState::Finished) return false;
    task->state = TaskState::Blocked;
    return true;
}

bool wake(TaskId id) {
    auto* task = find(id);
    if (task == nullptr || task->state != TaskState::Blocked) return false;
    task->state = TaskState::Ready;
    return true;
}

TaskId current_task() { return active; }

TaskState state(TaskId id) {
    const auto* task = find(id);
    return task == nullptr ? TaskState::Finished : task->state;
}

std::size_t task_count() {
    std::size_t count = 0;
    for (const auto& task : tasks) if (task.id != invalid_task) ++count;
    return count;
}

std::size_t runnable_count() {
    std::size_t count = 0;
    for (const auto& task : tasks) if (task.id != invalid_task && task.state == TaskState::Ready) ++count;
    return count;
}

} // namespace shirley::scheduler
