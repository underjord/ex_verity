# ExVerity

A library for signing a Nerves firmware image. It provides a small script, you
could vendor it. Also offers igniter to support with inserting the right config.

Requires a version of `nerves` and `nerves_system_br` which includes the
necessary post-processing hooks for `mix firmware` and `rel2fw.sh`.

## Installation

ExVerity is a build-time tool for signing and encrypting your Nerves firmware.

Start by adding it to your deps in `mix.exs`:

```elixir
def deps do
  #..
    {:ex_verity, "~> 0.1.0", runtime: false}
end
```

Then you need to make several modifications to your project:

- Add custom `fwup.conf` and `fwup_include` to handle a larger boot
  partition. Provided under `deps/ex_verity/priv/rpi4` and ideally
  kept under `config/rpi4/`.
- Override `/etc/erlinit.config` if you want to mount an encrypted data partition. Available in `deps/ex_verity/priv/rpi4/rootfs_overlay/etc/erlinit.config`. Ideally use a rootfs_overlay folder specific to the device: `overlays/rpi4/rootfs_overlay/`.
- Add `/sbin/init_sh` for initramfs switch over to, available in `deps/ex_verity/priv/rpi4/rootfs_overlay/sbin/init_sh`.
- Copy from `deps/ex_verity/priv/process_firmware` to `priv/ex_verity/process_firmware`
  so you have a known location for a script to hook into the firmware build.

Configuration changes in `target.exs`:

```
config :nerves, :firmware,
  post_processing_script: Path.expand("priv/ex_verity/process_firmware")

# We are not keeping key paths in the config. 
# Environment variables are easier to automate and adapt to each
# developer machine
config :ex_verity,
  # This can generate a root filesystem with a signed root hash and
  # hash tree for dm-verity use
  # You still need a device that has a secured boot mechanism to deliver
  # the root hash in a trusted manner.
  rootfs: [
    private_key_path: System.fetch_env!("EX_VERITY_ROOTFS_PRIVATE_KEY_PATH"),
    public_key_path: System.fetch_env!("EX_VERITY_ROOTFS_PUBLIC_KEY_PATH")
  ]

# ..
# Uncomment this:
import_config "#{Mix.target()}.exs"
```

Create `config/rpi4.exs`:

```
import Config

config :ex_verity,
  rpi4_secure_boot: [
    public_key_path: System.fetch_env!("EX_VERITY_RPI4_BOOT_PUBLIC_KEY_PATH"),
    private_key_path: System.fetch_env!("EX_VERITY_RPI4_BOOT_PRIVATE_KEY_PATH"),
    # Not necessary to set but available if customizing your initramfs
    # initramfs_path: "./deps/ex_verity/priv/initramfs/rpi4-initramfs.gz"
  ]

config :nerves, :firmware,
  rootfs_overlay: "overlays/rpi4/rootfs_overlay"
```

## Supported boards

The system currently supports:

### Raspberry Pi 4 (tested on CM4 primarily)

The procedure hooks into the `mix firmware` build step via the `post_processing_script` option. It will:

1. Generate a specialized `initramfs` from `priv/initramfs` using `./build-one.sh rpi4`.
2. Generate a signed root filesystem via veritysetup producing a hash tree
   appended to the root filesystem and a root hash that can be packaged
   for the boot partition.
3. Package the root hash for the root filesystem, the Linux kernel
   from your Nerves system along with other supporting files into
   `boot.img`.
4. Replicate procedure from `rpi-eeprom-digest` to produce a 
   signature `boot.sig`, a signature of the hash of `boot.img`.
5. Generate a `fwup` .fw file containing the boot partition with
   `boot.img` and the now signed root filesystem.

Current limitations:

- The encryption feature is not done.
- There is no safe storage for secret on the Raspberry Pi boards. 
  Data can be encrypted but the secret cannot be very deeply secured.
  We plan to provide ways to store a key in the CM4 One-Time
  Programmable storage and possibly in a secure element. This will be
  sufficient for some purposes but has fundamental flaws if there is
  not additional physical security.

When the board boots, this is the security model:

1. The RPi4 and CM4 can have their bootloader locked with a burned-in
   certificate. This tooling currently will not do that procedure.
   See [usbboot](https://github.com/raspberrypi/usbboot) documentation
   for examples. Here we trust Raspberry Pi and Broadcom to have done
   things right. Because we have no choice. The certificate gets is
   locked into the OTP.
2. On booting the bootloader verifies the `boot.img` on the boot
   partition against the locked certificate. If it is properly
   signed it will start from the `boot.img` that is now considered
   trusted. It contains two critical things: an initramfs (code) and
   the `dm-verity` root hash for the root filesystem.
3. The initramfs code is started and runs. This code was in
   `boot.img` and so is trusted. It takes the trusted root hash and
   configures the device mapper for the root filesystem. It then
   mounts the root filesystem and switches to run from it.
4. Optionally the `initramfs` can prepare device mappers for
   encrypting the data or root filesystem. Here we lack satisfactory
   key storage and to some extent rely on obscurity. Of course it is
   also solid protection against opportunistic attacks or accidental
   exposure.
5. The root filesystem is now trusted as it is running through the
   device mapper and any unsigned modifications of the filesystem will
   not be readable through the device mapper. We are now running a
   trusted Nerves system.

### More boards?

More boards will be supported as companies need it. Reach out to us at Underjord if you are interested in support for the hardware you need. We're getting pretty good at this.

## Basic usage of veritysetup

Some short examples of what you can use veritysetup and the device
mapper to do. These are the fundamentals used to secure the root
filesystem.

```
###### Create a hash tree for a disk image
# requires the filesystem image
# produces a root hash and an offset for the hash tree information

export FS_PATH=./data.img
# grab the size of the filesystem image as the offset for appending hash data
export DATA_SIZE=$(stat -c %s $FS_PATH)

# Generate a hash-tree for $FS_PATH and save it to $FS_PATH at the offset
# which actually means we append to the file making it larger
# save the root hash (the "key" for validating the hash tree) to a file
veritysetup format $FS_PATH $FS_PATH \
  --data-block-size=4096 \
  --hash-offset=$DATA_SIZE \
  --root-hash-file=root-hash.txt

###### Mount a disk image based on the root hash and hash tree location
# requires root hash and offset information

# grab the root hash
export ROOT_HASH=$(cat root-hash.txt)

export MAPPER_NAME="verity-test"

# verity-test is just a name or label that will be used in /dev/mapper
veritysetup open $FS_PATH $MAPPER_NAME $FS_PATH \
  $ROOT_HASH \
  --hash-offset=$DATA_SIZE

mkdir mnt
mount /dev/mapper/$MAPPER_NAME mnt

###### Un-mount and close
umount mnt
veritysetup close $MAPPER_NAME
```