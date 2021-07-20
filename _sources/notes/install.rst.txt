Installation
============

Install vineyard
----------------

Vineyard is distributed as a `python package <https://pypi.org/project/vineyard/>`_
and can be easily installed with :code:`pip`:

.. code:: shell

    pip3 install vineyard

Install etcd
------------

Start vineyard requires etcd (>= 3.3.0), a latest version of etcd can be downloaded via

for Ubuntu or (Debian):

.. code:: shell

    wget https://github.com/etcd-io/etcd/releases/download/v3.4.13/etcd-v3.4.13-linux-amd64.tar.gz
    tar zxvf etcd-v3.4.13-linux-amd64.tar.gz
    export PATH=etcd-v3.4.13-linux-amd64:$PATH

Or for MacOS:

.. code:: shell

    wget https://github.com/etcd-io/etcd/releases/download/v3.4.13/etcd-v3.4.13-darwin-amd64.zip
    unzip etcd-v3.4.13-darwin-amd64.zip
    export PATH=etcd-v3.4.13-darwin-amd64:$PATH

Prepare dependencies
--------------------

Vineyard can be built and deployed on common Unix-like systems. Vineyard has been
fully tests with C++ compilers that supports C++ 14.

Vineyard requires the following software as dependencies to build and run:

+ apache-arrow >= 0.17.1
+ gflags
+ glog
+ boost
+ mpi, for the graph data structure module
+ gtest, for build test suites

If you want to build the vineyard server, the following additional libraries are needed:

+ protobuf
+ grpc

And the following python packages is required:

+ libclang
+ parsec

and other packages to help us build the documentation, which can be easily installed using ``pip``:

.. code:: shell

    pip3 install libclang parsec yapf==0.30.0 sphinx sphinx_rtd_theme breathe

Ubuntu (or Debian)
~~~~~~~~~~~~~~~~~~

Vineyard has been fully tested on Ubuntu 20.04. The dependencies can be installed by

.. code:: shell

    apt-get install -y ca-certificates \
                       cmake \
                       doxygen \
                       libboost-all-dev \
                       libcurl4-openssl-dev \
                       libgflags-dev \
                       libgoogle-glog-dev \
                       libgrpc-dev \
                       libgrpc++-dev \
                       libmpich-dev \
                       libprotobuf-dev \
                       libssl-dev \
                       libunwind-dev \
                       libz-dev \
                       protobuf-compiler-grpc \
                       python3-pip \
                       wget

Then install the apache-arrow (see also `https://arrow.apache.org/install <https://arrow.apache.org/install/>`_):

.. code:: shell

    wget https://apache.jfrog.io/artifactory/arrow/$(lsb_release --id --short | tr 'A-Z' 'a-z')/apache-arrow-apt-source-latest-$(lsb_release --codename --short).deb \
        -O /tmp/apache-arrow-apt-source-latest-$(lsb_release --codename --short).deb
    apt install -y -V /tmp/apache-arrow-apt-source-latest-$(lsb_release --codename --short).deb
    apt update -y
    apt install -y libarrow-dev

MacOS
~~~~~

Vineyard has been tests on MacOS as well, the dependencies can be installed using :code:`brew`:

.. code:: shell

    brew install apache-arrow boost gflags glog grpc protobuf mpich openssl zlib

Install from source
-------------------

Vineyard is open source on Github: `https://github.com/v6d-io/v6d <https://github.com/v6d-io/v6d>`_.
You can obtain the source code using ``git``:

.. code:: console

    git clone https://github.com/v6d-io/v6d
    cd v6d
    git submodule update --init

Then you do a out-of-source build using CMake:

.. code:: shell

    mkdir build
    cd build
    cmake ..
    make -j$(nproc)
    make install  # optionally

You will see vineyard server binary under the ``bin`` directory, and static or shared linked
libraries will be placed under the ``lib`` folder.

Build python wheels
-------------------

After building the vineyard library successfully, you can package a install wheel distribution by

.. code:: shell

    python3 setup.py bdist_wheel
