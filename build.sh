#!/bin/bash

git submodule update --init --recursive
cd imx-voiceui
make -j8
cp -r release ../bridge_release
