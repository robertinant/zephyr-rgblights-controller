#!/bin/bash

/opt/zephyr-sdk-0.16.3/arm-zephyr-eabi/bin/arm-zephyr-eabi-objcopy --input-target=binary --output-target=elf32-little build/zephyr-rgblights-controller/zephyr/zephyr-rgblights-controller.signed.bin build/zephyr-rgblights-controller/zephyr/zephyr-rgblights-controller.signed.bin.elf
