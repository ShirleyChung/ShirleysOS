# Input drivers

Hardware-independent input logic. `scancode.cpp` translates PS/2 scancode set
1 into characters and knows nothing about ports, controllers, or interrupts,
so it compiles into the host build and is covered by `tests/input_smoke.cpp`.

The machine-specific half — reading the 8042 controller and handling IRQ1 —
lives in `platform/pc/ps2_keyboard.cpp`, because port I/O only exists on a PC.
Splitting the driver this way keeps the part worth testing testable.

The first version decodes unmodified lowercase letters, digits, Enter,
Backspace, Tab, space, and basic punctuation. Release events are ignored, and
both bytes of an extended key are dropped rather than decoded as the base
scancode, which would turn the arrow keys into letters.
