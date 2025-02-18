#!/bin/bash

# configs/xyz_defconfig
export TARGET_NAME="$1"
config="configs/${TARGET_NAME}_defconfig"

echo "Creating $config..."

# "readlink -f" implementation for BSD
# This code was extracted from the Elixir shell scripts
readlink_f () {
    cd "$(dirname "$1")" > /dev/null
    filename="$(basename "$1")"
    if [[ -h "$filename" ]]; then
        readlink_f "$(readlink "$filename")"
    else
        echo "$(pwd -P)/$filename"
    fi
}

abs_config=$(readlink_f "$config")
export ABS_DEFCONFIG_DIR=$(dirname "$abs_config")
./create-build.sh $config
if [[ $? != 0 ]]; then
    echo "--> './create-build.sh $config' failed!"
    exit 1
fi

base=$(basename -s _defconfig $config)

echo "Building $base..."

# If rebuilding, force a rebuild of nerves_initramfs.
rm -fr "o/$base/build/nerves_initramfs"

make -C o/$base
if [[ $? != 0 ]]; then
    echo "--> 'Building $base' failed!"
    exit 1
fi

cp "o/$base/images/rootfs.cpio.gz" "./$base-initramfs.gz"

