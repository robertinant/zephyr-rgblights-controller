#!/bin/bash
#
# This script runs the image generated by the docker build script
# using docker run command, where:
#   --rm  automatically removes the container and on exit
#   -i  keeps the standard input open
#   -t  provides a terminal as a interactive shell within container
#   --volumes  are file systems mounted on docker container to preserve
# data generated during the build or interactive session and these are stored on the host.
# Left side being an absolute path on the host machine, right side being
# an absolute path inside the container.
#
# The script can be run with or without parameter:
#
#   $ ./docker-run.sh
#
# to go into docker container prompt or:
#
#   $ ./docker-run.sh "<command>"
#
# to run a command inside container and exit.
# e.g. ./docker-run.sh "docker build -b fluke289_eval -p=auto"
#      ./docker-run.sh "docker boards"

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

# run the docker image and the zephyr setup script. This will take a while during the initial run while
# the zephyr repos are cloned. Things will speed up during any successive run.
docker run -it --rm \
    -p 0.0.0.0:2223:22 \
    --volume ${HOME}/.ssh:/home/$USER/.ssh \
    --volume ${DOCKER_WORKDIR}:${DOCKER_WORKDIR} \
    --workdir ${DOCKER_WORKDIR} \
    -e uid=$(id -u) \
    -e gid=$(id -g $(id -u)) \
    -e user=$USER \
    -e group=$(id -gn $(id -u)) \
    --name="zephyr_rgblights_controller" \
    "${DOCKER_IMAGE_TAG}" \
    /bin/bash -c "./docker/setupzephyr.sh && ${1:-bash}"

