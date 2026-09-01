#include "shirley/syscall.hpp"

#include "shirley/arch.hpp"
#include "shirley/process.hpp"
#include "shirley/platform.hpp"
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
    default:
        context.result = -1;
        return;
    }
}

} // namespace shirley::syscall
