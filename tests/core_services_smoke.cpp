#include "shirley/io.hpp"
#include "shirley/ram_disk.hpp"
#include "shirley/scheduler.hpp"

#include <array>
#include <cassert>

namespace {
struct TaskContext { int runs = 0; int finish_after = 0; };

shirley::scheduler::TaskState task_step(void* opaque) {
    auto& context = *static_cast<TaskContext*>(opaque);
    ++context.runs;
    return context.runs >= context.finish_after ? shirley::scheduler::TaskState::Finished
                                                : shirley::scheduler::TaskState::Ready;
}

class CaptureStream final : public shirley::io::ByteStream {
public:
    shirley::io::Result read(void*, std::size_t) override { return {0, shirley::io::Error::Unsupported}; }
    shirley::io::Result write(const void* source, std::size_t length) override {
        if (length > data.size()) return {0, shirley::io::Error::OutOfRange};
        const auto* bytes = static_cast<const char*>(source);
        for (std::size_t i = 0; i < length; ++i) data[i] = bytes[i];
        size = length;
        return {length, shirley::io::Error::None};
    }
    std::array<char, 32> data{};
    std::size_t size = 0;
};
} // namespace

int main() {
    using namespace shirley;
    scheduler::initialize();
    TaskContext first{0, 2};
    TaskContext second{0, 3};
    const auto first_id = scheduler::create(task_step, &first);
    const auto second_id = scheduler::create(task_step, &second);
    assert(first_id != scheduler::invalid_task && second_id != scheduler::invalid_task);
    assert(scheduler::run_next() && first.runs == 1);
    assert(scheduler::run_next() && second.runs == 1);
    assert(scheduler::block(second_id));
    assert(scheduler::run_next() && first.runs == 2);
    assert(scheduler::task_count() == 1 && scheduler::runnable_count() == 0);
    assert(scheduler::wake(second_id));
    assert(scheduler::run_next() && scheduler::run_next());
    assert(scheduler::task_count() == 0 && !scheduler::run_next());

    CaptureStream capture;
    io::set_standard_output(&capture);
    constexpr char message[] = "stdout works";
    const auto written = io::write_standard_output(message, sizeof(message) - 1);
    assert(written && written.transferred == sizeof(message) - 1);
    assert(capture.size == sizeof(message) - 1 && capture.data[0] == 's');
    assert(io::read_standard_input(nullptr, 0).error == io::Error::Unsupported);

    std::array<std::uint8_t, 2048> storage{};
    std::array<std::uint8_t, 512> source{};
    std::array<std::uint8_t, 512> destination{};
    for (std::size_t i = 0; i < source.size(); ++i) source[i] = static_cast<std::uint8_t>(i);
    io::RamDisk disk(storage.data(), storage.size());
    assert(disk.block_size() == 512 && disk.block_count() == 4);
    assert(disk.write_blocks(2, 1, source.data()).transferred == 512);
    assert(disk.read_blocks(2, 1, destination.data()).transferred == 512);
    assert(source == destination);
    assert(disk.read_blocks(4, 1, destination.data()).error == io::Error::OutOfRange);
    assert(disk.write_blocks(0, 1, nullptr).error == io::Error::InvalidArgument);
    return 0;
}
