#!/bin/bash

set -e

SCRIPT_NAME=$0

usage() {
    echo "Usage: $SCRIPT_NAME filesystem_image_path root_hash_output_path [block_size]"
}

fs_path=$1
root_hash_path=$2
block_size=${3:-4096}
# grab the size of the filesystem image as the offset for appending hash data
data_size=$(stat -c %s $fs_path)

if [[ $# -lt 1 ]]; then
    echo "$SCRIPT_NAME: ERROR: Expecting release directory"
    usage
    exit 1
fi

which veritysetup > /dev/null \
  || echo "veritysetup was not found, install it and make sure it is in the path"

# Generate a hash-tree for $fs_path and save it to $fs_path at the offset
# which actually means we append to the file making it larger
# save the root hash (the "key" for validating the hash tree) to a file
veritysetup format "$fs_path" "$fs_path" \
  --data-block-size=4096 \
  --hash-offset=$data_size \
  --root-hash-file="$root_hash_path"

