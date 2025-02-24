#!/bin/bash

# Docker

DOCKER_IMAGE_TAG="zephyr"
SCRIPT=$(realpath "$0")
SCRIPTPATH=$(dirname "$SCRIPT")
DOCKER_WORKDIR="${SCRIPTPATH}/../"
ZEPHYR_SDK_VER="0.16.3"
