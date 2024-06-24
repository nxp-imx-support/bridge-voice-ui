#!/bin/bash

git submodule update --init --recursive
cd imx-voiceui
make BUILD_ARCH=CortexA55 -j8
cp -r release ../bridge_release
