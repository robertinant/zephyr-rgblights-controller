#!/bin/bash

RED='\033[0;31m'
NC='\033[0m'

# source the common variables
. ./docker/env.sh

/opt/zephyr-sdk-${ZEPHYR_SDK_VER}/setup.sh -t arm-zephyr-eabi -c

# Run west init and update if this is the first time
if [ ! -f ./docker/.WEST_INITIALIZED ]; then
        echo -e "${RED}\n\r\tThis is the first run of the zephyr setup script and is going to take a while. Any next run will be significanttly faster.\n\r"
        if [ ! -f ./.west/config ]; then
                west init -l ./manifest
                if [ $? -ne 0 ]; then
                        echo -e "${RED}west init failed, either exit docker and try again or\n\rmanually execute 'west init -l ./manifest' inside the docker container.\n\r${NC}"
                        exit -1
                fi
        else
                echo -ne "West already intialized, moving on to west update.\n\r"
        fi

        west update
        if [ $? -ne 0 ]; then
                echo -ne "${RED}west update failed, either exit docker and try again using the docker-run.sh script or\n\rmanually execute 'west update' inside the docker container.\n\r${NC}"
                exit -1
        fi

        touch ./docker/.WEST_INITIALIZED
fi
