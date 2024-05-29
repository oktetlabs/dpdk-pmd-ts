#! /bin/bash
# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved.
#
# Generate TRC status reports.
#
# Author: Andrew Rybchenko <Andrew.Rybchenko@oktetlabs.ru>
#

source "$(dirname "$(which $0)")"/guess.sh

DPDK_VER=v24.03
DPDK_NUM=24030016

declare -a COMMON_OPTS
COMMON_OPTS+=(--db="${TE_TS_TRC_DB}")
if [[ -n "${TE_TS_RIGSDIR}" ]] && [[ -r "${TE_TS_RIGSDIR}"/trc.key2html ]] ; then
    COMMON_OPTS+=(--key2html="${TE_TS_RIGSDIR}"/trc.key2html)
fi

declare -a COMMON_TAGS
COMMON_TAGS+=(dpdk:"${DPDK_NUM}")

declare -a OPTS1
declare -a OPTS2

for tag in "${COMMON_TAGS[@]}" ; do
    OPTS1+=(-1 "${tag}")
    OPTS2+=(-2 "${tag}")
done

SHORT_EXCLUDE_OPTS=(--2-exclude=WONTFIX)


#
# SN1022 EF100 status
#

declare -a SN1022_EF100_TAGS
SN1022_EF100_TAGS+=(pci-10ee pci-10ee-0100)
SN1022_EF100_TAGS+=(rx-datapath-ef100 tx-datapath-ef100)
# Number of VFs should be greater than zero
SN1022_EF100_TAGS+=(num_vfs:1)

declare -a SN1022_EF100_OPTS2
SN1022_EF100_OPTS2=("${OPTS2[@]}")
for tag in "${SN1022_EF100_TAGS[@]}" ; do
    SN1022_EF100_OPTS2+=(-2 "${tag}")
done

declare -a SN1022_EF100_STATUS_OPTS
SN1022_EF100_STATUS_OPTS+=("${COMMON_OPTS[@]}")
SN1022_EF100_STATUS_OPTS+=(--1-name="Reference")
SN1022_EF100_STATUS_OPTS+=("${OPTS1[@]}")
SN1022_EF100_STATUS_OPTS+=(--2-name="SN1022")
SN1022_EF100_STATUS_OPTS+=(--2-show-keys)
SN1022_EF100_STATUS_OPTS+=("${SN1022_EF100_OPTS2[@]}")

te-trc-diff \
    --html=SN1022-EF100-DPDK-${DPDK_VER}-full.html \
    --title="SN1022 EF100 in DPDK ${DPDK_VER} status" \
    "${SN1022_EF100_STATUS_OPTS[@]}"

te-trc-diff \
    --html=SN1022-EF100-DPDK-${DPDK_VER}-short.html \
    --title="SN1022 EF100 in DPDK ${DPDK_VER} short status" \
    "${SN1022_EF100_STATUS_OPTS[@]}" \
    "${SHORT_EXCLUDE_OPTS[@]}"


#
# SN1022 Virtio-Net status
#

declare -a VIRTIO_NET_OPTS1
declare -a SN1022_VIRTIO_NET_OPTS2

declare -a VIRTIO_NET_TAGS
VIRTIO_NET_TAGS+=(pci-1af4-1000 pci-sub-1af4-0001)

for tag in "${VIRTIO_NET_TAGS[@]}" ; do
    VIRTIO_NET_OPTS1+=(-1 "${tag}")
    SN1022_VIRTIO_NET_OPTS2+=(-2 "${tag}")
done

declare -a SN1022_VIRTIO_NET_TAGS
SN1022_VIRTIO_NET_TAGS+=(xilinx-virtio-net)

for tag in "${SN1022_VIRTIO_NET_TAGS[@]}" ; do
    SN1022_VIRTIO_NET_OPTS2+=(-2 "${tag}")
done

declare -a SN1022_VNET_STATUS_OPTS
SN1022_VNET_STATUS_OPTS+=("${COMMON_OPTS[@]}")
SN1022_VNET_STATUS_OPTS+=(--1-name="Virtio-Net")
SN1022_VNET_STATUS_OPTS+=("${VIRTIO_NET_OPTS1[@]}")
SN1022_VNET_STATUS_OPTS+=(--2-name="SN1022")
SN1022_VNET_STATUS_OPTS+=(--2-show-keys)
SN1022_VNET_STATUS_OPTS+=("${SN1022_VIRTIO_NET_OPTS2[@]}")

te-trc-diff \
    --html=SN1022-Virtio-Net-DPDK-${DPDK_VER}-full.html \
    --title="SN1022 Virtio-Net in DPDK ${DPDK_VER} status" \
    "${SN1022_VNET_STATUS_OPTS[@]}"

te-trc-diff \
    --html=SN1022-Virtio-Net-DPDK-${DPDK_VER}-short.html \
    --title="SN1022 Virtio-Net in DPDK ${DPDK_VER} short status" \
    "${SN1022_VNET_STATUS_OPTS[@]}" \
    "${SHORT_EXCLUDE_OPTS[@]}"


#
# i40e status
#

declare -a I40E_TAGS
I40E_TAGS+=(net_i40e pci-8086 pci-8086-1572 pci-8086-1583)
# Number of VFs should be greater than zero
I40E_TAGS+=(num_vfs:1)

declare -a I40E_OPTS2
I40E_OPTS2=("${OPTS2[@]}")
for tag in "${I40E_TAGS[@]}" ; do
    I40E_OPTS2+=(-2 "${tag}")
done

declare -a I40E_STATUS_OPTS
I40E_STATUS_OPTS+=("${COMMON_OPTS[@]}")
I40E_STATUS_OPTS+=(--1-name="Reference")
I40E_STATUS_OPTS+=("${OPTS1[@]}")
I40E_STATUS_OPTS+=(--2-name="i40e")
I40E_STATUS_OPTS+=(--2-show-keys)
I40E_STATUS_OPTS+=("${I40E_OPTS2[@]}")

te-trc-diff \
    --html=i40e-DPDK-${DPDK_VER}-full.html \
    --title="i40e in DPDK ${DPDK_VER} status" \
    "${I40E_STATUS_OPTS[@]}"

te-trc-diff \
    --html=i40e-DPDK-${DPDK_VER}-short.html \
    --title="i40e in DPDK ${DPDK_VER} short status" \
    "${I40E_STATUS_OPTS[@]}" \
    "${SHORT_EXCLUDE_OPTS[@]}"
