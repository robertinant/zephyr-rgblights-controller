#!/bin/bash

objcopy="/opt/zephyr-sdk-0.16.3/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-objcopy"
args="--input-target=binary  --output-target=elf32-little"
bootloader="build/esp-idf/build/bootloader/bootloader.bin"
partitions="build/esp-idf/build/partitions_singleapp.bin"
application="build/zephyr/FA2-Product-Template.bin"

# Do the conversions using objcopy
echo "Converting bootloader"
eval "${objcopy} ${args} ${bootloader} ${bootloader}.elf"
echo "Converting partitions"
eval "${objcopy} ${args} ${partitions} ${partitions}.elf"
echo "Converting application" 
eval "${objcopy} ${args} ${application} ${application}.elf"
