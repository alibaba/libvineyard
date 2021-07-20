Frequently Asked Questions
==========================

We organize questions which are frequently asked by our end users as a *FAQ* page here.
If the following sections still doesn't answer your question, feel free to `open an issue`_
or `post it to discussions`_.

1. *What are the objects in vineyard?*

  A global object consists of a set of local objects distributed across the cluster,
  where each local object is stored in a single vineyard daemon (a local object can
  always fit into the memory of a single machine).

  The local objects form a partition of the global object (e.g., partitioned dataframes
  of a big dataframe, graph fragments of a big graph). In general, a global object
  is an abstraction of the input or output of a parallel-processing workload, whereas
  a local object is the input or output of a single worker of workload.

2. *Does vineyard support multiple readers on the same piece of data at the same time?*

  Yes. The objects stored in vineyard are **immutable** and they are shared to the
  readers' processes via memory mapping. Thus it is safe to consume the objects by
  multiple readers at the same time.

3. *Is Kubernetes a necessity for vineyard?*

  No. But when deploying vineyard on Kubernetes, users can enjoy the flexibility of
  resource management provided by cloud-native deployments for the workloads of their
  applications. Meanwhile, the scheduler plugin will help co-locate the worker pods
  to the data for better data-work alignment.

4. *How vineyard achieves IPC and memory sharing (i.e., zero-copy sharing) on Kubernetes?*

  Inter-process memory sharing is tricky in Kubernetes, but it is doable. When
  deploying on Kubernetes, vineyard exposes its UNIX-domain socket as a :code:`PersistVolume`,
  then the volume could be mounted into the job's pod and the socket could be used
  for IPC connections to the vineyard daemon. Indeed, the memory sharing is achieved
  by mounting a volume of medium :code:`Memory` into both vineyard daemon's pod and
  the job's pod.

5. *How stream in vineyard differs from similar systems, e.g., Kafka?*

  The stream in vineyard is the abstraction of a sequence of objects, where each object
  is generally a small part of the entire object (e.g., a mini-batch of a tensor).
  Such an abstraction is designed to support cross-engine pipelining between consecutive
  workers in a data analytic pipeline (e.g., a dataframe engine is generating training
  data while the next machine learning engine can consume the data and train the model
  simultaneously).

  The biggest difference to traditional stream frameworks like Kafka is that the data
  are still abstracted as (high-level) objects in Vineyard stream and can be consumed
  in a zero-copy fashion just like normal objects in Vineyard. Whereas Kafka is designed
  for stream processing applications and abstracts data as (low-level) messages. Using
  Kafka in the above scenario still incurs (de)serialization and memory copy costs.

6. *Does vineyard support accessing remote objects?*

  Yes. The RPC client can access the metadata of an object no matter the object is local
  or remote. This allows users as well as internal operators examine the information (e.g.,
  chunk axis, size) of an object to help make decisions on the management (e.g., do we
  need to repartition the object, how to launch the next workload) of the object.

7. *How migration works in vineyard? Is it automatically triggered?*

  Support workload *A* produces a global object *O* and the next workload *B* will consume
  *O* as input. On a Kubernetes cluster with many hosts, namely *h1*, *h2*, *h3*, *h4*,
  etc. *A* has two worker pods on *h1 and *h2*, then the local objects (i.e., *O1* and
  *O2*) of *O* are stored on *h1* and *h2* as well.

  If those two worker pods of *B* (i.e., *B1* and *B2*) are placed at *h1* and *h3*,
  then *B1* can access *O1* locally via memory mapping, but *B2* (on *h3*) cannot
  access *O2* since *O2* lies on *h2*. In this case, an utility program distributed
  along with vineyard in the :code:`initContainer` of *B2* will trigger the migration
  of *O2* from *h2* to *h3* so that pod *B2* can access *O2* locally.

  Of course the data migration has a cost, so we developed the scheduler plugin which will
  prioritize h2 when launching B2 to avoid migration when possible.


.. _open an issue: https://github.com/v6d-io/v6d/issues/new
.. _post it to discussions: https://github.com/v6d-io/v6d/discussions/new
