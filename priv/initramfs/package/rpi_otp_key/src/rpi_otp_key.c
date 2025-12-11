/*
 * rpi_otp_key - Utility for reading/writing Raspberry Pi OTP key material
 *
 * Copyright (C) 2024
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>

#define PROGRAM_NAME "rpi-otp-key"

#ifndef PROGRAM_VERSION
#define PROGRAM_VERSION "unknown"
#endif

#ifdef DEBUG
#define debug(...) fprintf(stderr, "DEBUG: " __VA_ARGS__)
#else
#define debug(...)
#endif

#define MAX_KEY_WORDS 16
#define DEFAULT_KEY_WORDS 8

typedef struct {
    int check_only;
    int force_write;
    int output_binary;
    int skip_confirm;
    int row_count;
    int row_offset;
    char *write_key;
} options_t;

static void die(const char *msg)
{
    fprintf(stderr, "Error: %s\n", msg);
    exit(1);
}

static void usage(void)
{
    printf("Usage: %s [-bcfwy] [-l length] [-o offset] [key]\n", PROGRAM_NAME);
    printf("\n");
    printf("No args - reads the current device unique private key from OTP.\n");
    printf("*These values are NOT visible via 'vcgencmd otp_dump'*\n");
    printf("\n");
    printf("Options:\n");
    printf("  -b        Output the key in binary format\n");
    printf("  -c        Reads key and exits with 1 if it is all zeros (not set)\n");
    printf("  -f        Force write (if OTP is non-zero)\n");
    printf("            The vcmailbox API checks that the new key equals the bitwise\n");
    printf("            OR of current OTP and new key. OTP bits can never change from 1 to 0.\n");
    printf("  -w <key>  Writes the new key to OTP memory\n");
    printf("  -y        Skip the confirmation prompt when writing to OTP\n");
    printf("  -l <len>  Specify key length in words. Defaults to 8 words (32 bytes).\n");
    printf("            Pi 5 supports up to 16 words (64 bytes).\n");
    printf("  -o <off>  Word offset into the keystore (e.g. 0-7 for Pi 4, 0-15 for Pi 5)\n");
    printf("  -h        Show this help message\n");
    printf("  -v        Show version information\n");
    printf("\n");
    printf("<key> is a hex number (e.g., 64 digits for 256 bits)\n");
    printf("\n");
    printf("Example key generation and provisioning:\n");
    printf("  # Generate the new private-key\n");
    printf("  openssl ecparam -name prime256v1 -genkey -noout -out private_key.pem\n");
    printf("\n");
    printf("  # Extract the raw private key component\n");
    printf("  openssl ec -in private_key.pem -text -noout | \\\n");
    printf("    awk '/priv:/{flag=1; next} /pub:/{flag=0} flag' | \\\n");
    printf("    tr -d ' \\n:' | head -n1 > d.hex\n");
    printf("\n");
    printf("  # Write the key to OTP\n");
    printf("  %s -w $(cat d.hex)\n", PROGRAM_NAME);
    printf("\n");
    printf("WARNING: Changes to OTP memory are permanent and cannot be undone.\n");
}

static void version(void)
{
    printf("%s version %s\n", PROGRAM_NAME, PROGRAM_VERSION);
}

static int get_board_info(uint32_t *board_info)
{
    FILE *fp;
    char buf[256];
    uint32_t value = 0;

    /* Try device tree first */
    fp = fopen("/sys/firmware/devicetree/base/system/linux,revision", "r");
    if (fp) {
        if (fread(buf, 1, sizeof(buf), fp) > 0) {
            /* Convert from big-endian bytes */
            value = ((uint8_t)buf[0] << 24) | ((uint8_t)buf[1] << 16) |
                    ((uint8_t)buf[2] << 8) | (uint8_t)buf[3];
            *board_info = value;
            fclose(fp);
            debug("Board info from devicetree: 0x%08x\n", value);
            return 0;
        }
        fclose(fp);
    }

    /* Try /proc/cpuinfo */
    fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        while (fgets(buf, sizeof(buf), fp)) {
            if (sscanf(buf, "Revision : %x", &value) == 1) {
                *board_info = value;
                fclose(fp);
                debug("Board info from cpuinfo: 0x%08x\n", value);
                return 0;
            }
        }
        fclose(fp);
    }

    /* Try vcgencmd as fallback */
    fp = popen("vcgencmd otp_dump 2>/dev/null | grep '30:' | sed 's/.*://'", "r");
    if (fp) {
        if (fgets(buf, sizeof(buf), fp) == 1 && sscanf(buf, "%x", &value) == 1) {
            *board_info = value;
            pclose(fp);
            debug("Board info from vcgencmd: 0x%08x\n", value);
            return 0;
        }
        pclose(fp);
    }

    return -1;
}

static int get_max_row_count(uint32_t board_info)
{
    int chip_gen;

    /* Check if chip is supported (bit 23) */
    if (((board_info >> 23) & 1) == 0) {
        die("Chip not supported");
    }

    /* Get processor type (bits 12-15) */
    chip_gen = (board_info >> 12) & 0xF;

    if (chip_gen == 3) {
        /* Pi 4 */
        return 8;
    } else if (chip_gen == 4) {
        /* Pi 5 */
        return 16;
    } else {
        fprintf(stderr, "WARNING: Secure-boot is only supported on Pi4 and newer models\n");
        return 8;
    }
}

static void normalize_hex_key(char *key)
{
    char *src = key;
    char *dst = key;

    /* Convert to lowercase and remove non-hex characters */
    while (*src) {
        char c = tolower(*src);
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
            *dst++ = c;
        }
        src++;
    }
    *dst = '\0';
}

static int read_key(int row_offset, int row_count, char *key_out, size_t key_out_size)
{
    char cmd[512];
    char buf[1024];
    FILE *fp;
    int i;
    char *token;
    int token_count = 0;

    /* Build vcmailbox command */
    snprintf(cmd, sizeof(cmd), "vcmailbox 0x00030081 %d %d %d %d 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 2>/dev/null",
             8 + row_count * 4, 8 + row_count * 4, row_offset, row_count);

    debug("Read command: %s\n", cmd);

    fp = popen(cmd, "r");
    if (!fp) {
        die("Failed to execute vcmailbox command");
    }

    if (!fgets(buf, sizeof(buf), fp)) {
        pclose(fp);
        die("Failed to read vcmailbox output");
    }

    pclose(fp);

    debug("Raw output: %s\n", buf);

    /* Parse output - skip first tokens and get the key words */
    key_out[0] = '\0';
    token = strtok(buf, " \t\n");
    while (token) {
        token_count++;
        /* Start collecting from token 8 onwards */
        if (token_count >= 8 && token_count < (8 + row_count)) {
            /* Remove 0x prefix if present */
            if (strncmp(token, "0x", 2) == 0) {
                token += 2;
            }
            /* Ensure we have 8 hex digits */
            char word[9];
            snprintf(word, sizeof(word), "%08s", token);
            strncat(key_out, word, key_out_size - strlen(key_out) - 1);
        }
        token = strtok(NULL, " \t\n");
    }

    debug("Read key: %s\n", key_out);
    return 0;
}

static int is_key_all_zeros(const char *key)
{
    while (*key) {
        if (*key != '0') {
            return 0;
        }
        key++;
    }
    return 1;
}

static int write_key(const char *key, int row_offset, int row_count, int skip_confirm)
{
    char normalized_key[256];
    char cmd[1024];
    char buf[1024];
    char verify_key[256];
    FILE *fp;
    int i;
    int expected_len = row_count * 8; /* 8 hex chars per word */

    strncpy(normalized_key, key, sizeof(normalized_key) - 1);
    normalized_key[sizeof(normalized_key) - 1] = '\0';
    normalize_hex_key(normalized_key);

    debug("Normalized key: %s\n", normalized_key);

    if (strlen(normalized_key) != expected_len) {
        fprintf(stderr, "Invalid key parameter: expected %d hex digits, got %zu\n",
                expected_len, strlen(normalized_key));
        die("Key length mismatch");
    }

    /* Prompt for confirmation unless skipped */
    if (!skip_confirm && isatty(STDIN_FILENO)) {
        char confirm[64];

        printf("Write %s of %d words to OTP starting at word offset %d?\n",
               normalized_key, row_count, row_offset);
        printf("\n");
        printf("WARNING: Updates to OTP registers are permanent and cannot be undone.\n");
        printf("\n");
        printf("Type YES (in upper case) to continue or press return to exit: ");
        fflush(stdout);

        if (!fgets(confirm, sizeof(confirm), stdin)) {
            printf("Cancelled\n");
            exit(0);
        }

        /* Remove newline */
        confirm[strcspn(confirm, "\n")] = '\0';

        if (strcmp(confirm, "YES") != 0) {
            printf("Cancelled\n");
            exit(0);
        }
    }

    /* Build vcmailbox write command */
    snprintf(cmd, sizeof(cmd), "vcmailbox 0x38081 %d %d %d %d",
             8 + row_count * 4, 8 + row_count * 4, row_offset, row_count);

    /* Append key words */
    for (i = 0; i < row_count; i++) {
        char word[10];
        strncpy(word, normalized_key + (i * 8), 8);
        word[8] = '\0';
        snprintf(cmd + strlen(cmd), sizeof(cmd) - strlen(cmd), " 0x%s", word);
    }

    strncat(cmd, " 2>/dev/null", sizeof(cmd) - strlen(cmd) - 1);

    debug("Write command: %s\n", cmd);

    fp = popen(cmd, "r");
    if (!fp) {
        die("Failed to execute vcmailbox command");
    }

    /* Read response */
    if (fgets(buf, sizeof(buf), fp)) {
        debug("Write response: %s\n", buf);
    }

    if (pclose(fp) != 0) {
        die("Failed to write key");
    }

    /* Verify the write */
    if (read_key(row_offset, row_count, verify_key, sizeof(verify_key)) != 0) {
        die("Failed to read back key for verification");
    }

    if (strcmp(verify_key, normalized_key) != 0) {
        fprintf(stderr, "Key readback check failed.\n");
        fprintf(stderr, "Expected: %s\n", normalized_key);
        fprintf(stderr, "Got:      %s\n", verify_key);
        die("Verification failed");
    }

    printf("Key written and verified successfully\n");
    return 0;
}

static void output_key_binary(const char *hex_key)
{
    size_t len = strlen(hex_key);
    size_t i;

    for (i = 0; i < len; i += 2) {
        unsigned int byte;
        char hex_byte[3] = {hex_key[i], hex_key[i+1], '\0'};
        if (sscanf(hex_byte, "%x", &byte) == 1) {
            putchar((unsigned char)byte);
        }
    }
}

int main(int argc, char *argv[])
{
    options_t opts = {0};
    int opt;
    uint32_t board_info;
    int max_row_count;
    char key[256];

    /* Set defaults */
    opts.row_count = DEFAULT_KEY_WORDS;
    opts.row_offset = 0;

    /* Parse command line options */
    while ((opt = getopt(argc, argv, "bcfhvw:yl:o:")) != -1) {
        switch (opt) {
        case 'b':
            opts.output_binary = 1;
            break;

        case 'c':
            opts.check_only = 1;
            break;

        case 'f':
            opts.force_write = 1;
            break;

        case 'h':
            usage();
            return 0;

        case 'v':
            version();
            return 0;

        case 'w':
            opts.write_key = optarg;
            break;

        case 'y':
            opts.skip_confirm = 1;
            break;

        case 'l':
            opts.row_count = atoi(optarg);
            break;

        case 'o':
            opts.row_offset = atoi(optarg);
            break;

        default:
            usage();
            return 1;
        }
    }

    /* Get remaining arguments */
    if (optind < argc && !opts.write_key) {
        opts.write_key = argv[optind];
    }

    /* Get board information */
    if (get_board_info(&board_info) != 0) {
        die("No Raspberry Pi board info found");
    }

    max_row_count = get_max_row_count(board_info);
    debug("Max row count: %d\n", max_row_count);

    /* Validate parameters */
    if (opts.row_count < 1) {
        die("Key length too small");
    }
    if (opts.row_count > max_row_count) {
        die("Key length too big");
    }
    if (opts.row_offset < 0) {
        die("Offset too small");
    }
    if (opts.row_offset > (max_row_count - opts.row_count)) {
        die("Offset too big");
    }

    /* Check for binary output dependency */
    if (opts.output_binary && system("which xxd >/dev/null 2>&1") != 0) {
        die("xxd command required for binary output");
    }

    /* Execute requested operation */
    if (opts.write_key) {
        /* Write key operation */
        if (!opts.force_write) {
            /* Check if key is already set */
            if (read_key(opts.row_offset, opts.row_count, key, sizeof(key)) == 0) {
                if (!is_key_all_zeros(key)) {
                    die("Current key is non-zero. Specify -f to write anyway");
                }
            }
        }
        write_key(opts.write_key, opts.row_offset, opts.row_count, opts.skip_confirm);
    } else {
        /* Read key operation */
        if (read_key(opts.row_offset, opts.row_count, key, sizeof(key)) != 0) {
            die("Failed to read key");
        }

        if (opts.check_only) {
            /* Check if key is set */
            if (is_key_all_zeros(key)) {
                debug("Key is all zeros (not set)\n");
                return 1;
            }
            debug("Key is set\n");
            return 0;
        }

        /* Output key */
        if (opts.output_binary) {
            output_key_binary(key);
        } else {
            printf("%s\n", key);
        }
    }

    return 0;
}
