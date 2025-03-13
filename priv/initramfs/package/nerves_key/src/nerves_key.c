#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <linux/dm-ioctl.h>

#include "util.h"
#include "serial.h"


#define SALT_LENGTH 16
#define HASH_SIZE 64
#define SECRET_LENGTH 32
#define SECRET_LENGTH_HEX (SECRET_LENGTH * 2)

static size_t read_serial(char *buffer, size_t max_length)
{
    return atecc_serial(buffer, max_length, "/dev/i2c-2", 0x35);
}

int main(int argc, char *argv[])
{
    (void)argc; // unused
    (void)argv;

#ifdef DEBUG
    info("version " PROGRAM_VERSION_STR " [DEBUG]");
#endif

    // char message[SALT_LENGTH + MAX_SERIAL_LENGTH + 1] = {0};
    // unsigned char hash[HASH_SIZE] = {0};
    // char secret[SECRET_LENGTH_HEX + 1];
    // unsigned char salt[SALT_LENGTH + 1];

    // char *salt_hex = 0;
    // Bad salt? Create a new salt.
    // getrandom(salt, SALT_LENGTH, 0);

    char new_salt_hex[SALT_LENGTH * 2 + 1];
    //bytes_to_hex(salt, new_salt_hex, SALT_LENGTH);

    // Create the key
    //memcpy(message, &salt, SALT_LENGTH);
    //int serial_length = read_serial(message + SALT_LENGTH, MAX_SERIAL_LENGTH);

    // create a blake2b hash of salt and serial and convert it to a lowercase hex string
    //crypto_blake2b(hash, (uint8_t *) message, SALT_LENGTH + serial_length);
    //bytes_to_hex(hash, secret, SECRET_LENGTH);

    printf("woo");
}