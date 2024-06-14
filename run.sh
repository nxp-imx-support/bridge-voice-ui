#!/bin/bash -x

# Copyright 2023-2024 NXP
# SPDX-License-Identifier: BSD-3-Clause

mypath=$(dirname $0)
evk=$(uname -n)

# Remove old queue files
rm /dev/mqueue/wakeword_queue
rm /dev/mqueue/command_queue

# Backup original sound configuration files
mv /etc/asound.conf /etc/asound.conf.original
mv /unit_tests/nxp-afe/Config.ini /unit_tests/nxp-afe/Config.ini.original

# Set machine dependant configuration
if [[ $evk == "imx8mp-lpddr4-evk" || $evk == "imx8mpevk" ]]; then
	audiocard="micfilaudio"
	cp -v $mypath/conf/asound.conf_imx8mp /etc/asound.conf
elif [[ $evk == "imx93-11x11-lpddr4x-evk" || $evk == "imx93evk" ]]; then
	audiocard="micfilaudio"
	cp -v $mypath/conf/asound.conf_imx93 /etc/asound.conf
elif [[ $evk == "imx8mm-lpddr4-evk" || $evk == "imx8mmevk" ]]; then
	audiocard="imxaudiomicfil"
	cp -v $mypath/conf/asound.conf_imx8mm /etc/asound.conf
else
	echo "Smart Kitchen demo is not supported in this board!!!"
	exit
fi

cp $mypath/vit-notify/WakeWordNotify /usr/bin/
cp $mypath/vit-notify/WWCommandNotify /usr/bin/
cp $mypath/conf/Config.ini /unit_tests/nxp-afe/
cp /unit_tests/nxp-afe/afe $mypath/

killall voice_ui_app
killall smart-kitchen-gui
killall afe

sleep 0.1s

amixer -c $audiocard sset 'MICFIL Quality Select' 'High'

for i in {1..8}; do
	amixer -c $audiocard cset numid=$i 85%
done

modprobe snd-aloop
sleep 0.1s

$mypath/smart-kitchen-gui &
sleep 0.1s
$mypath/voice_ui_app -notify &
sleep 0.1s
$mypath/afe libvoiceseekerlight >/dev/null 2>&1
