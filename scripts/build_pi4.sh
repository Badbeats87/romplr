#!/bin/bash
set -e
set -x

export PATH="/Users/invision/arm-gnu-toolchain-15.2.rel1-darwin-arm64-aarch64-none-elf/bin:/opt/homebrew/opt/gnu-getopt/bin:/usr/local/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"

cd /Users/invision/mini-jv880/circle-stdlib

# Clean and reconfigure
make mrproper || true
./configure -r 4 --prefix "aarch64-none-elf-" \
    -o USE_PWM_AUDIO_ON_ZERO \
    -o SAVE_VFP_REGS_ON_IRQ \
    -o REALTIME \
    -o SCREEN_DMA_BURST_LENGTH=1 \
    -o ARM_ALLOW_MULTI_CORE \
    -o KERNEL_MAX_SIZE=0x400000 \
    -o _LDBL_EQ_DBL

# Build circle core (ignore WLAN failure)
cd libs/circle && ./makeall --nosample || true
cd ../..
make -C libs/circle/addon/SDCard
make -C libs/circle/addon/fatfs

# Build newlib (complex long-double may fail, that's OK)
make newlib || true

# Fix up complex/lib.a if needed
COMPLEX_DIR="build/circle-newlib/aarch64-none-circle/newlib/libm/complex"
if [ ! -f "$COMPLEX_DIR/lib.a" ]; then
    echo "=== Fixing up complex/lib.a ==="
    cd "$COMPLEX_DIR"
    if ls *.o 1>/dev/null 2>&1; then
        aarch64-none-elf-ar rc lib.a *.o
        aarch64-none-elf-ranlib lib.a
    else
        aarch64-none-elf-ar rc lib.a
    fi
    cd /Users/invision/mini-jv880/circle-stdlib
fi

# Build libm.a manually
LIBM_DIR="build/circle-newlib/aarch64-none-circle/newlib/libm"
cd "$LIBM_DIR"
rm -f libm.a
rm -rf tmp && mkdir tmp && cd tmp
for i in math/lib.a common/lib.a complex/lib.a fenv/lib.a machine/lib.a; do
    aarch64-none-elf-gcc-ar x ../$i
done
aarch64-none-elf-gcc-ar rc ../libm.a *.o
aarch64-none-elf-gcc-ranlib ../libm.a
cd .. && rm -rf tmp
echo "=== libm.a built ==="

# Run newlib install
cd /Users/invision/mini-jv880/circle-stdlib
make -C build/circle-newlib install || true

# Build circle addons
cd libs/circle/addon/display/
make clean || true
make -j
cd ../sensor/
make clean || true
make -j
cd ../Properties/
make clean || true
make -j
cd ../../../..
cd ..

# Build MiniJV880
cd src
make clean || true
make -j
ls *.img
echo "=== BUILD SUCCESSFUL ==="
