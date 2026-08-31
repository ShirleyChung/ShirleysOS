# Device abstraction and registry

`shirley::device` is the one thing a driver and its user agree on. A device is
a name, a kind, an operation table, and a `driver_data` pointer:

```cpp
constexpr shirley::device::Operations keyboard_operations{
    nullptr, nullptr, keyboard_read, nullptr, nullptr};

constinit shirley::device::Device keyboard{
    "kbd0", shirley::device::Type::Input, keyboard_operations, &queue};

shirley::device::register_device(keyboard);
```

and a consumer needs nothing but the name:

```cpp
auto* keyboard = shirley::device::find("kbd0");
keyboard->read(buffer, length);              // Unsupported when there is no read
keyboard->operations->read(*keyboard, buffer, length);   // the same call
```

The registry is a fixed table of at most `max_devices` pointers keyed by name.
Registration refuses a duplicate name, a full table, an empty or over-long
name, and a device with no operation table, and it says which of those it was:
a name collision is a programming error while a full table is a configuration
one, and a driver should react differently to each.

A device object belongs to its driver. The registry stores a pointer and
neither allocates nor frees anything; the kernel has no dynamic allocator, and
a driver lives as long as its hardware does.

**Every device object must be constant-initialized.** The kernel does not run
`.init_array`, so a static that needs run-time construction is never
constructed and simply keeps the zeroes `.bss` gave it — it would reach the
registry with an empty name and be refused. `Device`'s constructor is
`constexpr` for that reason, `constinit` on each device object turns a mistake
into a build error, and an operation table is referred to as an object
(`stream_operations`) rather than through a function that returns one: one
ordinary call in the initializer is enough to lose compile-time initialization.

`stream_operations` bridges to `io::ByteStream`, so anything already written as
a stream — `io::InputQueue`, for one — becomes a device without its read and
write being written twice. `device::Stream` goes the other way and lets a
future VFS node consume a device through the stream interface.

`null.cpp` is the smallest device there is and the only one backed by no
hardware: reads report end of file, writes are accepted and discarded.
