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
#define BOOT_MOUNT "/boot"
#define BOOT_IMAGE "/boot/boot.img"
#define ROOT_HASH_FILE "/root_hash.txt"
#define VERITY_OFFSET_FILE "/verity_offset.txt"
#define MAPPER_NAME "verity"
#define MAPPER_PATH "/dev/mapper/" MAPPER_NAME
#define ROOT_MOUNT "/media/root"
#define NEXT_INIT "/sbin/init_sh"
#define DATA_SIZE 4096

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
    /* Drop to a shell for debugging if possible */
    kmsg("Attempting to spawn emergency shell...");
    execl("/bin/sh", "/bin/sh", NULL);
    /* If that fails, just hang */
    while (1) {
        sleep(3600);
    }
}

static void die_simple(const char *msg)
{
    kmsg("FATAL: %s", msg);
    /* Drop to a shell for debugging if possible */
    kmsg("Attempting to spawn emergency shell...");
    execl("/bin/sh", "/bin/sh", NULL);
    /* If that fails, just hang */
    while (1) {
        sleep(3600);
    }
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

    debug("Running command: %s", cmd);

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

int main(int argc, char *argv[])
{
    char root_hash[256];
    char verity_offset[64];
    char *mcopy_argv[7];
    char *veritysetup_argv[16];
    int arg_idx;

    kmsg("Starting %s version %s", PROGRAM_NAME, PROGRAM_VERSION);

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

    /* Wait for root device to appear */
    if (wait_for_device(ROOT_DEVICE, 30) < 0) {
        die_simple("Root device did not appear");
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
        die_simple("Failed to extract verity metadata from boot.img");
    }

    /* Read the extracted files */
    if (read_file_trim(ROOT_HASH_FILE, root_hash, sizeof(root_hash)) < 0) {
        die_simple("Failed to read root hash");
    }

    if (read_file_trim(VERITY_OFFSET_FILE, verity_offset, sizeof(verity_offset)) < 0) {
        die_simple("Failed to read verity offset");
    }

    /* Setup dm-verity */
    kmsg("Setting up verity rootfs mapper...");
    kmsg("Mapper: %s", MAPPER_PATH);
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
    veritysetup_argv[arg_idx++] = MAPPER_NAME;
    veritysetup_argv[arg_idx++] = ROOT_DEVICE;
    veritysetup_argv[arg_idx++] = root_hash;
    veritysetup_argv[arg_idx++] = hash_offset_arg;
    veritysetup_argv[arg_idx] = NULL;

    if (run_command("/usr/sbin/veritysetup", veritysetup_argv) < 0) {
        die_simple("Failed to setup dm-verity");
    }

    /* Mount the verified root filesystem */
    kmsg("Mounting mapper as /media/root...");
    if (mkdir_p(ROOT_MOUNT, 0755) < 0) {
        die("Failed to create root mount point");
    }

    if (mount_fs(MAPPER_PATH, ROOT_MOUNT, "ext4", MS_RDONLY) < 0) {
        die("Failed to mount verified root filesystem");
    }

    /* Switch to the real root */
    kmsg("Switching from initramfs to rootfs on %s, running %s...", ROOT_DEVICE, NEXT_INIT);

    /* Unmount filesystems that were only needed for init */
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