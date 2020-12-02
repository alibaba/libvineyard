#! /usr/bin/env python3
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
import sys
import oss2
from urllib.parse import urlparse

import vineyard
from vineyard.io.byte import ByteStreamBuilder


def write_oss_bytes(vineyard_socket, stream_id, path, proc_num, proc_index):
    client = vineyard.connect(vineyard_socket)
    streams = client.get(stream_id)
    if len(streams) != proc_num or streams[proc_index] is None:
        raise ValueError(f'Fetch stream error with proc_num={proc_num},proc_index={proc_index}')
    instream = streams[proc_index]
    reader = instream.open_reader(client)

    parsed = urlparse(path)
    auth = oss2.Auth(parsed.username, parsed.password)
    _, bucket_name, object_name = parsed.path.split('/', 2)
    bucket = oss2.Bucket(auth, parsed.hostname, bucket_name)

    result = None
    object_name += f'_{proc_index}'

    while True:
        try:
            buf = reader.next()
        except vineyard.StreamDrainedException:
            break
        if result is None:
            offset = 0
        else:
            offset = result.next_position
        result = bucket.append_object(object_name, offset, bytes(memoryview(buf)))


if __name__ == '__main__':
    if len(sys.argv) < 6:
        print('usage: ./write_oss_bytes <ipc_socket> <stream_id> <file_path> <proc_num> <proc_index>')
        exit(1)
    ipc_socket = sys.argv[1]
    stream_id = sys.argv[2]
    file_path = sys.argv[3]
    proc_num = int(sys.argv[4])
    proc_index = int(sys.argv[5])
    write_oss_bytes(ipc_socket, stream_id, file_path, proc_num, proc_index)
