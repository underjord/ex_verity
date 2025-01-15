# ExVerity

A library for signing a Nerves firmware image. It provides a small script, you
could vendor it. Also offers igniter to support with inserting the right config.

Requires a version of `nerves` and `nerves_system_br` which includes the
necessary post-processing hooks for `mix firmware` and `rel2fw.sh`.

## Installation

Using [igniter](https://hex.pm/packages/igniter):

```
mix igniter.install ex_verity
```

Or classic, by adding it to your deps.

```elixir
def deps do
  #..
    {:ex_verity, "~> 0.1.0"}
end
```

## Basic usage of veritysetup

Some examples of what you can use veritysetup and the device mapper to do.

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