#!/usr/bin/env sh
# Convenience wrapper around the Wii CMake build. See README.md.
set -e

# devkitPro is usually installed system-wide at /opt/devkitpro, but it can also
# be installed without root, in which case it lives in $HOME/devkitpro.
if [ -z "$DEVKITPRO" ]; then
    if [ -d /opt/devkitpro ]; then
        DEVKITPRO=/opt/devkitpro
    elif [ -d "$HOME/devkitpro" ]; then
        DEVKITPRO=$HOME/devkitpro
    else
        echo "Cannot find devkitPro. Set DEVKITPRO or run scripts/install-devkitpro-wii.py." >&2
        exit 1
    fi
fi
export DEVKITPRO
export DEVKITPPC=$DEVKITPRO/devkitPPC
export DEVKITARM=$DEVKITPRO/devkitARM
PATH=$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$DEVKITPRO/portlibs/wii/bin:$PATH
export PATH

cmake -S . -B build-wii -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Wii.cmake"
cmake --build build-wii --parallel

echo
echo "Built build-wii/superbrummiekart.dol - see README.md for how to run it in Dolphin."
