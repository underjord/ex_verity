# verity_init

Init program for dm-verity verified boot with dm-crypt encryption on Raspberry Pi.

## Overview

This is a minimal init program written in C that runs as PID 1 in the initramfs. It is responsible for:

1. Mounting essential filesystems (`/dev`, `/proc`, `/sys`)
2. Detecting first boot condition (first 256 LBA of boot device are zeroed)
3. On first boot:
   - Generating a random 256-bit encryption key
   - Writing the key to Raspberry Pi OTP memory using `rpi-otp-key`
   - Encrypting the rootfs in-place
   - Marking the boot device as initialized
4. Reading the encryption key from OTP memory
5. Waiting for the root block device to appear
6. Mounting the boot partition
7. Extracting dm-verity metadata (root hash and hash offset) from `boot.img`
8. Setting up the dm-verity device mapper for verified boot
9. Setting up dm-crypt on top of dm-verity for encryption
10. Optionally setting up and formatting a data partition with F2FS
11. Mounting the verified and encrypted root filesystem
12. Switching to the real root filesystem and executing the next init

## Purpose

This replaces a shell script init with a compiled C program to provide:

- **Verified Boot**: dm-verity ensures the rootfs hasn't been tampered with
- **Encryption**: dm-crypt provides encryption at rest using hardware-backed keys
- **Hardware Key Storage**: Uses Raspberry Pi OTP (One-Time Programmable) memory
- **Faster boot times**: No shell interpreter overhead
- **More reliable error handling**: Direct system calls with proper error checking
- **Reduced dependencies**: Minimal requirements in the initramfs
- **Better logging**: All output goes to kernel message buffer

## Security Architecture

The security model uses a layered approach:

```
User Space
    └─> /dev/mapper/cryptroot (dm-crypt, encrypted)
            └─> /dev/mapper/verity (dm-verity, verified)
                    └─> /dev/mmcblk0p2 (raw block device)
```

### Key Management

- **Key Generation**: On first boot, a 256-bit random key is generated from `/dev/urandom`
- **Key Storage**: The key is written to Raspberry Pi OTP memory (cannot be read by normal means on Pi 5+)
- **Key Retrieval**: During boot, the key is read using the `rpi-otp-key` utility
- **Key Protection**: The key file is stored in tmpfs (RAM) with 0400 permissions and deleted after use

### First Boot Detection

First boot is detected by checking if the first 256 LBA blocks (128 KB) of the boot device are all zeros. On first boot:

1. A marker is written to the boot device
2. A new OTP key is generated and written
3. The rootfs is copied through dm-crypt to encrypt it in place
4. The system proceeds with normal boot

## Configuration

The program uses compile-time constants that can be modified in `verity_init.c`:

- `BOOT_DEVICE` - Boot partition device (default: `/dev/mmcblk0p1`)
- `ROOT_DEVICE` - Root partition device (default: `/dev/mmcblk0p2`)
- `DATA_DEVICE` - Optional data partition (default: `/dev/mmcblk0p3`)
- `BOOT_IMAGE` - Path to boot image (default: `/boot/boot.img`)
- `VERITY_MAPPER_NAME` - dm-verity mapper name (default: `verity`)
- `CRYPT_MAPPER_NAME` - dm-crypt mapper name (default: `cryptroot`)
- `DATA_MAPPER_NAME` - Data partition mapper name (default: `data`)
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
- `cryptsetup` (from cryptsetup) - To setup dm-crypt encryption
- `rpi-otp-key` - Custom utility to read/write Raspberry Pi OTP memory
- `mkfs.f2fs` (from f2fs-tools) - To format data partition (optional)
- `losetup` - To setup loop devices for encryption
- `dd` - For copying data

## Installation

The program installs as `/sbin/init` in the initramfs, which is automatically executed by the kernel as PID 1 during boot.

## Boot Process

### First Boot

```
Kernel
  └─> /sbin/init (verity_init)
       ├─> Detect first boot (check first 256 LBA of boot device)
       ├─> Generate 256-bit random key
       ├─> Write key to OTP using rpi-otp-key
       ├─> Mark boot device as initialized
       ├─> Copy rootfs through dm-crypt to encrypt in place
       ├─> Continue with normal boot...
```

### Normal Boot

```
Kernel
  └─> /sbin/init (verity_init)
       ├─> Mount /dev, /proc, /sys
       ├─> Read encryption key from OTP using rpi-otp-key
       ├─> Wait for /dev/mmcblk0p2
       ├─> Mount /dev/mmcblk0p1 to /boot
       ├─> Extract root_hash.txt and verity_offset.txt from /boot/boot.img
       ├─> Setup dm-verity mapper at /dev/mapper/verity
       ├─> Setup dm-crypt on /dev/mapper/verity -> /dev/mapper/cryptroot
       ├─> Setup data partition at /dev/mapper/data (if present)
       ├─> Mount /dev/mapper/cryptroot to /media/root (read-only, verified, encrypted)
       ├─> Clean up key file
       ├─> switch_root to /media/root
       └─> exec /sbin/init_sh
```

## Data Partition

If a third partition (`/dev/mmcblk0p3`) is present, it will be:

1. Encrypted using the same OTP key via dm-crypt (plain mode)
2. Formatted with F2FS filesystem
3. Available as `/dev/mapper/data`

This provides an encrypted, writable partition for application data while keeping the rootfs read-only and verified.

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
- Command execution details with arguments
- File read operations
- Key operations (not the key values themselves)
- Mapper setup details

## Security Considerations

### Strengths

- **Verified Boot**: dm-verity ensures rootfs integrity, preventing tampering
- **Encryption at Rest**: All data is encrypted on disk
- **Hardware-Backed Keys**: OTP memory provides secure key storage
- **Read-Only Root**: Rootfs is mounted read-only, reducing attack surface
- **Memory-Only Key Handling**: Key file only exists in tmpfs (RAM)

### Limitations

- **First Boot Vulnerability**: The first boot process could be intercepted before OTP key is written
- **Boot Partition**: The boot partition itself is not encrypted or verified (secure boot would address this)
- **OTP Readability**: On Pi 4 and earlier, OTP may be readable by root processes
- **Physical Access**: An attacker with physical access could potentially extract the OTP key on older Pi models
- **No Key Escrow**: If OTP key is lost/corrupted, data is unrecoverable

### Recommendations

1. Use Raspberry Pi 5 or newer for better OTP security
2. Enable secure boot to protect the boot chain
3. Set `lock_device_private_key=1` in config.txt on Pi 5+
4. Perform first boot in a secure environment
5. Consider additional authentication mechanisms for sensitive deployments
6. Keep backups of critical data before first boot

## Performance Impact

- **First Boot**: Significantly longer (encrypting entire rootfs)
- **Normal Boot**: Slight overhead from dm-verity verification (~5-10% slower)
- **Runtime**: Minimal overhead from dm-crypt for I/O operations
- **CPU**: AES hardware acceleration used when available

## Troubleshooting

### First boot never completes

Check that:
- Sufficient tmpfs space is available (needs 2x rootfs size)
- All required tools are present in initramfs
- OTP memory is not already programmed

### Key read fails

- Verify `rpi-otp-key` is working: `/usr/bin/rpi-otp-key -c`
- Check that OTP key was written on first boot
- Ensure Pi model supports OTP (Pi 4 and newer)

### Mount fails after encryption

- Check that dm-verity metadata matches the encrypted rootfs
- Verify the root hash and offset are correct
- Ensure the encryption completed successfully on first boot

## License

Apache License 2.0