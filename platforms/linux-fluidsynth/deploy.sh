#!/bin/bash
# Deploy patched FluidSynth library and BankFix to Raspberry Pi
#
# Prerequisites:
#   1. Apply patch:  cd ../../libs/fluidsynth && git apply ../../patches/fluidsynth-mmap.patch
#   2. Build:        See ../../patches/README.md for Docker cross-compile command
#   3. Build BankFix: See below
#
# Usage: ./deploy.sh [PI_IP]

set -e
PI_IP="${1:-192.168.0.94}"
PI_USER="root"
PI_PASS="rompler"
SCP="sshpass -p '$PI_PASS' scp -o PubkeyAuthentication=no -o PreferredAuthentications=password"
SSH="sshpass -p '$PI_PASS' ssh -o PubkeyAuthentication=no -o PreferredAuthentications=password"

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FS_LIB="$REPO_ROOT/libs/fluidsynth/build/src/libfluidsynth.so.3.3.3"
BANKFIX="$REPO_ROOT/tools/midi_bankfix"
STARTUP="$(dirname "$0")/fluidsynth-start.sh"

# Deploy FluidSynth library
if [ -f "$FS_LIB" ]; then
    echo "Deploying libfluidsynth.so.3.3.3..."
    eval $SCP "$FS_LIB" "$PI_USER@$PI_IP:/usr/lib/libfluidsynth.so.3.3.3"
else
    echo "WARNING: $FS_LIB not found (build FluidSynth first)"
fi

# Deploy BankFix binary
if [ -f "$BANKFIX" ]; then
    echo "Deploying midi_bankfix..."
    eval $SCP "$BANKFIX" "$PI_USER@$PI_IP:/usr/bin/midi_bankfix"
else
    echo "WARNING: $BANKFIX not found (cross-compile BankFix first)"
fi

# Deploy startup script
echo "Deploying fluidsynth-start.sh..."
eval $SCP "$STARTUP" "$PI_USER@$PI_IP:/root/fluidsynth-start.sh"

# Restart services
echo "Restarting FluidSynth + BankFix..."
eval $SSH "$PI_USER@$PI_IP" "killall fluidsynth midi_bankfix 2>/dev/null; sleep 1; /root/fluidsynth-start.sh &"

echo "Done."
