/*
 * verity_init - Init program for dm-verity verified boot initramfs
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
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>

#define PROGRAM_NAME "verity-init"

#ifndef PROGRAM_VERSION
#define PROGRAM_VERSION "unknown"
#endif

#ifdef DEBUG
#define debug(...) kmsg("DEBUG: " __VA_ARGS__)
#else
#define debug(...)
#endif

/* Configuration */
#define BOOT_DEVICE "/dev/mmcblk0p1"
#define ROOT_DEVICE "/dev/mmcblk0p2"
#define DATA_DEVICE "/dev/mmcblk0p3"
#define BOOT_MOUNT "/boot"
#define BOOT_IMAGE "/boot/boot.img"
#define ROOT_HASH_FILE "/root_hash.txt"
#define VERITY_OFFSET_FILE "/verity_offset.txt"
#define VERITY_MAPPER_NAME "verity"
#define VERITY_MAPPER_PATH "/dev/mapper/" VERITY_MAPPER_NAME
#define CRYPT_MAPPER_NAME "cryptroot"
#define CRYPT_MAPPER_PATH "/dev/mapper/" CRYPT_MAPPER_NAME
#define DATA_MAPPER_NAME "data"
#define DATA_MAPPER_PATH "/dev/mapper/" DATA_MAPPER_NAME
#define ROOT_MOUNT "/media/root"
#define TMP_MOUNT "/tmp"
#define NEXT_INIT "/sbin/init_sh"
#define KEY_FILE "/tmp/keyfile.bin"
#define FIRST_BOOT_CHECK_SIZE (256 * 512)  /* 256 LBA blocks */

/* Forward declarations */
static void kmsg(const char *fmt, ...);

static int check_platform_driver_bound(const char *driver_path)
{
    DIR *dir;
    struct dirent *entry;
    int found_device = 0;

    dir = opendir(driver_path);
    if (!dir) {
        return 0;  /* Driver not loaded */
    }

    /* Look for device symlinks (not . or .. or driver control files) */
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (strcmp(entry->d_name, "bind") == 0) continue;
        if (strcmp(entry->d_name, "unbind") == 0) continue;
        if (strcmp(entry->d_name, "uevent") == 0) continue;
        if (strcmp(entry->d_name, "module") == 0) continue;

        /* Found a device binding */
        found_device = 1;
        debug("Platform driver bound to device: %s", entry->d_name);
        break;
    }

    closedir(dir);
    return found_device;
}

static int wait_for_vcio_device(int timeout_seconds)
{
    struct stat st;
    int elapsed = 0;
    int driver_checked = 0;

    kmsg("Waiting for /dev/vcio device...");

    while (elapsed < timeout_seconds) {
        /* Check if /dev/vcio exists and is a character device */
        if (stat("/dev/vcio", &st) == 0 && S_ISCHR(st.st_mode)) {
            kmsg("/dev/vcio is ready");
            return 0;
        }

        /* On first iteration, check the platform driver status */
        if (!driver_checked) {
            driver_checked = 1;

            if (access("/sys/bus/platform/drivers/vcio", F_OK) == 0) {
                debug("vcio platform driver is registered");

                if (check_platform_driver_bound("/sys/bus/platform/drivers/vcio")) {
                    debug("vcio driver is bound to device, waiting for /dev node...");
                } else {
                    kmsg("Warning: vcio driver registered but not bound to any device");
                    kmsg("Check device tree for mailbox/vcio node");
                }
            } else {
                kmsg("Warning: vcio platform driver not found at /sys/bus/platform/drivers/vcio");
                kmsg("The driver may not be compiled or device tree may be missing vcio node");
            }

            /* Check if the device class exists */
            if (access("/sys/class/vcio", F_OK) == 0) {
                debug("vcio device class exists");
            }
        }

        sleep(1);
        elapsed++;
    }

    kmsg("ERROR: Timeout waiting for /dev/vcio device after %d seconds", timeout_seconds);
    kmsg("Troubleshooting:");
    kmsg("  1. Check that vcio driver is enabled in device tree");
    kmsg("  2. Check kernel config: CONFIG_BCM2835_MBOX=y or =m");
    kmsg("  3. Check dmesg for driver initialization errors");
    return -1;
}

static void kmsg(const char *fmt, ...)
{
    va_list args;
    int fd;
    char buf[512];
    int len;

    fd = open("/dev/kmsg", O_WRONLY);
    if (fd < 0) {
        /* Fall back to stderr if kmsg not available */
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
        fprintf(stderr, "\n");
        return;
    }

    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);

    if (len > 0) {
        buf[len] = '\n';
        write(fd, buf, len + 1);
    }

    close(fd);
}

static void die(const char *msg)
{
    kmsg("FATAL: %s: %s", msg, strerror(errno));
    kmsg("Dropping to emergency shell for debugging...");
    kmsg("Type 'exit' or press Ctrl+D to reboot");

    /* Try to exec a shell */
    char *shell_argv[] = {"/bin/sh", NULL};
    execv("/bin/sh", shell_argv);

    /* If exec fails, just exit */
    kmsg("Failed to exec /bin/sh: %s", strerror(errno));
    exit(1);
}


static int mount_fs(const char *source, const char *target, const char *fstype, unsigned long flags)
{
    debug("Mounting %s -> %s (type: %s)", source, target, fstype);
    if (mount(source, target, fstype, flags, NULL) < 0) {
        kmsg("Failed to mount %s on %s: %s", source, target, strerror(errno));
        return -1;
    }
    return 0;
}

static int mkdir_p(const char *path, mode_t mode)
{
    struct stat st;

    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        kmsg("Path exists but is not a directory: %s", path);
        return -1;
    }

    if (mkdir(path, mode) < 0) {
        if (errno != EEXIST) {
            kmsg("Failed to create directory %s: %s", path, strerror(errno));
            return -1;
        }
    }

    debug("Created directory: %s", path);
    return 0;
}

static int wait_for_device(const char *device, int timeout_seconds)
{
    struct stat st;
    int elapsed = 0;

    kmsg("Waiting for %s...", device);

    while (elapsed < timeout_seconds) {
        if (stat(device, &st) == 0 && S_ISBLK(st.st_mode)) {
            debug("Device %s is ready", device);
            return 0;
        }
        sleep(1);
        elapsed++;
    }

    kmsg("Timeout waiting for device %s", device);
    return -1;
}

static int run_command(const char *cmd, char *const argv[])
{
    pid_t pid;
    int status;
    int i;

    debug("Running command: %s", cmd);
    for (i = 0; argv[i] != NULL; i++) {
        debug("  argv[%d]: %s", i, argv[i]);
    }

    pid = fork();
    if (pid < 0) {
        kmsg("Failed to fork: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        execv(cmd, argv);
        kmsg("Failed to exec %s: %s", cmd, strerror(errno));
        _exit(127);
    }

    /* Parent process */
    if (waitpid(pid, &status, 0) < 0) {
        kmsg("Failed to wait for child: %s", strerror(errno));
        return -1;
    }

    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code != 0) {
            kmsg("Command %s exited with code %d", cmd, exit_code);
            return -1;
        }
        debug("Command %s completed successfully", cmd);
        return 0;
    } else if (WIFSIGNALED(status)) {
        kmsg("Command %s killed by signal %d", cmd, WTERMSIG(status));
        return -1;
    }

    return -1;
}

static int read_file_trim(const char *path, char *buf, size_t buf_size)
{
    FILE *fp;
    size_t len;

    fp = fopen(path, "r");
    if (!fp) {
        kmsg("Failed to open %s: %s", path, strerror(errno));
        return -1;
    }

    if (!fgets(buf, buf_size, fp)) {
        kmsg("Failed to read from %s", path);
        fclose(fp);
        return -1;
    }

    fclose(fp);

    /* Trim trailing whitespace */
    len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r' || buf[len-1] == ' ' || buf[len-1] == '\t')) {
        buf[--len] = '\0';
    }

    debug("Read from %s: %s", path, buf);
    return 0;
}

static int is_first_boot(void)
{
    int fd;
    unsigned char buf[FIRST_BOOT_CHECK_SIZE];
    ssize_t nread;
    size_t i;
    int all_zeros = 1;

    kmsg("Checking for first boot condition...");

    fd = open(DATA_DEVICE, O_RDONLY);
    if (fd < 0) {
        kmsg("Failed to open %s: %s", DATA_DEVICE, strerror(errno));
        return -1;
    }

    nread = read(fd, buf, sizeof(buf));
    close(fd);

    if (nread < 0) {
        kmsg("Failed to read from %s: %s", DATA_DEVICE, strerror(errno));
        return -1;
    }

    if ((size_t)nread != sizeof(buf)) {
        kmsg("Short read from %s: expected %zu, got %zd", DATA_DEVICE, sizeof(buf), nread);
        return -1;
    }

    /* Check if all bytes are zero */
    for (i = 0; i < (size_t)nread; i++) {
        if (buf[i] != 0) {
            all_zeros = 0;
            break;
        }
    }

    if (all_zeros) {
        kmsg("First boot detected: first 256 LBA blocks are zeroed");
        return 1;
    }

    debug("Not first boot: data device has been initialized");
    return 0;
}

static int mark_initialized(void)
{
    int fd;
    unsigned char marker[1];
    ssize_t nwritten;

    kmsg("Marking data device as initialized...");

    /* Write a simple marker to the first block */
    memset(marker, 0xFF, 1);
    snprintf((char *)marker, sizeof(marker), "INITIALIZED");

    fd = open(DATA_DEVICE, O_WRONLY);
    if (fd < 0) {
        kmsg("Failed to open %s for writing: %s", DATA_DEVICE, strerror(errno));
        return -1;
    }

    nwritten = write(fd, marker, sizeof(marker));
    close(fd);

    if (nwritten != sizeof(marker)) {
        kmsg("Failed to write marker to %s", DATA_DEVICE);
        return -1;
    }

    /* Sync to ensure it's written */
    sync();

    kmsg("Boot device marked as initialized");
    return 0;
}

static int generate_otp_key(void)
{
    char *argv[4];
    char key_hex[256];
    FILE *fp;
    int i;
    unsigned char key_bytes[32];

    kmsg("Generating new OTP key...");

    /* Generate a random 256-bit key */
    fp = fopen("/dev/urandom", "r");
    if (!fp) {
        kmsg("Failed to open /dev/urandom: %s", strerror(errno));
        return -1;
    }

    if (fread(key_bytes, 1, sizeof(key_bytes), fp) != sizeof(key_bytes)) {
        kmsg("Failed to read random data");
        fclose(fp);
        return -1;
    }
    fclose(fp);

    /* Convert to hex string */
    for (i = 0; i < 32; i++) {
        snprintf(key_hex + (i * 2), 3, "%02x", key_bytes[i]);
    }
    key_hex[64] = '\0';

    kmsg("Generated key: %.16s...", key_hex);

    /* Write key to OTP using rpi-otp-key */
    argv[0] = "rpi-otp-key";
    argv[1] = "-wy";
    argv[2] = key_hex;
    argv[3] = NULL;

    if (run_command("/usr/bin/rpi-otp-key", argv) < 0) {
        die("Failed to write OTP key");
    }

    kmsg("OTP key written successfully");
    return 0;
}

static int read_otp_key(unsigned char *key_out, size_t key_size)
{
    FILE *fp;

    kmsg("Reading OTP key...");

    fp = popen("/usr/bin/rpi-otp-key 2>&1", "r");
    if (!fp) {
        kmsg("Failed to execute rpi-otp-key: %s", strerror(errno));
        return -1;
    }

    size_t bytes_read = fread(key_out, 1, key_size, fp);
    int status = pclose(fp);

    if (bytes_read != key_size) {
        kmsg("Failed to read %zu bytes from rpi-otp-key (got %zu bytes)", key_size, bytes_read);
        if (status != 0) {
            kmsg("rpi-otp-key exited with status: %d", WEXITSTATUS(status));
        }
        kmsg("Check if /dev/vcio exists and vcmailbox is available");
        return -1;
    }

    if (status != 0) {
        kmsg("rpi-otp-key completed but exited with status: %d", WEXITSTATUS(status));
        return -1;
    }

    debug("OTP key read successfully");
    return 0;
}

static int write_key_file(const unsigned char *key, size_t key_size)
{
    FILE *fp;

    debug("Writing key to %s", KEY_FILE);

    fp = fopen(KEY_FILE, "wb");
    if (!fp) {
        kmsg("Failed to create key file: %s", strerror(errno));
        return -1;
    }

    if (fwrite(key, 1, key_size, fp) != key_size) {
        kmsg("Failed to write key file");
        fclose(fp);
        return -1;
    }

    fclose(fp);

    /* Set restrictive permissions */
    if (chmod(KEY_FILE, 0400) < 0) {
        kmsg("Failed to set key file permissions: %s", strerror(errno));
        return -1;
    }

    debug("Key file written successfully");
    return 0;
}

static int setup_dm_crypt(const char *source, const char *mapper_name, const char *key_file)
{
    char *argv[16];
    int arg_idx = 0;

    kmsg("Setting up dm-crypt for %s -> /dev/mapper/%s", source, mapper_name);

    argv[arg_idx++] = "cryptsetup";
    argv[arg_idx++] = "open";
    argv[arg_idx++] = "--type=plain";
    argv[arg_idx++] = "--cipher=aes-cbc-plain";
    argv[arg_idx++] = "--key-size=256";
    argv[arg_idx++] = "--key-file";
    argv[arg_idx++] = (char *)key_file;
    argv[arg_idx++] = "-q";
    argv[arg_idx++] = (char *)source;
    argv[arg_idx++] = (char *)mapper_name;
    argv[arg_idx] = NULL;

    if (run_command("/usr/sbin/cryptsetup", argv) < 0) {
        die("Failed to setup dm-crypt");
    }

    kmsg("dm-crypt setup complete: /dev/mapper/%s", mapper_name);
    return 0;
}

static int encrypt_rootfs_in_place(void)
{
    char *argv[16];
    int arg_idx;
    int fd;
    off_t root_size;
    char size_str[128];
    char tmp_file[256];
    char loop_device[64];
    char if_arg[64];
    char of_arg[128];
    char bs_arg[32];
    FILE *loop_fp;

    kmsg("Encrypting rootfs in place...");

    /* Get the size of the root device */
    fd = open(ROOT_DEVICE, O_RDONLY);
    if (fd < 0) {
        kmsg("Failed to open %s: %s", ROOT_DEVICE, strerror(errno));
        return -1;
    }

    /* Use lseek to get device size */
    root_size = lseek(fd, 0, SEEK_END);
    close(fd);

    if (root_size < 0) {
        kmsg("Failed to determine size of %s", ROOT_DEVICE);
        return -1;
    }

    kmsg("Root device size: %lld bytes (%lld MB)", (long long)root_size, (long long)(root_size / (1024 * 1024)));

    /* Remount tmpfs with enough space (already mounted, remount with size) */
    snprintf(size_str, sizeof(size_str), "remount,size=%lld", (long long)root_size + (100 * 1024 * 1024));
    if (mount("tmpfs", TMP_MOUNT, "tmpfs", MS_REMOUNT, size_str) < 0) {
        kmsg("Warning: Failed to remount tmpfs with larger size: %s", strerror(errno));
    }

    /* Step 1: Create a file in tmpfs to hold the encrypted data */
    snprintf(tmp_file, sizeof(tmp_file), "%s/encrypted_root.img", TMP_MOUNT);
    kmsg("Creating temporary file: %s", tmp_file);

    fd = open(tmp_file, O_RDWR | O_CREAT, 0600);
    if (fd < 0) {
        kmsg("Failed to create %s: %s", tmp_file, strerror(errno));
        return -1;
    }

    /* Allocate space for the file */
    if (ftruncate(fd, root_size) < 0) {
        kmsg("Failed to allocate space for %s: %s", tmp_file, strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);

    /* Step 2: Setup loopback device for the tmpfs file */
    kmsg("Setting up loop device...");

    snprintf(size_str, sizeof(size_str), "losetup -f --show %s 2>&1", tmp_file);
    loop_fp = popen(size_str, "r");
    if (!loop_fp || !fgets(loop_device, sizeof(loop_device), loop_fp)) {
        die("Failed to setup loop device");
    }
    pclose(loop_fp);

    /* Trim newline */
    loop_device[strcspn(loop_device, "\n")] = 0;
    kmsg("Loop device: %s", loop_device);

    /* Step 3: Setup dm-crypt on loop device */
    kmsg("Setting up dm-crypt on loop device...");
    setup_dm_crypt(loop_device, "tmpenc", KEY_FILE);

    /* Step 4: Copy plaintext rootfs through dm-crypt mapper (encrypts it) */
    kmsg("Copying rootfs through dm-crypt to tmpfs (this may take several minutes)...");

    snprintf(if_arg, sizeof(if_arg), "if=%s", ROOT_DEVICE);
    snprintf(of_arg, sizeof(of_arg), "of=/dev/mapper/tmpenc");
    snprintf(bs_arg, sizeof(bs_arg), "bs=4M");

    arg_idx = 0;
    argv[arg_idx++] = "dd";
    argv[arg_idx++] = if_arg;
    argv[arg_idx++] = of_arg;
    argv[arg_idx++] = bs_arg;
    argv[arg_idx] = NULL;

    if (run_command("/bin/dd", argv) < 0) {
        die("Failed to copy rootfs through dm-crypt");
    }

    /* Step 5: Close dm-crypt (we need the raw encrypted data) */
    kmsg("Closing dm-crypt mapper...");
    argv[0] = "cryptsetup";
    argv[1] = "close";
    argv[2] = "tmpenc";
    argv[3] = NULL;
    run_command("/usr/sbin/cryptsetup", argv);

    /* Step 6: Copy encrypted data from tmpfs file back to root device */
    kmsg("Writing encrypted rootfs back to %s...", ROOT_DEVICE);

    snprintf(if_arg, sizeof(if_arg), "if=%s", loop_device);
    snprintf(of_arg, sizeof(of_arg), "of=%s", ROOT_DEVICE);

    arg_idx = 0;
    argv[arg_idx++] = "dd";
    argv[arg_idx++] = if_arg;
    argv[arg_idx++] = of_arg;
    argv[arg_idx++] = bs_arg;
    argv[arg_idx] = NULL;

    if (run_command("/bin/dd", argv) < 0) {
        die("Failed to write encrypted rootfs");
    }

    /* Step 7: Clean up loop device */
    kmsg("Cleaning up loop device...");
    argv[0] = "losetup";
    argv[1] = "-d";
    argv[2] = loop_device;
    argv[3] = NULL;
    run_command("/sbin/losetup", argv);

    /* Remove temporary file */
    unlink(tmp_file);

    sync();
    kmsg("Rootfs encryption complete");
    return 0;
}

int main(int argc, char *argv[])
{
    char root_hash[256];
    char verity_offset[64];
    char *mcopy_argv[7];
    char *veritysetup_argv[16];
    char *cryptsetup_argv[16];
    int arg_idx;
    int first_boot;
    unsigned char otp_key[32];

    kmsg("====== Starting %s version %s", PROGRAM_NAME, PROGRAM_VERSION);

    /* Mount essential filesystems */
    kmsg("Mounting essential filesystems...");

    if (mount_fs("none", "/dev", "devtmpfs", MS_NOEXEC | MS_NOSUID) < 0) {
        die("Failed to mount /dev");
    }

    if (mount_fs("proc", "/proc", "proc", MS_NOEXEC | MS_NOSUID | MS_NODEV) < 0) {
        die("Failed to mount /proc");
    }

    if (mount_fs("sysfs", "/sys", "sysfs", MS_NOEXEC | MS_NOSUID | MS_NODEV) < 0) {
        die("Failed to mount /sys");
    }

    /* Wait for /dev/vcio device (needed by vcmailbox for OTP access) */
    if (wait_for_vcio_device(10) < 0) {
        kmsg("Warning: /dev/vcio not available - OTP operations may fail");
    }

    /* Check for first boot */
    first_boot = is_first_boot();
    if (first_boot < 0) {
        die("Failed to check first boot condition");
    }

    if (first_boot) {
        kmsg("=== FIRST BOOT DETECTED ===");
        kmsg("Performing initial setup...");

        /* Generate OTP key */
        if (generate_otp_key() < 0) {
            die("Failed to generate OTP key");
        }

        /* Mark boot as initialized */
        if (mark_initialized() < 0) {
            die("Failed to mark boot as initialized");
        }
    }

    /* Wait for root device to appear */
    if (wait_for_device(ROOT_DEVICE, 30) < 0) {
        die("Root device did not appear");
    }

    /* Create and mount boot partition */
    kmsg("Mounting the /boot partition...");
    if (mkdir_p(BOOT_MOUNT, 0755) < 0) {
        die("Failed to create boot mount point");
    }

    if (mount_fs(BOOT_DEVICE, BOOT_MOUNT, "vfat", MS_RDONLY) < 0) {
        die("Failed to mount boot partition");
    }

    /* Extract dm-verity metadata from boot.img */
    kmsg("Extracting dm-verity root hash and partition offset...");

    /* Run: mcopy -i /boot/boot.img ::root_hash.txt ::verity_offset.txt / */
    mcopy_argv[0] = "mcopy";
    mcopy_argv[1] = "-i";
    mcopy_argv[2] = BOOT_IMAGE;
    mcopy_argv[3] = "::root_hash.txt";
    mcopy_argv[4] = "::verity_offset.txt";
    mcopy_argv[5] = "/";
    mcopy_argv[6] = NULL;

    if (run_command("/usr/bin/mcopy", mcopy_argv) < 0) {
        die("Failed to extract verity metadata from boot.img");
    }

    /* Read the extracted files */
    if (read_file_trim(ROOT_HASH_FILE, root_hash, sizeof(root_hash)) < 0) {
        die("Failed to read root hash");
    }

    if (read_file_trim(VERITY_OFFSET_FILE, verity_offset, sizeof(verity_offset)) < 0) {
        die("Failed to read verity offset");
    }

    /* Read OTP key */
    if (read_otp_key(otp_key, sizeof(otp_key)) < 0) {
        die("Failed to read OTP key");
    }

    /* Mount tmpfs for key file */
    if (mkdir_p(TMP_MOUNT, 0755) < 0) {
        die("Failed to create tmp mount point");
    }

    if (mount_fs("tmpfs", TMP_MOUNT, "tmpfs", MS_NOEXEC | MS_NOSUID | MS_NODEV) < 0) {
        die("Failed to mount tmpfs");
    }

    /* Write key to file */
    if (write_key_file(otp_key, sizeof(otp_key)) < 0) {
        die("Failed to write key file");
    }

    /* If first boot, encrypt the rootfs in place */
    if (first_boot) {
        kmsg("=== ENCRYPTING ROOTFS ===");
        if (encrypt_rootfs_in_place() < 0) {
            die("Failed to encrypt rootfs");
        }
    }

    /* Setup dm-verity */
    kmsg("Setting up verity rootfs mapper...");
    kmsg("Mapper: %s", VERITY_MAPPER_PATH);
    kmsg("Device: %s", ROOT_DEVICE);
    kmsg("Root hash: %s", root_hash);
    kmsg("Hash offset: %s", verity_offset);

    /* Build veritysetup command with proper hash-offset formatting */
    char hash_offset_arg[128];
    snprintf(hash_offset_arg, sizeof(hash_offset_arg), "--hash-offset=%s", verity_offset);

    arg_idx = 0;
    veritysetup_argv[arg_idx++] = "veritysetup";
    veritysetup_argv[arg_idx++] = "open";
    veritysetup_argv[arg_idx++] = ROOT_DEVICE;
    veritysetup_argv[arg_idx++] = VERITY_MAPPER_NAME;
    veritysetup_argv[arg_idx++] = ROOT_DEVICE;
    veritysetup_argv[arg_idx++] = root_hash;
    veritysetup_argv[arg_idx++] = hash_offset_arg;
    veritysetup_argv[arg_idx] = NULL;

    if (run_command("/usr/sbin/veritysetup", veritysetup_argv) < 0) {
        die("Failed to setup dm-verity");
    }

    /* Setup dm-crypt on top of dm-verity */
    kmsg("Setting up dm-crypt on top of dm-verity...");
    setup_dm_crypt(VERITY_MAPPER_PATH, CRYPT_MAPPER_NAME, KEY_FILE);

    /* Mount the verified and decrypted root filesystem */
    kmsg("Mounting mapper as /media/root...");
    if (mkdir_p(ROOT_MOUNT, 0755) < 0) {
        die("Failed to create root mount point");
    }

    if (mount_fs(CRYPT_MAPPER_PATH, ROOT_MOUNT, "squashfs", MS_RDONLY) < 0) {
        die("Failed to mount verified root filesystem");
    }

    /* Clean up sensitive data */
    unlink(KEY_FILE);
    memset(otp_key, 0, sizeof(otp_key));

    /* Switch to the real root */
    kmsg("Switching from initramfs to rootfs on %s, running %s...", ROOT_DEVICE, NEXT_INIT);

    /* Unmount filesystems that were only needed for init */
    umount(TMP_MOUNT);
    umount("/boot");
    umount("/proc");
    umount("/sys");

    /* Perform switch_root */
    if (chdir(ROOT_MOUNT) < 0) {
        die("Failed to chdir to new root");
    }

    if (mount(ROOT_MOUNT, "/", NULL, MS_MOVE, NULL) < 0) {
        die("Failed to move root mount");
    }

    if (chroot(".") < 0) {
        die("Failed to chroot to new root");
    }

    if (chdir("/") < 0) {
        die("Failed to chdir to / after chroot");
    }

    /* Execute the real init */
    execl(NEXT_INIT, NEXT_INIT, NULL);

    /* If we get here, exec failed */
    die("Failed to exec real init");

    return 1;
}
