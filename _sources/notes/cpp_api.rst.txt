C++ API Reference
=================

.. _python_cpp:

.. default-domain:: cpp

.. contents::
    :local:

ObjectID and Object
-------------------

.. doxygentypedef:: vineyard::ObjectID

.. doxygenclass:: vineyard::Object
    :members:
    :protected-members:
    :undoc-members:

.. doxygenclass:: vineyard::ObjectBuilder
    :members:
    :protected-members:
    :undoc-members:

.. doxygenclass:: vineyard::ObjectBase
    :members:
    :undoc-members:

Metadata of objects
-------------------

.. doxygenclass:: vineyard::ObjectMeta
    :members:
    :protected-members:
    :undoc-members:

Connect to vineyard
-------------------

.. doxygenclass:: vineyard::ClientBase
    :members:
    :protected-members:
    :undoc-members:

.. doxygenclass:: vineyard::Client
    :members:
    :protected-members:
    :undoc-members:

.. doxygenclass:: vineyard::RPCClient
    :members:
    :protected-members:
    :undoc-members:

State of server
---------------

.. doxygenstruct:: vineyard::InstanceStatus
    :members:
    :undoc-members:

Primitives
----------

.. doxygenclass:: vineyard::Blob
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::BlobWriter
    :members:
    :undoc-members:

Stream
------

.. doxygenclass:: vineyard::ByteStream
    :members:
    :undoc-members:

Data types
----------

.. doxygenclass:: vineyard::Array
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::ArrayBuilder
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::Hashmap
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::HashmapBuilder
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::Tensor
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::TensorBuilder
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::DataFrame
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::DataFrameBuilder
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::Pair
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::PairBuilder
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::Tuple
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::TupleBuilder
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::Scalar
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::ScalarBuilder
    :members:
    :undoc-members:

Distributed data types
----------------------

.. doxygenclass:: vineyard::GlobalTensor
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::GlobalTensorBuilder
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::GlobalDataFrame
    :members:
    :undoc-members:

.. doxygenclass:: vineyard::GlobalDataFrameBuilder
    :members:
    :undoc-members:
