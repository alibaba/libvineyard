Troubleshooting
===============

You may have problems when operates vineyard, and content in this page
could helps you when error occurs.

1. *Etcd hasn't been configured properly*

  You may see the following error when sending requests to vineyard:

  .. code::

      Etcd error: etcdserver: too many operations in txn request, error code: 3

  That means your etcd hasn't been configured properly and doesn't support
  more than 128 operations inside a transaction. You could check your etcd
  startup parameters and set :code:`--max-txn-ops` to a larger value, for
  example :code:`102400`.

2. *Encounter strange behaviors when working with pyarrow*

  The python SDK of vineyard depends on libarrow-dev. When the module is imported
  along with pyarrow there will be a tons of DLL conflict issues that need to be
  resolved, see https://issues.apache.org/jira/browse/ARROW-10599 for more details.

  That means, we need to keep the consistence of apache-arrow's installation and
  vineyard's installation. More specifically,

  + For users, just install pyarrow and vineyard both using the :code:`pip` package
    manager should be enough. You could install them by

      .. code::

          pip3 install pyarrow vineyard

  + For developers of vineyard, the localized installed pyarrow should be built
    from scratch, using the system-wide libarrow-dev, that can be achieved by

      .. code::

          pip3 install --no-binary pyarrow pyarrow

  Besides, you may also meet strange crashes or runtime exceptions if you have
  :code:`import` pyarrow before vineyard, you could try to adjust the import order
  by :code:`import vineyard` before :code:`import pyarrow`. And please export environment
  variable :code:`VINEYARD_DEV=TRUE` if you have encountered any shared library related
  issue.

3. *Resource of etcd pod when deploying on Kubernetes*

  We have noticed that etcd performs pretty poor when vineyard client persist a large
  object, especially in Kubernetes deployment the CPU cores of etcd pod is limited by
  cgroup. For such cases, users need to increase the CPU resources of etcd pod. For
  more details about etcd tuning, please refer to the section `Hardware recommendations
  <https://etcd.io/docs/v3.4.0/op-guide/hardware/>`_
  in etcd docs.

4. *Installation Error: Could not find package configuration file provided by: libgrapelite*

  While attempting to install vineyard from source, user may face this error. A module 
  named "Graph" in vineyard which is used for distributed data structure 
  requires `libgrape-lite library <https://github.com/alibaba/libgrape-lite>`_ .
  This issue can be resolved by:

  + By installing the libgrape-lite library.
  + Disabling the part by the following code:

  .. code:: console

      cmake .. -DBUILD_VINEYARD_GRAPH=OFF
