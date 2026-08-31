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

    key -> device interrupt -> driver decodes it -> the driver's ring buffer
        -> kbd0 -> console -> standard input -> read_line()

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

`help` lists them all. `ls`, `cat`, `cd`, `pwd`, and `stat` work on the VFS;
`mem`, `uptime`, `devices`, `mount`, and `version` report what the kernel knows
about itself; `blk` dumps a block device a sector at a time; `exec` and `hello`
run a program; `reboot` and `poweroff` go through the platform's power control.

`devices` prints the device registry and marks which devices are feeding
console input. The shell sees a name and a kind and nothing else: that IRQ1 and
port 0x60 sit behind `kbd0` is not something it can find out.

Every path goes through `shirley::vfs`, so `cat /etc/motd` and `cat /dev/kbd0`
are the same command doing the same thing — the shell does not know the two
paths are answered by different file systems. `cd` stores the normalized
absolute path the resolved node carries, so the prompt shows that rather than
the `../..` that was typed.

`echo ... > path` is the one command that writes. It exists to make the write
half reachable from the prompt: `echo hi > /dev/uart0` puts two characters on
the serial line, `> /dev/null` discards them, and `> /etc/version` is refused at
`open()` because SHRFS1 is read-only.

`exec <path>` reads a program out of the file system and hands it to the ELF
loader; `hello` is `exec /bin/hello`. The shell supplies a path and nothing
else: which device the file came off is not something it can see.
