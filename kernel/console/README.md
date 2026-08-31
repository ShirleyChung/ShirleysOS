# Generic console

The console is the only interface between everything above it and the hardware:

```text
shell / readline / printf
           ↓
        console
      ↙         ↘
   kbd0        uart0 / framebuffer
```

Output goes through a replaceable `Backend`, selected with `set_backend()`
before `initialize()`. Passing `nullptr` restores the platform default:

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

Input goes through devices instead, because a machine can have several sources
at once — a keyboard and a serial terminal should both drive the same shell. An
input driver publishes its ring buffer as a device and hands it over:

```cpp
shirley::console::attach_input(keyboard_device);
```

`console::read()` then polls the attached devices in attachment order and
returns the first result that carries a character. It never waits: with nothing
queued anywhere it transfers zero bytes, and the caller decides whether to wait
for an interrupt. Attaching the first device is also what points standard input
at the console — before that, standard input stays unset, because a prompt on a
machine that can produce no characters would only lie.

The console is itself a device in the registry, named `console`, whose reads
and writes come back through this layer. A future `/dev/console` is that device
under a path, and needs no further adapter.
