#! /bin/bash
# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved.
#
# Helper script to run Test Environment for the Test Suite
#
# Author: Andrew Rybchenko <Andrew.Rybchenko@oktetlabs.ru>
#

source "$(dirname "$(which "$0")")"/guess.sh

source "${TE_BASE}/scripts/lib"
source "${TE_BASE}/scripts/lib.grab_cfg"
source "${TE_BASE}/scripts/lib.meta"

if [[ -e "${TE_TS_RIGSDIR}/scripts/lib/grab_cfg_handlers" ]] ; then
    source "${TE_TS_RIGSDIR}/scripts/lib/grab_cfg_handlers"
fi

cleanup() {
    call_if_defined grab_cfg_release
}
trap "cleanup" EXIT

run_fail() {
    echo "$*" >&2
    exit 1
}

test -d "${RTE_SDK}" ||
    run_fail "Path to DPDK sources MUST be specified in RTE_SDK"

TE_RUN_META=yes

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

EOF

    call_if_defined grab_cfg_print_help

    cat <<EOF

  --checkup                 Run checkup
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

  --no-meta                 Do not generate testing metadata
  --publish                 Publish testing logs to Bublik

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
        *-vf-vf)    cfg=${cfg%-vf-vf} ; mod_opts=(--script=env/vf-vf) ;;
        *-vf-pf)    cfg=${cfg%-vf-pf} ; mod_opts=(--script=env/vf-pf) ;;
        *-vf)       cfg=${cfg%-vf} ;    mod_opts=(--script=env/vf)    ;;
    esac

    case "${cfg}" in
        cmod-rhsim-ef100|cmod-rhsim-virtio)
            process_cmod_vm "$@" ;;
        cmod-virtio-net)
            process_cmod "$@" ;;
        rheadington)
            process_cmod "$@" ;;
        virtio_tap)
            process_virtio TE_TST1 "$@" ;;
        virtio_virtio)
            process_virtio TE_HYPERVISOR "$@" ;;
        *)
            call_if_defined grab_cfg_process "${cfg}" || exit 1 ;;
    esac

    if test "${cfg}" != "${cfg%-p[0-9]}" ; then
        run_conf="${cfg%-p[0-9]}"
        # VF modifiers must be applied after port modifiers
        mod_opts=(--script=scripts/only-"${cfg#${run_conf}-}" "${mod_opts[@]}")
        cfg="${run_conf}"
    fi
    # Modifier must be applied after base configuration
    RUN_OPTS+=(--opts=run/"${cfg}" "${mod_opts[@]}")
}

CFG=
MODE=
declare -a RUN_OPTS
declare -a MY_OPTS

for opt ; do
    if call_if_defined grab_cfg_check_opt "${opt}" ; then
        shift 1
        continue
    fi

    case "${opt}" in
        --help) usage ;;
        --cfg=*)
            test -z "${CFG}" ||
                run_fail "Configuration is specified twice: ${CFG} vs ${opt#--cfg=}"
            CFG="${opt#--cfg=}"
            ;;
        --checkup)
            # Use negative requirement to exclude iterations
            # which values are marked as NO_TEST_HARNESS_CHECKUP
            RUN_OPTS+=('--tester-req=TEST_HARNESS_CHECKUP'
                       '--tester-req=!NO_TEST_HARNESS_CHECKUP')
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
                MODE="failsafe"
            fi
            if [[ $vdev = *"af_xdp"* ]]; then
                # In prologue, the number of combined
                # channels is set to 1 to disable RSS
                MY_OPTS+=(--trc-tag="max_rx_queues:1")
                MY_OPTS+=(--trc-tag="max_tx_queues:1")
                MODE="af_xdp"
            fi
            if [[ $vdev = *"bonding"* ]]; then
                MODE="bonding"
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
        --no-meta)
            TE_RUN_META=no
            RUN_OPTS+=("${opt}")
            ;;
        --publish)
            source "${TE_TS_RIGSDIR}/scripts/publish_logs/ts_publish"
            pscript="$(tsrigs_publish_get dpdk-ethdev-ts)"
            RUN_OPTS+=("--publish=${pscript}")
            ;;
        --build-only)
            export TE_TS_BUILD_ONLY="yes"
            ;;&
        *)  RUN_OPTS+=("${opt}") ;;
    esac
    shift 1
done

if test -n "${CFG}" ; then
    IFS=: ; process_cfg ${CFG} ; IFS=
fi

##########################################################################
# Set meta variable corresponding to a given device argument
# if it is present among arguments specification.
# Arguments:
#   Arguments specification (param1=value1,param2=value2,...).
#   Parameter name.
##########################################################################
dev_arg_to_meta() {
    local args="$1"
    local name="$2"
    local val=

    # Try to remove "${name}=" from the beginning.
    # If that succeeds, our parameter was at the beginning
    # and we are at the start of its value now.
    val="${args##${name}=}"
    if [[ "${val}" = "${args}" ]] ; then
        # Try to remove all from the beginning to ",${name}=".
        # If that succeeds, we go to the start of target
        # parameter value, dropping everything before it.
        val="${val##*,${name}=}"
    fi
    if [[ "${val}" = "${args}" ]] ; then
        # Nothing was removed, args do not contain
        # ${name}.
        return
    fi

    te_meta_set "${name^^}" "${val%%,*}"
}

if [[ "${TE_RUN_META}" = "yes" ]] ; then
    te_meta_test_suite "dpdk-ethdev-ts"

    te_meta_set CFG "${CFG}"
    te_meta_set_git "${SF_TS_CONFDIR}" TSCONF

    if [[ -n "${RTE_SDK}" ]] ; then
        te_meta_set_git "${RTE_SDK}" DPDK
    fi

    te_meta_set DEV_ARGS "${TE_IUT_DEV_ARGS}"
    te_meta_set EAL_ARGS "${TE_IUT_EXTRA_EAL_ARGS}"
    te_meta_set MODE "${MODE}"

    dev_arg_to_meta "${TE_IUT_DEV_ARGS}" rx_datapath
    dev_arg_to_meta "${TE_IUT_DEV_ARGS}" tx_datapath
fi

# Add test suite default options after configuration specifics
RUN_OPTS+=(--opts=opts.ts)

# TRC tags are not required in the case of --build-only.
if [[ -z "${TE_TS_BUILD_ONLY}" ]] ; then
    RUN_OPTS+=(--opts=trc-opts.ts)
fi

MY_OPTS+=(--conf-dirs="${TE_TS_CONFDIR}:${TE_TS_RIGSDIR}${TE_TS_RIGSDIR:+:}${SF_TS_CONFDIR}")

MY_OPTS+=(--trc-db="${TE_TS_TRC_DB}")
MY_OPTS+=(--trc-comparison=normalised)
MY_OPTS+=(--trc-html=trc-brief.html)
MY_OPTS+=(--trc-no-expected)
MY_OPTS+=(--trc-no-total --trc-no-unspec)
MY_OPTS+=(--trc-keep-artifacts)
if [[ -n "${TE_TS_RIGSDIR}" ]] && [[ -r "${TE_TS_RIGSDIR}"/trc.key2html ]] ; then
    MY_OPTS+=(--trc-key2html="${TE_TS_RIGSDIR}"/trc.key2html)
fi

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

if test ${RESULT} -ne 0 ; then
    echo FAIL
    echo ""
fi

echo -ne "\a"
exit ${RESULT}
