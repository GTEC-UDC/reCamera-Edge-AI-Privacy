#!/bin/bash

set -e  # Exit on error

# Check if at least one argument is provided
if [ $# -lt 1 ]; then
    echo "Error: Missing arguments"
    echo "Usage: $0 [timeout_seconds] <executable_name> [args...]"
    exit 1
fi

# Check if first argument is a number (timeout)
if [[ $1 =~ ^[0-9]+$ ]]; then
    TIMEOUT="$1"
    shift  # Remove the first argument
    
    # Check if executable name is provided after timeout
    if [ $# -lt 1 ]; then
        echo "Error: Missing executable name after timeout"
        echo "Usage: $0 [timeout_seconds] <executable_name> [args...]"
        exit 1
    fi
else
    TIMEOUT="0"  # Default timeout is 0 (no timeout)
fi

EXECUTABLE="$1"
shift  # Remove the executable name from arguments

# Remaining arguments will be passed to the executable
EXTRA_ARGS="$@"

echo "Building project..."
# Check if we need to enter the build directory
if [ -d "build" ]; then
    cd build
    echo "Entered build directory"
else
    echo "Already in build directory"
fi

#cmake ..
make $EXECUTABLE

REMOTE_IPs=("192.168.1.137" "192.168.42.1")
REMOTE_USER="recamera"
REMOTE_DIR="/home/recamera"

# find the first available remote ip
for REMOTE_IP_TEST in "${REMOTE_IPs[@]}"; do
    # check if the remote ip is reachable
    if ping -w 1 $REMOTE_IP_TEST &> /dev/null; then
        echo "Remote device ($REMOTE_IP_TEST) is reachable"
        REMOTE_IP=$REMOTE_IP_TEST
        break
    fi
done

if [ -z "$REMOTE_IP" ]; then
    echo "Error: No reachable remote device found in ${REMOTE_IPs[@]}"
    exit 1
fi

echo "Copying executable $EXECUTABLE to remote device ($REMOTE_IP)..."
scp $EXECUTABLE $REMOTE_USER@$REMOTE_IP:$REMOTE_DIR || { echo "Error: Failed to copy file"; exit 1; }

# NOTE: To avoid typing sudo password, modify your sudo configuration on the remote device:
#   Change in /etc/sudoers.d/sudo_users:
#   FROM: recamera ALL=(ALL:ALL) ALL
#   TO:   recamera ALL=(ALL:ALL) NOPASSWD: ALL
#
# Or for more security, limit NOPASSWD just to the anonymize_recamera:
#   recamera ALL=(ALL:ALL) ALL
#   recamera ALL=(ALL) NOPASSWD: /home/recamera/anonymize_recamera
#
# Make sure to use 'sudo visudo -f /etc/sudoers.d/sudo_users' to edit the file safely

if [ "$TIMEOUT" -gt 0 ]; then
    echo "Running executable on remote device with $TIMEOUT second timeout..."
    # Run sudo with NOPASSWD option configured on remote system
    ssh -t $REMOTE_USER@$REMOTE_IP "bash -l -c 'cd $REMOTE_DIR && sudo timeout --kill-after=$TIMEOUT ${TIMEOUT}s ./$EXECUTABLE $EXTRA_ARGS || echo \"Program terminated after timeout or with error\"'"
else
    echo "Running executable on remote device without timeout..."
    # Run sudo with NOPASSWD option configured on remote system
    ssh -t $REMOTE_USER@$REMOTE_IP "bash -l -c 'cd $REMOTE_DIR && sudo ./$EXECUTABLE $EXTRA_ARGS || echo \"Program terminated with error\"'"
fi

echo "Test completed."
