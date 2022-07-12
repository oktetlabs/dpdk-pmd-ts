#! /bin/bash
# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved.

set -e

for ta_type in ${TE_TA_TYPES} ; do
    igb_uio_ko="${TE_PREFIX}/lib/modules/$(uname -r)/extra/igb_uio.ko"
    test -f "${igb_uio_ko}" &&
        cp -p "${igb_uio_ko}" "${TE_AGENTS_INST}/${ta_type}/"

    cp -p "${TE_PREFIX}/bin/dpdk-testpmd" "${TE_AGENTS_INST}/${ta_type}/"
    cp -p "${TE_PREFIX}/bin/dpdk-l2fwd" "${TE_AGENTS_INST}/${ta_type}/"
done
