# Text helpers

`shirley::text` is the small set of string operations the file system and the
shell share. A freestanding kernel has no libc, and these are the pieces that
would otherwise be reimplemented in both places.

```cpp
char path[shirley::fs::max_path_length];
if (!shirley::text::copy(path, sizeof(path), "/etc") ||
    !shirley::text::append(path, sizeof(path), "/motd")) return false;
```

Every function is bounded by the destination capacity and reports failure
rather than truncating: a truncated path quietly names a different file, which
is far more dangerous than an explicit `false`. `copy()` empties the
destination when the source does not fit, and `append()` leaves it untouched,
so neither can produce a half-written string that looks valid.
