#!/bin/sh
[ $1 ] || { echo 'address required'; exit 1; }
$WONDERFUL_TOOLCHAIN/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-addr2line -e build/ndsx.elf $1