Roadmap
=======

Vineyard aims to serve as an open-source in-memory immutable data manager. We
cut a major release once a year, a minor release for about every two months,
and a patch release every one or two weeks.

The roadmap for major vineyard releases are listed as follows:

v0.5.0
------

We plan to release the first preliminary version for the Rust SDK and Go SDK
in vineyard *v0.5.0*, that is expected to be delivered in later Oct, 2021.

In vineyard *v0.5.0*, we will investigate the opportunity about code generation
based on the metadata of vineyard objects, i.e., we could generate the data
structure definition based on the structure of metadata in runtime (for Python)
and in compile time (even maybe in runtime) for C++ and Rust.

The integration with Kubernetes (especially the CSI part) will be another key
improvement for *v0.5.0*.

Further details about release for *v0.5.0* will be added later.

v0.4.0
------

The release of vineyard *v0.4.0*, will be hopefully released in Aug, 2021, will
be a follow-up bugfix releases after *v0.3.0*. The version *v0.4.0* makes the
kubernetes related components better.

+ Improve the robustness of the scheduler plugin.
+ Refine the definition of CRDs.
+ Distribute the vineyard operator to artifact hub as a chart, to make it available for more users.

v0.3.0
------

We plan to release *v0.3.0* in June, 2021. vineyard *v0.3.0*
will be the first major stable releases with fully kubernetes support, which will include:

+ A stable CRD definition for ``LocalObject`` and ``GlobalObject`` to represents vineyard objects
  as kubernetes resources.
+ A full-features scheduler plugin for kubernetes, as well as a custom controller that manages
  objects (custom resources) in vineyard cluster.
+ A refined version of Helm integration.
+ Application-aware far memory will be included in v0.3.0 as an experimental feature.

v0.2.0
------

Vineyard *v0.2.0* will address the issue about Python ecosystem compatibility, I/O, and
the kubernetes integration.

+ Vineyard *v0.2.0* will support any *filesystem-spec*-compatible data source/sink as well as file
  format.
+ Vineyard *v0.2.0* will support Python ecosystem (especially numpy and pandas) better.
+ Vineyard *v0.2.0* will include basic Helm integration for deploying on Kubernetes as a ``DaemonSet``.
+ A prototype of scheduler plugin to do data locality scheduling will be included into vineyard v0.2.0
  to demonstrates the capability about co-scheduling job and data in kubernetes brought by vineyard.
+ Match the criterion of CNCF sandbox project.

v0.1.0
------

Vineyard *v0.1.0* is the first release after open source. This version includes:

+ Complete functionality for both server and client.
+ Complete Python SDK.
+ User-friendly package distribution on pypi (for python SDK) and on dockerhub (for vineyardd server).

Release Notes
-------------

For more details about what changes happened for every version, please refer to
our `releases notes`_ as well.

.. _releases notes: https://github.com/v6d-io/v6d/releases
