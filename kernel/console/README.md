# Generic console

Kernel code writes through `shirley::console`. The active device is a
`shirley::console::Backend`, selected with `set_backend()` before
`initialize()`. Passing `nullptr` restores the platform default:

```cpp
class MyConsole final : public shirley::console::Backend {
public:
    void initialize() override;
    void write(const char* text, std::size_t length) override;
};

MyConsole console;
shirley::console::set_backend(&console);
shirley::console::initialize();
```

UART backends are supplied by the platform layer. The host platform's default
is the shell console, which writes to standard output. A future framebuffer,
serial, logging, or test backend can be selected without changing kernel code.
