# Zephyr Docker

This directory contains all the docker related files for creating a docker image and running it as a container. The primary goal of using docker is to eliminate dependencies on the host and resulting in a portable en highly maintainable environment for development and maintenance. Eventually, a version of this docker image will be placed in the Fluke docker registry eliminating the need to build the image on the host. This document will be updated with the information needed to use the image in the Fluke registry.

## Organization

The idea behind this docker image is to enable building a zephyr project without having to depend on tools installed on the host. Instead, the docker image will contain all tools needed to build a zephyr project. The project directory will however remain on the host and all build artifacts will be accessible on the host even after the container is stopped. This is accomplished by creating a shared volume when the container starts. The host UID / GID for the active user will be used to create a docker user inside the container to ensure that all files created on the shared volume are created with the same UID / GID as that of the active user on the host. This will prevent permission issues on the host when accessing artifacts created during the build inside the docker container.

## Creating a docker image

The script docker-build.sh can be used to build a zephyr ready docker image. Without arguments it will use the default Dockerfile.

i.e. `./docker-build.sh`

During the build of the docker container, the zephyr dependencies such as python, west, and compiler are downloaded and embedded in the image.

## Running the container

Once the image has been build, a container can be started by running the script docker-run.sh

i.e. `./docker-run.sh`

During the initial run, all project code dependencies will be downloaded and stored on the shared volume. Any successive run will detect if these dependencies are already present and skip this phase speeding up starting the container significantly (seconds vs minutes).

If no arguments are supplied to the script, the container enters interactive mode and presents the user with a bash shell. At the bash shell, the project can now be build using west as well as executing other tasks. To build the project, run:

`west build -b fluke289_eval`

The docker-run.sh can also be supplied with arguments to execute a command inside the container and then exit. For example:

```
./docker-run.sh "west boards"
./docker-run.sh "west build -b fluke289_eval"
```

## Future

Future versions of this docker container will enable ssh and the ability setup an ssh tunnel for programming and debugging a board that is connected to the host or remote host.