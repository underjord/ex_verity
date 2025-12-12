# verity_init

Init program for dm-verity verified boot initramfs on Raspberry Pi.

## Overview

This is a minimal init program written in C that runs as PID 1 in the initramfs. It is responsible for:

1. Mounting essential filesystems (`/dev`, `/proc`, `/sys`)
2. Waiting for the root block device to appear
3. Mounting the boot partition
4. Extracting dm-verity metadata (root hash and hash offset) from `boot.img`
5. Setting up the dm-verity device mapper
6. Mounting the verified root filesystem
7. Switching to the real root filesystem and executing the next init

## Purpose

This replaces a shell script init with a compiled C program to provide:

- Faster boot times (no shell interpreter overhead)
- More reliable error handling
- Reduced dependencies in the initramfs
- Better logging to kernel message buffer

## Configuration

The program uses compile-time constants that can be modified in `verity_init.c`:

- `BOOT_DEVICE` - Boot partition device (default: `/dev/mmcblk0p1`)
- `ROOT_DEVICE` - Root partition device (default: `/dev/mmcblk0p2`)
- `BOOT_IMAGE` - Path to boot image (default: `/boot/boot.img`)
- `MAPPER_NAME` - Device mapper name (default: `verity`)
- `NEXT_INIT` - Next init to execute (default: `/sbin/init_sh`)

## Building

This package is built as part of the buildroot system. To enable it:

1. Run `make menuconfig` in your buildroot build directory
2. Navigate to "External options"
3. Enable "verity_init"
4. Optionally enable "verity_init debug" for verbose logging

## Dependencies

The program requires these tools to be present in the initramfs:

- `mcopy` (from mtools) - To extract files from FAT filesystem images
- `veritysetup` (from cryptsetup) - To setup dm-verity device mapper

## Installation

The program installs as `/sbin/init` in the initramfs, which is automatically executed by the kernel as PID 1 during boot.

## Boot Process

```
Kernel
  └─> /sbin/init (verity_init)
       ├─> Mount /dev, /proc, /sys
       ├─> Wait for /dev/mmcblk0p2
       ├─> Mount /dev/mmcblk0p1 to /boot
       ├─> Extract root_hash.txt and verity_offset.txt from /boot/boot.img
       ├─> Setup dm-verity mapper at /dev/mapper/verity
       ├─> Mount /dev/mapper/verity to /media/root (read-only, verified)
       ├─> switch_root to /media/root
       └─> exec /sbin/init_sh
```

## Error Handling

On fatal errors, the program:

1. Logs the error to `/dev/kmsg` (kernel message buffer)
2. Attempts to spawn an emergency shell (`/bin/sh`) for debugging
3. If that fails, enters an infinite sleep loop

This ensures the system doesn't panic but allows for debugging in development.

## Debug Mode

When compiled with `DEBUG=1`, the program outputs detailed progress information to `/dev/kmsg`. Enable this in buildroot with the "verity_init debug" option.

Debug messages include:

- Mount operations and their parameters
- Device wait states
- Command execution details
- File read operations

## License

Apache License 2.0