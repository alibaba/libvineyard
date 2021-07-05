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


def write_directory(client, path, meta):
    path = '%s/%r' % (path, meta.id)
    os.mkdir(path)
    pure_meta = {}
    for k, v in meta.items():
        if isinstance(v, vineyard.ObjectMeta):
            write_directory(client, path, v)
            pure_meta[k] = repr(v.id)
        else:
            pure_meta[k] = v

    meta_file = open(f"{path}/.meta", "wb")
    try:
        with meta_file as f:
            f.write(json.dumps(pure_meta).encode('utf-8'))
    except Exception as e:
        err = traceback.format_exc()
        report_status("error", err)
        raise

    if pure_meta['typename'] == 'vineyard::Blob':
        blob_file = open(f"{path}/.data", "wb")
        try:
            with blob_file as f:
                f.write(bytes(client.get(pure_meta['id'])))
        except Exception as e:
            err = traceback.format_exc()
            report_status("error", err)
            raise

def write_object(vineyard_socket, path, object_id):
    """Write an object to external storage as a directory.
       The name of the (sub)directory is the repr(ObjectID) of the (sub)object.
       Within the directory, a .meta file stores the metadata and blobs are stored
       as files named with their blob IDs.
       

    Args:
        vineyard_socket (str): Ipc socket
        path (str): External storage path to write to
        object_id (str): ObjectID of the object to write

    Raises:
        ValueError: If the object is invalid.
    """
    client = vineyard.connect(vineyard_socket)
    meta = client.get_meta(vineyard.ObjectID(object_id))
    write_directory(client, path, meta)



if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(
            "usage: ./write_object <ipc_socket> <path> <object_id> "
        )
        exit(1)
    ipc_socket = sys.argv[1]
    path = sys.argv[2]
    object_id = sys.argv[3]
    write_object(ipc_socket, path, object_id)
