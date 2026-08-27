#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

static int output_char(char* buffer, size_t capacity, size_t* used, char value) {
    if (*used >= capacity) return -1;
    buffer[(*used)++] = value;
    return 0;
}

static int output_string(char* buffer, size_t capacity, size_t* used, const char* value) {
    if (value == 0) value = "(null)";
    while (*value != '\0') {
        if (output_char(buffer, capacity, used, *value++) != 0) return -1;
    }
    return 0;
}

static int output_unsigned(char* buffer, size_t capacity, size_t* used,
                           unsigned long long value, unsigned base, int negative) {
    char digits[32];
    size_t count = 0;
    const char* alphabet = "0123456789abcdef";
    if (negative && output_char(buffer, capacity, used, '-') != 0) return -1;
    if (value == 0) digits[count++] = '0';
    while (value != 0) {
        digits[count++] = alphabet[value % base];
        value /= base;
    }
    while (count != 0) {
        if (output_char(buffer, capacity, used, digits[--count]) != 0) return -1;
    }
    return 0;
}

int printf(const char* format, ...) {
    if (format == 0) return -1;
    char output[512];
    size_t used = 0;
    va_list arguments;
    va_start(arguments, format);
    while (*format != '\0') {
        if (*format != '%') {
            if (output_char(output, sizeof(output), &used, *format++) != 0) goto failed;
            continue;
        }
        ++format;
        switch (*format++) {
            case '%': if (output_char(output, sizeof(output), &used, '%') != 0) goto failed; break;
            case 'c': if (output_char(output, sizeof(output), &used, (char)va_arg(arguments, int)) != 0) goto failed; break;
            case 's': if (output_string(output, sizeof(output), &used, va_arg(arguments, const char*)) != 0) goto failed; break;
            case 'd': {
                long long value = va_arg(arguments, int);
                unsigned long long magnitude = value < 0 ? (unsigned long long)(-(value + 1)) + 1 : (unsigned long long)value;
                if (output_unsigned(output, sizeof(output), &used, magnitude, 10, value < 0) != 0) goto failed;
                break;
            }
            case 'u': if (output_unsigned(output, sizeof(output), &used, va_arg(arguments, unsigned), 10, 0) != 0) goto failed; break;
            case 'x': if (output_unsigned(output, sizeof(output), &used, va_arg(arguments, unsigned), 16, 0) != 0) goto failed; break;
            default: goto failed;
        }
    }
    va_end(arguments);
    if (write(1, output, used) < 0) return -1;
    return (int)used;
failed:
    va_end(arguments);
    return -1;
}
