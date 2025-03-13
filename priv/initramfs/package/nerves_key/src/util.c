/*
The MIT License (MIT)

Copyright (c) 2013-16 Frank Hunleth

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "util.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

static int format_message(char **strp, const char *fmt, va_list ap)
{
    char *msg;
    if (vasprintf(&msg, fmt, ap) < 0) {
        return -1;
    }

    int rc = asprintf(strp, PROGRAM_NAME ": %s\r\n", msg);
    free(msg);

    return rc;
}

static void log_write(const char *str, size_t len)
{
    size_t ignore;
    (void) write(STDERR_FILENO, str, len);
    int log_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (log_fd >= 0) {
        ignore = write(log_fd, str, len);
        close(log_fd);
    }
    (void) ignore;
}

static void log_format(const char *fmt, va_list ap)
{
    char *line;
    int len = format_message(&line, fmt, ap);

    if (len >= 0) {
        log_write(line, len);
        free(line);
    }
}

void info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_format(fmt, ap);
    va_end(ap);
}

static void report_dmesg()
{
    write(STDERR_FILENO, "\n## dmesg\n\n```\n", 15);

    int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return;

    int in_message = 0;
    for (;;) {
        char buffer[4096];
        ssize_t num_read = read(fd, buffer, sizeof(buffer));
        if (num_read <= 0)
            break;

        // Trivial log parser that prints the messages
        int start_ix = 0;
        for (int i = 0; i < num_read; i++) {
            if (in_message && buffer[i] == '\n') {
                write(STDERR_FILENO, &buffer[start_ix], i + 1 - start_ix);
                start_ix = i + 1;
                in_message = 0;
            } else if (!in_message && buffer[i] == ';') {
                start_ix = i + 1;
                in_message = 1;
            }
        }
        if (in_message)
            write(STDERR_FILENO, &buffer[start_ix], num_read - start_ix);
    }
    close(fd);
    write(STDERR_FILENO, "```\n", 4);
}

void fatal(const char *fmt, ...)
{
    log_write("\r\n\r\nFATAL ERROR:\r\n", 18);

    va_list ap;
    va_start(ap, fmt);
    log_format(fmt, ap);
    va_end(ap);

    report_dmesg();

    log_write("\r\n\r\nCANNOT CONTINUE.\r\n", 22);

    // Short pause before the panic.
    usleep(1000000);

    // This will cause the kernel to panic.
    exit(1);
}

void trim_string_in_place(char *str)
{
    char *first = str;
    while (*first && isspace(*first))
        first++;

    char *last = first + strlen(first) - 1;
    while (last != first && isspace(*last))
        last--;

    size_t len = last - first + 1;
    memmove(str, first, len);
    str[len] = '\0';
}

static uint8_t hexchar_to_int(char c)
{
    switch (c) {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'a':
    case 'A': return 10;
    case 'b':
    case 'B': return 11;
    case 'c':
    case 'C': return 12;
    case 'd':
    case 'D': return 13;
    case 'e':
    case 'E': return 14;
    case 'f':
    case 'F': return 15;
    default: return 255;
    }
}

static char nibble_to_hexchar(uint8_t nibble)
{
    switch (nibble) {
    case 0: return '0';
    case 1: return '1';
    case 2: return '2';
    case 3: return '3';
    case 4: return '4';
    case 5: return '5';
    case 6: return '6';
    case 7: return '7';
    case 8: return '8';
    case 9: return '9';
    case 10: return 'a';
    case 11: return 'b';
    case 12: return 'c';
    case 13: return 'd';
    case 14: return 'e';
    case 15: return 'f';
    default: return '0';
    }
}

static int two_hex_to_byte(const char *str, uint8_t *byte)
{
    uint8_t sixteens = hexchar_to_int(str[0]);
    uint8_t ones = hexchar_to_int(str[1]);
    if (sixteens == 255 || ones == 255)
        ERR_RETURN("Invalid character in hex string");

    *byte = (uint8_t) (sixteens << 4) | ones;
    return 0;
}

int hex_to_bytes(const char *str, uint8_t *bytes, size_t numbytes)
{
    size_t len = strlen(str);
    if (len != numbytes * 2)
        ERR_RETURN("hex string should have length %d, but got %d", numbytes * 2, len);

    while (len) {
        if (two_hex_to_byte(str, bytes) < 0)
            return -1;

        str += 2;
        len -= 2;
        bytes++;
    }
    return 0;
}

int bytes_to_hex(const uint8_t *bytes, char *str, size_t byte_count)
{
    while (byte_count) {
        *str++ = nibble_to_hexchar(*bytes >> 4);
        *str++ = nibble_to_hexchar(*bytes & 0xf);
        bytes++;
        byte_count--;
    }
    *str = '\0';
    return 0;
}