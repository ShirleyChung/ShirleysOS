# The console shell

The shell runs in the kernel. A keystroke reaches it like this:

    key -> device interrupt -> driver decodes it -> the driver's ring buffer
        -> kbd0 -> console -> standard input -> the shell's line editor

Nothing polls the keyboard. Between keys the kernel waits in a low-power state
for the next interrupt, which is why the shell can sit at a prompt forever
without spending a cycle.

Every path the shell touches goes through the VFS, so `cat /etc/motd` and
`cat /dev/kbd0` are the same command doing the same thing. `mount` shows which
file systems are behind which paths, `devices` lists what is under /dev, and
`blk /dev/ram0 0` dumps a sector of the disk this file is stored on.

The line editor is deliberately small: printable characters are appended and
echoed, backspace erases one character on the screen and in the buffer, and
Enter ends the line. There is no history and no cursor movement yet.

Paths work the way they look. A leading slash starts at the root, anything
else starts at the working directory, and "." and ".." are resolved while the
path is walked rather than by rewriting the string first.
