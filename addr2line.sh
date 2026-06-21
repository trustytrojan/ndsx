#!/bin/sh
[ $1 ] || { echo 'address required'; exit 1; }
# $WONDERFUL_TOOLCHAIN/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-addr2line -e build/*.elf $1
# $WONDERFUL_TOOLCHAIN/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-addr2line -e build/dash_lib.elf $1
for elf in build/*.elf; do
	echo -n "$elf: "
	$WONDERFUL_TOOLCHAIN/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-addr2line -e $elf $1
done