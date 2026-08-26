# Number formatting

Integer-to-string conversion for kernel diagnostics. The kernel has no
`printf` and no heap, so callers supply the buffer and every conversion
reports how many characters it wrote.

A buffer that is too small produces an empty string rather than a truncated
number, so a diagnostic can never show a value that looks plausible but is
wrong.
