#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Copyright 2024 NXP
# SPDX-License-Identifier: BSD-3-Clause

import threading
import json
import posix_ipc
import time
import sys
import os
from subprocess import Popen, PIPE, STDOUT

os.environ['TMPDIR'] = '/etc'
os.system('export TMPDIR')

if __name__ == "__main__":
    mq_w = posix_ipc.MessageQueue("/wakeword_queue", flags=posix_ipc.O_CREAT)
    mq_c = posix_ipc.MessageQueue("/command_queue",  flags=posix_ipc.O_CREAT)

    cmd = ["chip-tool", "interactive", "start"]
    chiptool = Popen(cmd, stdin=PIPE, stderr=STDOUT)

    while True:
        c,_ = mq_c.receive()
        num = int.from_bytes(c) - 48
        print(num)

        if num == 7:
            chiptool.stdin.write('onoff off 8888 3\n'.encode('utf-8'))
            chiptool.stdin.flush()
        elif num == 8:
            chiptool.stdin.write('onoff on 8888 3\n'.encode('utf-8'))
            chiptool.stdin.flush()