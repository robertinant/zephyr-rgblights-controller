# Use Ubuntu as the base.
FROM ubuntu:22.04

# Colors please
ENV TERM="xterm-color"
ARG PATH=$PATH:/opt/zephyr-sdk-0.16.3/arm-zephyr-eabi/bin
# Pass docker workdir argument into the container. Set to none if not specified.
ARG DOCKER_WORKDIR="none"

# Suppresses prompt for timezone
ENV DEBIAN_FRONTEND=noninteractive

# The entrypoint script used when the container starts
COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

# Cmake Ubuntu package is outdated. Install cmake from kitware apt repo to get the most recent.
COPY kitware-archive.sh /tmp
RUN /bin/sh -c "/tmp/kitware-archive.sh"

# Install package dependencies, and clean up. Reason this is all done in one go is that each RUN creates
# a layer and increases the size of the resulting image. The less RUN commands the fewer layers the smaller the image.
RUN apt-get update && apt-get -y upgrade && apt-get -y install --no-install-recommends git cmake ninja-build \
        ccache device-tree-compiler wget gcc python3-dev python3-pip python3-setuptools python3-wheel \
        python3 xz-utils file libpython3-dev make bash openssh-server \
        sudo vim curl ca-certificates gpg gosu less gcc-multilib g++-multilib python3.10-venv libusb-1.0-0 gdb clang-format clang-tidy && \
        apt-get clean -y && \
        apt-get autoremove --purge -y && \
        rm -rf /var/lib/apt/lists/*

# Fetch the zephyr sdk (compiler and cmake files). The call to setup fetches compiler of interest. The same setup script
# will be called again when the container is started to make zephyr aware of where to find the compiler and cmake files.
ARG ZEPHYR_SDK_VER
RUN wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VER}/zephyr-sdk-${ZEPHYR_SDK_VER}_linux-x86_64_minimal.tar.xz -P /tmp \
        && tar -xf /tmp/zephyr-sdk-${ZEPHYR_SDK_VER}_linux-x86_64_minimal.tar.xz -C /opt \
        && rm -f /tmp/zephyr-sdk-${ZEPHYR_SDK_VER}_linux-x86_64_minimal.tar.xz \
        && /opt/zephyr-sdk-${ZEPHYR_SDK_VER}/setup.sh -t arm-zephyr-eabi -t riscv64-zephyr-elf -c

# On the Fluke network we get SSL issues if we don't include Zscaler certificates.
COPY ZscalerRootCertificate-2048-SHA256.crt /usr/local/share/ca-certificates/
RUN update-ca-certificates

# Install Zephyr python dependencies as specified in the requirements files.
# Eventually mcuboot needs to be fixed to a version as well rathern than fetching main
RUN pip install west &&  pip install --no-cache-dir \
        -r https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/v3.4-branch/scripts/requirements-base.txt \
        -r https://raw.githubusercontent.com/zephyrproject-rtos/mcuboot/main/scripts/requirements.txt

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
