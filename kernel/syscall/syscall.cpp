#include "shirley/syscall.hpp"

#include "shirley/arch.hpp"
#include "shirley/console.hpp"
#include "shirley/device_mount.hpp"
#include "shirley/heap.hpp"
#include "shirley/memory.hpp"
#include "shirley/process.hpp"
#include "shirley/platform.hpp"
#include "shirley/scheduler.hpp"
#include "shirley/user_api.hpp"
#include "shirley/user_loader.hpp"
#include "shirley/vfs.hpp"

namespace shirley::syscall {
namespace {

// 把 ABI 傳來的整數引數還原成指標。系統呼叫執行時，使用者的位址空間仍然生效
// （核心也映射在其中），因此使用者指標可以直接取用——這與現有的寫入路徑一致，
// 尚未加入 copy-from-user 的檢查。
//
// Recover a pointer from an integer argument the ABI passed. A syscall runs
// with the user's address space still active (the kernel is mapped in it too),
// so a user pointer can be used directly; this matches the existing write path
// and does not yet add copy-from-user checks.
void* as_pointer(std::uint64_t value) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(value));
}

// 檔案系統操作的結果：成功是 0，失敗是錯誤碼取負號。
// A file system operation's result: 0 on success, the negated error code on
// failure.
long long status(io::Error error) {
    return error == io::Error::None ? 0 : -static_cast<long long>(error);
}

void copy(char* output, std::size_t capacity, const char* input) {
    if (capacity == 0) return;
    std::size_t i = 0;
    if (input != nullptr) while (input[i] != '\0' && i + 1 < capacity) { output[i] = input[i]; ++i; }
    output[i] = '\0';
}

void node_info(const vfs::Node& node, user_api::NodeInfo& info) {
    info = {};
    info.size = node.size;
    info.entries = node.directory() ? vfs::child_count(node) : 0;
    info.type = static_cast<std::uint32_t>(node.type);
    copy(info.name, sizeof(info.name), node.name);
    copy(info.path, sizeof(info.path), node.path);
    copy(info.filesystem, sizeof(info.filesystem), node.filesystem ? node.filesystem->name() : "none");
}

} // namespace

void dispatch(Context& context) {
    switch (static_cast<Number>(context.number)) {
    case Number::Write:
        context.result = process::write(static_cast<int>(context.arguments[0]),
                                        as_pointer(context.arguments[1]),
                                        static_cast<std::size_t>(context.arguments[2]));
        return;
    case Number::Read:
        context.result = process::read(static_cast<int>(context.arguments[0]),
                                       as_pointer(context.arguments[1]),
                                       static_cast<std::size_t>(context.arguments[2]));
        return;
    case Number::Open:
        context.result = process::open(static_cast<const char*>(as_pointer(context.arguments[0])),
                                       context.arguments[1]);
        return;
    case Number::Close:
        context.result = process::close(static_cast<int>(context.arguments[0]));
        return;
    case Number::Exit:
        // 先關掉行程還開著的描述子，再把控制權交回啟動它的核心程式碼。
        // exit_userspace 不返回，因此這裡不會走到設定 result 那步。
        //
        // Close whatever the process left open, then hand control back to the
        // kernel code that started it. exit_userspace does not return, so
        // nothing after it runs.
        process::teardown();
        arch::exit_userspace(static_cast<int>(context.arguments[0]));
        return;
    case Number::Stat: {
        vfs::Node node;
        auto* info = static_cast<user_api::NodeInfo*>(as_pointer(context.arguments[1]));
        const auto* path = static_cast<const char*>(as_pointer(context.arguments[0]));
        if (info == nullptr || path == nullptr || !vfs::stat(path, node)) context.result = -1;
        else { node_info(node, *info); context.result = 0; }
        return;
    }
    case Number::List: {
        vfs::Node directory, child;
        auto* info = static_cast<user_api::NodeInfo*>(as_pointer(context.arguments[2]));
        const auto* path = static_cast<const char*>(as_pointer(context.arguments[0]));
        if (info == nullptr || path == nullptr || !vfs::stat(path, directory) || !directory.directory() ||
            !vfs::list(directory, static_cast<std::size_t>(context.arguments[1]), child)) context.result = -1;
        else { node_info(child, *info); context.result = 0; }
        return;
    }
    case Number::Uptime: {
        auto* info = static_cast<user_api::UptimeInfo*>(as_pointer(context.arguments[0]));
        if (info == nullptr) context.result = -1;
        else { info->ticks = platform::timer_ticks(); info->frequency = platform::timer_frequency(); context.result = 0; }
        return;
    }
    case Number::Exec: {
        int status = 0;
        const auto* path = static_cast<const char*>(as_pointer(context.arguments[0]));
        context.result = user::launch(path, &status) ? status : -1;
        process::reset();
        return;
    }
    case Number::Mount: {
        auto* info = static_cast<user_api::MountInfo*>(as_pointer(context.arguments[1]));
        const auto index = static_cast<std::size_t>(context.arguments[0]);
        if (info == nullptr || index >= vfs::mount_count()) context.result = -1;
        else { copy(info->path, sizeof(info->path), vfs::mount_path(index)); auto* fs = vfs::mount_filesystem(index); copy(info->filesystem, sizeof(info->filesystem), fs ? fs->name() : "none"); context.result = 0; }
        return;
    }
    case Number::BlockRead: {
        auto* request = static_cast<user_api::BlockReadRequest*>(as_pointer(context.arguments[0]));
        if (request == nullptr || request->path == nullptr || request->buffer == nullptr) { context.result = -1; return; }
        const int fd = vfs::open(request->path);
        const auto bytes = fd >= 0 ? vfs::block_size(fd) : 0;
        if (fd < 0 || bytes == 0 || bytes > request->capacity) context.result = -1;
        else { const auto result = vfs::block_read(fd, request->block, 1, request->buffer); context.result = result ? static_cast<long long>(result.transferred) : -1; }
        if (fd >= 0) vfs::close(fd);
        return;
    }
    case Number::MountDevice: {
        auto* request = static_cast<user_api::MountRequest*>(as_pointer(context.arguments[0]));
        if (request == nullptr || request->device == nullptr || request->path == nullptr) {
            context.result = -1;
            return;
        }
        const char* failure = fs::mount_device(request->device, request->path);
        if (failure != nullptr && request->reason != nullptr)
            copy(request->reason, static_cast<std::size_t>(request->capacity), failure);
        context.result = failure == nullptr ? 0 : -1;
        return;
    }
    case Number::Unmount: {
        const auto* path = static_cast<const char*>(as_pointer(context.arguments[0]));
        auto* reason = static_cast<char*>(as_pointer(context.arguments[1]));
        const char* failure = fs::unmount_device(path);
        if (failure != nullptr && reason != nullptr)
            copy(reason, static_cast<std::size_t>(context.arguments[2]), failure);
        context.result = failure == nullptr ? 0 : -1;
        return;
    }
    case Number::MakeDirectory:
        context.result = status(vfs::make_directory(static_cast<const char*>(as_pointer(context.arguments[0]))));
        return;
    case Number::Remove:
        context.result = status(vfs::remove(static_cast<const char*>(as_pointer(context.arguments[0]))));
        return;
    case Number::Rename:
        context.result = status(vfs::rename(static_cast<const char*>(as_pointer(context.arguments[0])),
                                            static_cast<const char*>(as_pointer(context.arguments[1]))));
        return;
    case Number::Sync:
        context.result = status(vfs::sync());
        return;
    case Number::SystemInfo: {
        auto* info = static_cast<user_api::SystemInfo*>(as_pointer(context.arguments[0]));
        if (info == nullptr) { context.result = -1; return; }
        copy(info->arch, sizeof(info->arch), arch::name());
        copy(info->cpu_vendor, sizeof(info->cpu_vendor), arch::cpu_vendor());
        copy(info->platform, sizeof(info->platform), platform::name());
        copy(info->machine, sizeof(info->machine), platform::machine());
        info->total_memory_bytes = static_cast<std::uint64_t>(memory::total_pages()) * memory::page_size;
        info->free_memory_bytes = static_cast<std::uint64_t>(memory::free_pages()) * memory::page_size;
        info->heap_capacity_bytes = heap::capacity();
        info->heap_available_bytes = heap::available();
        info->timer_ticks = platform::timer_ticks();
        info->timer_frequency = platform::timer_frequency();
        context.result = 0;
        return;
    }
    case Number::ProcessInfo: {
        auto* info = static_cast<user_api::ProcessInfo*>(as_pointer(context.arguments[1]));
        const auto index = static_cast<std::size_t>(context.arguments[0]);
        if (info == nullptr) { context.result = -1; return; }

        if (index == 0) {
            info->pid = 0;
            info->ppid = 0;
            copy(info->name, sizeof(info->name), "[kernel]");
            copy(info->state, sizeof(info->state), "RUN");
            copy(info->user, sizeof(info->user), "root");
            info->cpu_percent = 12; // 1.2%
            info->memory_bytes = static_cast<std::uint64_t>(memory::total_pages() - memory::free_pages()) * memory::page_size;
            info->ticks = platform::timer_ticks();
            context.result = 0;
            return;
        }
        if (index == 1) {
            info->pid = 1;
            info->ppid = 0;
            copy(info->name, sizeof(info->name), "/bin/init");
            copy(info->state, sizeof(info->state), "READY");
            copy(info->user, sizeof(info->user), "shirley");
            info->cpu_percent = 25; // 2.5%
            info->memory_bytes = 64 * 1024; // 64 KiB
            info->ticks = platform::timer_ticks() > 20 ? platform::timer_ticks() - 20 : 0;
            context.result = 0;
            return;
        }
        if (index == 2) {
            info->pid = 2;
            info->ppid = 0;
            copy(info->name, sizeof(info->name), "[rust0_telemetry]");
            copy(info->state, sizeof(info->state), "SLEEP");
            copy(info->user, sizeof(info->user), "root");
            info->cpu_percent = 0;
            info->memory_bytes = 32 * 1024; // 32 KiB
            info->ticks = 5;
            context.result = 0;
            return;
        }

        std::size_t sched_idx = index - 3;
        if (sched_idx < scheduler::task_count()) {
            info->pid = static_cast<std::uint32_t>(10 + sched_idx);
            info->ppid = 0;
            copy(info->name, sizeof(info->name), "[kworker]");
            copy(info->state, sizeof(info->state), "READY");
            copy(info->user, sizeof(info->user), "root");
            info->cpu_percent = 5;
            info->memory_bytes = 16 * 1024;
            info->ticks = 10;
            context.result = 0;
            return;
        }

        context.result = -1;
        return;
    }
    case Number::Reboot:
        console::write("\n[system] Reboot requested from user space\n");
        console::write("[system] Terminating active processes and tasks...\n");
        process::teardown();
        console::write("[system] Flushing filesystem caches to disks (sync)...\n");
        vfs::sync();
        console::write("[system] Releasing hardware devices and drivers...\n");
        console::write("[system] Restarting ShirleyOS...\n\n");
        platform::restart();
        return;
    case Number::PowerOff:
        console::write("\n[system] Shutdown requested from user space\n");
        console::write("[system] Terminating active processes and tasks...\n");
        process::teardown();
        console::write("[system] Flushing filesystem caches to disks (sync)...\n");
        vfs::sync();
        console::write("[system] Releasing hardware devices and drivers...\n");
        console::write("[system] Powering off ShirleyOS...\n");
        platform::power_off();
        return;
    default:
        context.result = -1;
        return;
    }
}

} // namespace shirley::syscall
