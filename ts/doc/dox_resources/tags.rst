..
   SPDX-License-Identifier: Apache-2.0
   (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved.

.. index:: pair: group; Tags
.. _tags_details:

TRC Tags List
=============

List of tags set on various testing configurations to distinguish them.

**This one is incomplete.**

Operating systems
-----------------

.. list-table::
  :header-rows: 1

  *
    - Name
    - Description
  *
    - linux
    - Test is run on Linux.
  *
    - linux-*
    - Test is executed on Linux of specific kernel version.

      | Example: for kernel 2.6.26-2, tags linux-2, linux-2.6, linux-2.6.26 and
        linux-2.6.26-2 will be defined. Tag linux-2.6 will have value 26.

      | Note: for Linux kernel v3, tag linux-2.6 is also defined, linux-2.6=40
        for kernel 3.0, linux-2.6=41 for kernel 3.1 and so on - so expressions
        like linux-2.6<32 will still work.

  *
    - freebsd
    - Test is run on FreeBSD.


Bitness
-------

.. list-table::
  :header-rows: 1

  *
    - Name
    - Description
  *
    - kernel-64
    - Kernel is 64bit.
  *
    - kernel-32
    - Kernel is 32bit.
  *
    - ul-64
    - User space is 64bit.
  *
    - ul-32
    - User space is 32bit.
