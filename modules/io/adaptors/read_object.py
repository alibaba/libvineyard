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

import base64
import json
import sys
import traceback

import os
import pyarrow as pa
import vineyard


def report_status(status, content):
    ret = {"type": status, "content": content}
    print(json.dumps(ret), flush=True)

def is_object(value):
    return isinstance(value, str) and len(value) == 17 and value[0] == 'o'

def read_directory(client, path):
    dirs = os.listdir(path)
    meta_dict = {}
    with open(f'{path}/.meta', 'rb') as meta_file:
        content = meta_file.read()
        meta_dict = json.loads(content)
    if '.data' in dirs:
        with open(f'{path}/.data', 'rb') as blob_file:
            content = blob_file.read()
            return client.put(content)

    meta = vineyard.ObjectMeta()
    for k, v in meta_dict.items():
        if k != 'id':
            if is_object(v):
                sub_id = read_directory(client, f'{path}/{v}')
                meta.add_member(k, client.get_meta(sub_id))
            else:
                meta[k] = v
    return client.create_metadata(meta).id

def read_object(vineyard_socket, path):
    """Read a directory in external storage as an object.       

    Args:
        vineyard_socket (str): Ipc socket
        path (str): External storage path to read from

    Raises:
        ValueError: If the object is invalid.
    """
    client = vineyard.connect(vineyard_socket)
    new_id = read_directory(client, path)
    ret = {"type": "return", "content": repr(new_id)}
    print(json.dumps(ret), flush=True)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(
            "usage: ./read_object <ipc_socket> <path> "
        )
        exit(1)
    ipc_socket = sys.argv[1]
    path = sys.argv[2]
    read_object(ipc_socket, path)
