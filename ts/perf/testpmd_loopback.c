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
    TEST_GET_INT_LIST_PARAM(testpmd_command_txpkts, txpkts_len);
    n_fwd_cores = TEST_UINT_PARAM(n_fwd_cores);
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
    CHECK_RC(tapi_dpdk_testpmd_get_link_speed(&testpmd_job, &link_speed));

    TEST_STEP("Initialize Tx and Rx statistics");
    CHECK_RC(te_meas_stats_init(&meas_stats_tx, TEST_MEAS_MAX_NUM_DATAPOINTS,
                                TEST_MEAS_INIT_FLAGS,
                                TEST_MEAS_MIN_NUM_DATAPOINTS,
                                TEST_MEAS_REQUIRED_CV,
                                TEST_MEAS_ALLOWED_SKIPS,
                                TEST_MEAS_DEVIATION_COEFF));

    CHECK_RC(te_meas_stats_init(&meas_stats_rx, TEST_MEAS_MIN_NUM_DATAPOINTS,
                                0, 0, 0, 0, 0));

    TEST_STEP("Retrieve Tx stats from running testpmd");
    CHECK_RC(tapi_dpdk_testpmd_get_stats(&testpmd_job, &meas_stats_tx,
                                         &meas_stats_rx));

    TEST_STEP("Check and log measurement results");
    if (meas_stats_tx.data.mean == 0 || meas_stats_rx.data.mean == 0)
        TEST_VERDICT("Failure: zero Rx or Tx packets per second");

    tapi_dpdk_stats_log_rates(TAPI_DPDK_TESTPMD_NAME, &meas_stats_tx,
                              packet_size, link_speed, NULL);

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
