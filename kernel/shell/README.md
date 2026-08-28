# Console shell

`shirley::shell::run()` is the last step of kernel start-up and never returns:
after boot, the machine is this prompt.

    shirley:/$ ls /etc
         352  motd
          24  version
    2 entries

The shell runs in the kernel, not as a user process, because a process cannot
yet exit back to whatever started it. That is also why `hello` says what it is
about to do: the embedded user program takes over the CPU and the prompt does
not come back until the machine restarts.

## How a keystroke arrives

    key -> device interrupt -> driver decodes it -> io::console_input()
        -> standard input -> read_line()

Nothing polls. When the queue is empty the line editor waits for the next
interrupt in a low-power state, so an idle prompt costs no cycles. A keystroke
that lands between the check and the wait is picked up on the next timer
interrupt, at most 10 ms later — not worth polling a keyboard to avoid.

Echo belongs to the line editor rather than to the driver, because only the
code collecting the line knows what should appear: a backspace on an empty line
must do nothing, or it would erase the prompt. Both `\n` and `\r` end a line,
since a keyboard driver decodes Enter as one and a serial terminal sends the
other.

## Commands

`help` lists them all. `ls`, `cat`, `cd`, `pwd`, and `stat` work on the mounted
file system; `mem`, `uptime`, and `version` report what the kernel knows about
itself; `reboot` and `poweroff` go through the platform's power control.

Paths are resolved by `fs::lookup()` against the working directory, and `cd`
stores the path it rebuilds from the entry itself, so the prompt always shows a
normalized path rather than the `../..` that was typed.
