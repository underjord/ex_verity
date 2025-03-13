#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>
#include <stdint.h>

#define MAX_SERIAL_LENGTH 32

size_t cpuinfo_serial(char *buffer, size_t max_length);
size_t atecc_serial(char *buffer, size_t max_length, const char *i2c_filename, uint8_t i2c_address);

#endif