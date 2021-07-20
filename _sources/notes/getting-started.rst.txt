Getting Started
===============

Starting vineyard server
------------------------

.. code:: console
     
     $ vineyardd

A vineyard daemon server will be launched on the underlying machine with default
settings. The default ``socket`` is ``/var/run/vineyard.sock``, and it is
listened by the server for ipc connections. 

Note that a vineyard daemon server is a vineyard instance in a vineyard cluster.
Thus, to start a vineyard cluster, we can simply start ``vineyardd`` over all the 
machines in the cluster, and make sure these vineyard instances can register to 
the same ``etcd_endpoint``. The default value of ``etcd_endpoint`` is 
``http://127.0.0.1:2379``, and ``vineyard`` will launch the ``etcd_endpoint`` 
in case the etcd servers are not started on the cluster.

Use ``vineyardd --help`` for other parameter settings.

Connecting to vineyard
----------------------

Vineyard deamon serves clients via UNIX domain socket:

.. code:: python

     >>> import vineyard
     >>> client = vineyard.connect('/var/run/vineyard.sock')

Here we established a vineyard client connected to the vineyardd instance 
via the IPC socket ``/var/run/vineyard.sock``.

Getting and putting Python object
---------------------------------

.. code:: python

     >>> import numpy as np
     >>> import vineyard.data.tensor
     >>> arr = np.arange(8)
     >>> arr_id = client.put(arr)
     >>> arr_id
     00002ec13bc81226
     >>> shared_arr = client.get(arr_id)
     >>> shared_arr
     array([0, 1, 2, 3, 4, 5, 6, 7])

We first use :code:`client.put()` to build the vineyard object from the local variable ``arr``,
which returns the ``object_id`` that is the unique id in vineyard to represent the object.

Then given the ``object_id``, we can obtain a shared-memory object from vineyard 
with :code:`client.get()`. Note that :code:`shared_arr` doesn't allocate memory in the
client process; instead, it shares the memory from the vineyard server.

Creating a dataframe
--------------------

.. code:: python

     >>> import numpy as np
     >>> import vineyard.data.dataframe
     >>> df = pd.DataFrame({'u': [0, 0, 1, 2, 2, 3],
     >>>                    'v': [1, 2, 3, 3, 4, 4],
     >>>                    'weight': [1.5, 3.2, 4.7, 0.3, 0.8, 2.5]})
     >>> df_id = client.put(df)

.. code:: python

     >>> shared_object = client.get_object(df_id)
     >>> shared_object.typename
     vineyard::DataFrame

.. code:: python

     >>> shared_df = client.get(df_id)
     >>> shared_df
     u  v  weight
     0  0  1     1.5
     1  0  2     3.2
     2  1  3     4.7
     3  2  3     0.3
     4  2  4     0.8
     5  3  4     2.5

We first build the vineyard dataframe object from pandas dataframe variable ``df``,
then to further understand the ``client.get()`` method, we use ``client.get_object()``
to get the vineyard object, and check its ``typename``. 

Actually, ``client.get()`` works in two steps, it first gets the vineyard object
from vineyardd via ``client.get_object()``, and then resolves the vineyard object
based on the registered resolver. 

In this case, when we ``import vineyard.dataframe``,
a resolver that can resolve a vineyard dataframe object to a pandas dataframe is
registered to the resolver factory under the vineyard type ``vineyard::DataFrame``,
so that the client can automatically resolve the vineyard dataframe object.
To further understand the registration design
in vineyard, see :ref:`divein-driver-label`.

Shared Memory
-------------

Vineyard supports shared memory interface of :class:`SharedMemory` and :class:`ShareableList`
like things in `multiprocessing.shared_memory <https://docs.python.org/3/library/multiprocessing.shared_memory.html>`_.

The shared memory interface can be used in the following way:

.. code:: python

     >>> from vineyard import shared_memory
     >>> value = shared_memory.ShareableList(client, [b"a", "bb", 1234, 56.78, True])
     >>> value
     ShareableList([b'a', 'bb', 1234, 56.78, True], name='o8000000119aa10c0')
     >>> value[4] = False
     >>> value
     ShareableList([b'a', 'bb', 1234, 56.78, False], name='o8000000119aa10c0')

Note that the semantic of the vineyard's :code:`shared_memory` is slightly different
with the :code:`shared_memory` in python's multiprocessing module. Shared memory in
vineyard cannot be mutable after been visible to other clients.

We have added a :code:`freeze` method to make such transformation happen:

.. code:: python

     >>> value.freeze()

After being freezed, the shared memory (aka. the :code:`ShareableList` in this case)
is available for other clients:

.. code:: python

     >>> value1 = shared_memory.ShareableList(client, name=value.shm.name)
     >>> value1
     ShareableList([b'a', 'bb', 1234, 56.78, False], name='o8000000119aa10c0')

For more details, see :ref:`shared-memory`.

Using streams
-------------

Vineyard supports streaming to facilitate big data pipelining.

Open a local file as a dataframe stream
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code:: python

     >>> from vineyard.io.stream import open
     >>> stream = open('file://twitter.e')
     >>> stream.typename
     vineyard::DataFrameStream

In practice, the file may be stored in an NFS, and we want to read the file in
parallel to further speed up the IO process.

Open a file in NFS parallelized as a parallel stream
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code:: python

     >>> stream = open('file://twitter.e', num_workers=16)
     >>> stream.typename
     vineyard::ParallelStream
     >>> stream.get_stream_num()
     16

To further understand the implementation of the driver ``open``, and the underlying
registration mechanism for drivers in vineyard, see :ref:`divein-driver-label`.
