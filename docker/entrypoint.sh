#!/bin/bash
set -e
 
# If "-e uid={custom/local user id}" flag is not set for "docker run" command, use 9999 as default
CURRENT_UID=${uid:-9999}
CURRENT_GID=${gid:-9999}
CURRENT_GROUP=${group:zephyr}
CURRENT_USER=${user:zephyr}

# Notify user about the UID selected
echo "Current container UID is $CURRENT_UID and should match your host UID" 

# Add a group with the same group name and group ID as on the host.
groupadd -f -g $CURRENT_GID $CURRENT_GROUP
# Create user with the same username as on the host and add to the host group id.
useradd --shell /bin/bash -u $CURRENT_UID -o -c "" -g $CURRENT_GID $CURRENT_USER
# Set the password for the newly created user.
echo "$CURRENT_USER:zephyr" | chpasswd

# ~/.ssh is mounted as a volume in new users home directory.
# This mount will create the path /home/<new user>/.ssh and by default
# is owned by root. The next line changes the ownership of that directory
# to be of the new user.
chown $CURRENT_USER:$CURRENT_GROUP /home/$CURRENT_USER

# Enable user to execute as root using sudo
usermod -aG sudo ${CURRENT_USER}
# Start ssh server
gosu root service ssh start &

# git complains about "dubious ownership in repository" for isome zephyr repos
# while on the Fluke network?! The root cause has not been determined and it only seems to happen
# for docker running under macOS. To make sure everything works on all platforms
# the following git configuration works around this issue.
gosu $CURRENT_USER git config --global --add safe.directory "*"

# This will execute any command argument that has been given to docker-run.sh.
# By default it will execute bash (see docker-run.sh). To, for example, build
# the application for the fluke289_eval board, run the following:
# ./docker-run.sh "west build -b fluke289_eval".
# This is however not limited to west. Any command can be executed inside the docker container.
# e.g. ./docker-run.sh "ps aux" also works. Or even ./docker-run.sh "top"
exec gosu $CURRENT_USER "$@"
