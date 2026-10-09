/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Performance Test Suite
 */

/** @defgroup perf-testpmd_loopback Test dpdk-testpmd performance in loopback mode
 * @ingroup perf
 * @{
 *
 * @objective Test dpdk-testpmd performance in loopback mode
 *
 *
 * @type performance
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "perf/testpmd_loopback"

#include "dpdk_pmd_test.h"
#include "tapi_job.h"
#include "tapi_cfg_cpu.h"
#include "tapi_dpdk.h"
#include "tapi_dpdk_stats.h"
#include "dpdk_pmd_test_perf.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_jobs_ctrl = NULL;
    const struct if_nameindex *iut_port = NULL;

    tapi_dpdk_testpmd_job_t testpmd_job = {0};

    tapi_cpu_prop_t prop = { .isolated = TRUE };

    unsigned int packet_size;

    int *testpmd_command_txpkts;
    int loopback_mode;
    unsigned int n_fwd_cores;
    unsigned int link_speed;
    int txpkts_len;
    int i;

    te_meas_stats_t meas_stats_tx = {0};
    te_meas_stats_t meas_stats_rx = {0};

    te_bool dbells_supp;
    te_kvpair_h dbells_opt;
    te_kvpair_init(&dbells_opt);

    TEST_START;
    TEST_GET_PCO(iut_jobs_ctrl);
    TEST_GET_IF(iut_port);
    TEST_PARAM_DOC(testpmd_command_txpkts,
        "Comma-separated list of segment lengths of transmitted packets, set by testpmd command `set txpkts`");
    TEST_GET_INT_LIST_PARAM(testpmd_command_txpkts, txpkts_len);
    TEST_PARAM_DOC(n_fwd_cores, "Number of CPU cores for testpmd on IUT");
    n_fwd_cores = TEST_UINT_PARAM(n_fwd_cores);
    TEST_PARAM_DOC(testpmd_command_loopback_mode,
        "Driver-specific loopback mode, set by testpmd command `port config all loopback`; the iteration is skipped if IUT does not support it");
    loopback_mode = TEST_INT_PARAM(testpmd_command_loopback_mode);

    rc = cfg_find_fmt(NULL, "/local:/dpdk:/iut_loopback_mode:%d",
                      loopback_mode);
    if (rc != 0)
    {
        if (TE_RC_GET_ERROR(rc) != TE_ENOENT)
            TEST_VERDICT("Failed to find out supported by IUT loopback mode");

        TEST_SKIP("Iteration skipped due to unsupported loopback mode by hardware");
    }

    for (i = 0, packet_size = 0; i < txpkts_len; i++)
        packet_size += (unsigned int)testpmd_command_txpkts[i];

    test_check_mtu(iut_jobs_ctrl, iut_port, packet_size);

    CHECK_RC(tapi_dpdk_add_tx_dbells_display(&dbells_opt, "1"));

    CHECK_RC(tapi_dpdk_testpmd_is_opt_supported(iut_jobs_ctrl, &env,
                                                &dbells_opt, &dbells_supp));
    if (dbells_supp)
    {
        CHECK_RC(tapi_dpdk_add_tx_dbells_display(&test_params, "1"));
    }

    TEST_STEP("Create testpmd job");
    TEST_PARAM_DOC(testpmd_arg_forward_mode,
        "Forwarding mode of testpmd on IUT (testpmd option `--forward-mode`)");
    TEST_PARAM_DOC(testpmd_arg_tx_first,
        "Whether to start forwarding by sending a burst of packets first (testpmd option `--tx-first`)");
    TEST_PARAM_DOC(testpmd_arg_stats_period,
        "Period in seconds of testpmd statistics display (testpmd option `--stats-period`)");
    TEST_PARAM_DOC(testpmd_arg_no_lsc_interrupt,
        "Whether to disable link status change interrupt (testpmd option `--no-lsc-interrupt`)");
    TEST_PARAM_DOC(testpmd_arg_txd,
        "Number of Tx ring descriptors (testpmd option `--txd`)");
    TEST_PARAM_DOC(testpmd_arg_burst,
        "Number of packets per burst (testpmd option `--burst`)");
    TEST_PARAM_DOC(testpmd_arg_rxfreet,
        "Free threshold of Rx rings, @c 0 means driver default (testpmd option `--rxfreet`)");
    TEST_PARAM_DOC(testpmd_arg_txfreet,
        "Free threshold of Tx rings, @c 0 means driver default (testpmd option `--txfreet`)");
    CHECK_RC(tapi_dpdk_create_testpmd_job(iut_jobs_ctrl, &env, n_fwd_cores,
                                          &prop, &test_params, &testpmd_job));

    TEST_STEP("Start the job");

    if (dbells_supp)
    {
        TEST_STEP("Attach doorbells filters");
        CHECK_RC(tapi_dpdk_attach_dbells_filter_tx(&testpmd_job));
    }
    CHECK_RC(tapi_dpdk_testpmd_start(&testpmd_job));

    TEST_STEP("Retrieve link speed from running testpmd");
    rc = tapi_dpdk_testpmd_get_link_speed(&testpmd_job, &link_speed);
    test_check_testpmd_link_speed_result(rc, "IUT");

    TEST_STEP("Initialize Tx and Rx statistics");
    CHECK_RC(test_meas_stats_init(&meas_stats_tx));
    CHECK_RC(te_meas_stats_init(&meas_stats_rx, TEST_MEAS_MIN_NUM_DATAPOINTS,
                                0, 0, 0, 0, 0));

    TEST_STEP("Retrieve Tx stats from running testpmd");
    CHECK_RC(tapi_dpdk_testpmd_get_stats(&testpmd_job, &meas_stats_tx,
                                         &meas_stats_rx));

    TEST_STEP("Check and log measurement results");
    CHECK_RC(test_report_loopback_rate(&meas_stats_tx, &meas_stats_rx,
                                       packet_size, link_speed));

    if (dbells_supp)
        CHECK_RC(tapi_dpdk_stats_log_tx_dbells(&testpmd_job, &meas_stats_tx));

    TEST_SUCCESS;

cleanup:
    tapi_dpdk_testpmd_destroy(&testpmd_job);
    te_meas_stats_free(&meas_stats_tx);
    te_meas_stats_free(&meas_stats_rx);

    TEST_END;
}
/** @} */
