#! /bin/bash
# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved.
#
# Helper script to run Test Environment for the Test Suite
#
# Author: Andrew Rybchenko <Andrew.Rybchenko@oktetlabs.ru>
#

source "$(dirname "$(which "$0")")"/guess.sh

run_fail() {
    echo "$*" >&2
    exit 1
}

test -d "${RTE_SDK}" ||
    run_fail "Path to DPDK sources MUST be specified in RTE_SDK"

usage() {
cat <<EOF
USAGE: run.sh [run.sh options] [dispatcher.sh options]
Options:
  --cfg=<CFG>               Configuration to be used, some configurations support
                            colon-separated options:
  --cfg=cmod-virtio-net:<SPEC>
  --cfg=rheadington:<SPEC>  SN1022 C model configuration specification
                            [IUT]:IUT_SSH_PORT:IUT_RCF_PORT:[TST]:TST_IF

  --cfg=cmod-rhsim-ef100:[IUT]:[TST1]
  --cfg=cmod-rhsim-virtio:[IUT]:[TST1]
                            Fully automated SN1022 C model configuration spec

  --steal-cfg               Steal the configuration even if it is owned by
                            someone else
  --dev-args=<ARGs>         Per-device parameters to be appended to whitelist opts
  --eal-args=<ARGs>         Extra EAL command-line arguments
  --reuse-pco               Do not restart RPC servers and re-init EAL in each test
                            (it makes testing significantly faster)
  --vdev=<NAME>,<MODE>      Request automatic RTE vdev to be created and used as
                            IUT interface. The name must be either net_failsafe0
                            or net_bonding0, and in the latter case mode must be
                            set to a numeric value in accordance with DPDK guide.
                            Use --vdev=net_af_xdp0 to request af_xdp device.
  --iut-dpdk-drv=<NAME>     DPDK-compatible driver to be used on IUT agent
  --tst-dpdk-drv=<NAME>     DPDK-compatible driver to be used on TST agent

EOF
"${TE_BASE}"/dispatcher.sh --help
exit 1
}

function process_cmod_vm() {
    local hypervisor=$1 ; shift
    local peer=$1 ; shift

    test -z "${hypervisor}" || export TE_HYPERVISOR="${hypervisor}"
    test -z "${peer}" || export TE_TST1="${peer}"

    if test -z "${TE_TST1}" ; then
        eval export TE_TST1_LOCALHOST=true
        eval export TE_TST1_BUILD=local
    fi
    if test -z "${TE_HYPERVISOR}" ; then
        eval export TE_HYPERVISOR_LOCALHOST=true
        eval export TE_HYPERVISOR_BUILD=local
    fi
}

function process_cmod() {
    local iut=$1 ; shift
    local iut_ssh_port=$1 ; shift
    local iut_rcf_port=$1 ; shift
    local tst=$1 ; shift
    local tst_if=$1 ; shift

    test -z "${iut}" || export TE_IUT_SSH_PROXY="${iut}"
    test -z "${iut_ssh_port}" || export TE_IUT_SSH_PORT="${iut_ssh_port}"
    test -z "${iut_rcf_port}" || export TE_IUT_PORT="${iut_rcf_port}"
    test -z "${tst}" || export TE_TST1="${tst}"
    test -z "${tst_if}" || export TE_TST1_IUT="${tst_if}"

    test -n "${TE_IUT_SSH_PORT}" || run_fail "TE_IUT_SSH_PORT is unspecified"
    test -n "${TE_IUT_PORT}" || run_fail "TE_IUT_PORT is unspecified"
    test -n "${TE_TST1_IUT}" || run_fail "TE_TST1_IUT is unspecified"
}

function process_virtio() {
    local hpv_var=$1 ; shift
    local hypervisor=$1 ; shift
    local vm_drive=$1 ; shift

    test -z "${hypervisor}" || eval export "${hpv_var}"="${hypervisor}"
    test -z "${vm_drive}" || export TE_VM_DRIVE="${vm_drive}"

    if test -z "${!hpv_var}" ; then
        eval export "${hpv_var}"_LOCALHOST=true
        eval export "${hpv_var}"_BUILD=local
    fi
}

function process_cfg() {
    local cfg="$1" ; shift
    local run_conf
    local -a mod_opts

    case "${cfg}" in
        cmod-rhsim-ef100|cmod-rhsim-virtio)
            process_cmod_vm "$@" ; CFG= ;;
        cmod-virtio-net)
            process_cmod "$@" ; CFG= ;;
        rheadington)
            process_cmod "$@" ; CFG= ;;
        virtio_tap)
            process_virtio TE_TST1 "$@" ; CFG= ;;
        virtio_virtio)
            process_virtio TE_HYPERVISOR "$@" ; CFG= ;;
        *)
            "${TE_TS_TOPDIR}"/scripts/check_cfg "${cfg}" "" "${STEAL_CFG}" \
                || exit 1 ;;
    esac
    case "${cfg}" in
        *-vf-vf)    cfg=${cfg%-vf-vf} ; mod_opts=(--script=env/vf-vf) ;;
        *-vf-pf)    cfg=${cfg%-vf-pf} ; mod_opts=(--script=env/vf-pf) ;;
        *-vf)       cfg=${cfg%-vf} ;    mod_opts=(--script=env/vf)    ;;
    esac
    if test "${cfg}" != "${cfg%-p[0-9]}" ; then
        run_conf="${cfg%-p[0-9]}"
        # VF modifiers must be applied after port modifiers
        mod_opts=(--script=env/"${cfg#${run_conf}-}" "${mod_opts[@]}")
        cfg="${run_conf}"
    fi
    # Modifier must be applied after base configuration
    RUN_OPTS+=(--opts=run/"${cfg}" "${mod_opts[@]}")
    # Add test suite default options after configuration specifics
    RUN_OPTS+=(--opts=opts.ts)
}

CFG=
declare -a RUN_OPTS
declare -a MY_OPTS

for opt ; do
    case "${opt}" in
        --help) usage ;;
        --cfg=*)
            test -z "${CFG}" ||
                run_fail "Configuration is specified twice: ${CFG} vs ${opt#--cfg=}"
            CFG="${opt#--cfg=}"
            ;;
        --steal-cfg)
            STEAL_CFG=steal
            ;;
        --dev-args=*)
            export TE_DPDK_DEV_ARGS="${opt#--dev-args=}"
            ;;
        --iut-dev-args=*)
            export TE_IUT_DEV_ARGS="${opt#--iut-dev-args=}"
            ;;
        --eal-args=*)
            export TE_DPDK_EXTRA_EAL_ARGS="${opt#--eal-args=}"
            ;;
        --iut-eal-args=*)
            export TE_IUT_EXTRA_EAL_ARGS="${opt#--iut-eal-args=}"
            ;;
        --reuse-pco)
            export TE_ENV_REUSE_PCO=yes
            ;;
        --vdev=*)
            vdev=${opt#--vdev=}
            if [[ $vdev = *","* ]]; then
                export TE_ENV_RTE_VDEV_NAME=${vdev%,*}
                export TE_ENV_RTE_VDEV_MODE=${vdev#*,}
            else
                export TE_ENV_RTE_VDEV_NAME=${vdev}
                export TE_ENV_RTE_VDEV_MODE="0"
            fi
            if [[ $vdev = *"failsafe"* ]]; then
                # Failsafe driver uses a service core for interrupts
                export TE_IUT_REQUIRED_SERVICE_CORES="1"
            fi
            if [[ $vdev = *"af_xdp"* ]]; then
                # In prologue, the number of combined
                # channels is set to 1 to disable RSS
                MY_OPTS+=(--trc-tag="max_rx_queues:1")
                MY_OPTS+=(--trc-tag="max_tx_queues:1")
            fi
            ;;
        --iut-dpdk-drv=*)
            iut_dpdk_driver="${opt#--iut-dpdk-drv=}"
            export TE_ENV_IUT_DPDK_DRIVER=${iut_dpdk_driver}
            ;;
        --tst-dpdk-drv=*)
            tst_dpdk_driver="${opt#--tst-dpdk-drv=}"
            export TE_ENV_TST_DPDK_DRIVER=${tst_dpdk_driver}
            ;;
        *)  RUN_OPTS+=("${opt}") ;;
    esac
    shift 1
done

if test -n "${CFG}" ; then
    IFS=: ; process_cfg ${CFG} ; IFS=
fi

MY_OPTS+=(--conf-dirs="${TE_TS_CONFDIR}:${TS_RIGSDIR}${TS_RIGSDIR:+:}${SF_TS_CONFDIR}")

MY_OPTS+=(--trc-db="${TE_TS_TRC_DB}")
MY_OPTS+=(--trc-comparison=normalised)
MY_OPTS+=(--trc-html=trc-brief.html)
MY_OPTS+=(--trc-no-expected)
MY_OPTS+=(--trc-no-total --trc-no-unspec)
MY_OPTS+=(--trc-keep-artifacts)
MY_OPTS+=(--trc-key2html="${SF_TS_CONFDIR}"/trc.key2html)

# Add to RUN_OPTS since it specified in user environment and should
# override everything else
test "$TE_NOBUILD" = "yes" &&
    RUN_OPTS+=(--no-builder --tester-nobuild)
test -z "${TE_BUILDER_CONF}" ||
    RUN_OPTS+=(--conf-builder="${TE_BUILDER_CONF}")
test -z "${TE_TESTER_CONF}" ||
    RUN_OPTS+=(--conf-tester="${TE_TESTER_CONF}")

# Make sure that old TRC report is not kept
rm -f trc-brief.html

"${TE_BASE}"/dispatcher.sh "${MY_OPTS[@]}" "${RUN_OPTS[@]}"
RESULT=$?

if test -n "${STEAL_CFG}" -a -n "${CFG}" ; then
    "${TE_TS_TOPDIR}"/scripts/check_cfg "${CFG}" "-" "${STEAL_CFG}"
fi

if test ${RESULT} -ne 0 ; then
    echo FAIL
    echo ""
fi

echo -ne "\a"
exit ${RESULT}
