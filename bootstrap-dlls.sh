#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# webplayer dlls
wget -r -l 7 -np -R "index.html*" -nH --cut-dirs=2 https://cdn.dexlabs.systems/webplayer/patched-latest/ -P $SCRIPT_DIR

# dxvk dll
wget https://github.com/doitsujin/dxvk/releases/download/v1.10.3/dxvk-1.10.3.tar.gz
tar -xvf dxvk-1.10.3.tar.gz
mv dxvk-1.10.3/x32/d3d9.dll $SCRIPT_DIR/d3d9_vulkan.dll
rm dxvk-1.10.3.tar.gz
rm -r dxvk-1.10.3/
