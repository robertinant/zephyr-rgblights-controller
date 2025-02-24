#!/bin/bash
#
# This script creates the zephyr-ready docker image.
# The resulting image will have the tag set in env.sh for easy identification of the generated image.
# By default the script will use Dockerfile to build the image if no Dockerfile is given as argument.
#

# Exit immediately if any command or pipeline returns a non-zero exit status
set -e

# Make sure that the script is executed from the right directory
if [ "`basename -- $(pwd)`" != "docker" ]
then
        echo -e -n "\033[3m\n\033[31mThis script should be executed in the docker directory\033[39m\033[23m\n"
        exit
fi

# Source the common variables
. ./env.sh

usage() {
    echo -e -n "\033[3m\n\033[31mUsage: $0 [path_to_Dockerfile]\033[39m\033[23m\n"
}

docker build --tag "${DOCKER_IMAGE_TAG}" \
             --build-arg "DOCKER_WORKDIR=${DOCKER_WORKDIR}" \
             --build-arg "ZEPHYR_SDK_VER=${ZEPHYR_SDK_VER}" \
             -f ${1:-Dockerfile} \
             .
