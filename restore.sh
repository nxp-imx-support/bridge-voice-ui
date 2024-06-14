#!/bin/bash -x

# Copyright 2023-2024 NXP
# SPDX-License-Identifier: BSD-3-Clause

#Restore original files and remove queues
rm /dev/mqueue/wakeword_queue
rm /dev/mqueue/command_queue

echo "Restoring original files..."
cp /etc/asound.conf.original /etc/asound.conf
cp /unit_tests/nxp-afe/Config.ini.original /unit_tests/nxp-afe/Config.ini

echo "Killing unnecessary processes..."
killall afe
killall voice_ui_app
