#! /usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2020 Alibaba Group Holding Limited.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import json
import subprocess
import numpy as np

import vineyard


def test_checkpoint_round_trip(vineyard_ipc_socket, test_dataset_tmp):
    client = vineyard.connect(vineyard_ipc_socket)
    arr = np.ones([2, 3])
    oid = client.put(arr)

    subprocess.check_call(['vineyard_write_object', vineyard_ipc_socket, test_dataset_tmp, repr(oid)])

    p = subprocess.Popen(['vineyard_read_object', vineyard_ipc_socket,
                          '%s/%r' % (test_dataset_tmp, oid)],
                         stdout=subprocess.PIPE)
    rt = p.poll()
    while rt is None:
        line = p.stdout.readline()
        if line:
            x = json.loads(line)
            if x['type'] == 'return':
                nid = x['content']
                narr = client.get(nid)
                np.testing.assert_array_equal(arr, narr)
                break
