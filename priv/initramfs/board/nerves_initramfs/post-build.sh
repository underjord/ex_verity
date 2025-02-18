#!/bin/sh

set -e

# The default configuration only requires a subset
# of the files in the target directory.
# The default files are listed here:
FILES="init"

# If fwup is present in the target directory, include that
if [ -e "$TARGET_DIR/usr/bin/fwup" ]; then
    FILES="$FILES\nusr\nusr/bin\nusr/bin/fwup"
fi

echo "Rootfs CPIO Files: "
echo "-------------------"
echo $FILES
echo "-------------------"

mkdir -p "$BINARIES_DIR"
cp "${ABS_DEFCONFIG_DIR}/${TARGET_NAME}/init" "${TARGET_DIR}/init"
cp "${ABS_DEFCONFIG_DIR}/${TARGET_NAME}/rpi-otp-private-key" "${TARGET_DIR}/bin/rpi-otp-private-key"
