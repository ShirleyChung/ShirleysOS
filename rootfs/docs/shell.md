# The console shell

The shell runs in the kernel. A keystroke reaches it like this:

    key -> device interrupt -> driver decodes it -> shared input queue
        -> standard input -> the shell's line editor

Nothing polls the keyboard. Between keys the kernel waits in a low-power state
for the next interrupt, which is why the shell can sit at a prompt forever
without spending a cycle.

The line editor is deliberately small: printable characters are appended and
echoed, backspace erases one character on the screen and in the buffer, and
Enter ends the line. There is no history and no cursor movement yet.

Paths work the way they look. A leading slash starts at the root, anything
else starts at the working directory, and "." and ".." are resolved while the
path is walked rather than by rewriting the string first.
