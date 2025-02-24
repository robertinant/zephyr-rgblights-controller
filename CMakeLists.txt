# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr-rgblights-controller)

target_sources(app PRIVATE src/main.c src/ble.c src/usb_uart.c src/button.c)
target_sources(app PRIVATE ${app_sources})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -E")

