# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved.

sources = [ 'dpdk_pmd_ts.c' ]

tslib = static_library('ts_dpdk_pmd', sources,
                       include_directories: lib_dir,
                       dependencies: [ dep_rpc ])
