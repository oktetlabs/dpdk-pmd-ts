/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * L2FWD Performance Test Suite
 */

/** @defgroup perf-l2fwd_simple Base test for l2fwd application
 * @ingroup perf
 * @{
 *
 * @objective Test l2fwd performance
 *
 * @type performance
 *
 * @author Georgiy Levashov <Georgiy.Levashov@oktetlabs.ru>
 *
 *
 * @par Scenario:
 */
#define TE_TEST_NAME "perf/l2fwd_simple"

#include "dpdk_pmd_test.h"
#include "tapi_job.h"
#include "tapi_cfg_cpu.h"
#include "tapi_l2fwd.h"
#include "tapi_dpdk.h"
#include "tapi_dpdk_stats.h"
#include "dpdk_pmd_test_perf.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_jobs_ctrl = NULL;
    rcf_rpc_server *tst_jobs_ctrl = NULL;

    tapi_dpdk_l2fwd_job_t l2fwd_job = {0};
    tapi_dpdk_testpmd_job_t tst_testpmd_job = {0};

    tapi_cpu_prop_t prop = { .isolated = TRUE };

    unsigned int link_speed;
    const char  *generator_mode;
    unsigned int testpmd_arg_txd;
    unsigned int testpmd_arg_burst;
    unsigned int testpmd_arg_txfreet;
    unsigned int n_l2fwd_fwd_cores;
    unsigned int n_peer_cores;
    unsigned int packet_size;
    const char *txpkts;

    te_kvpair_h *traffic_generator_params = NULL;
    char *iut_mac;

    te_meas_stats_t meas_stats_tx = {0};
    te_meas_stats_t meas_stats_rx = {0};
    te_meas_stats_t tst_stats_tx = {0};

    TEST_START;
    TEST_GET_PCO(iut_jobs_ctrl);
    TEST_GET_PCO(tst_jobs_ctrl);
    TEST_GET_STRING_PARAM(generator_mode);
    TEST_GET_UINT_PARAM(testpmd_arg_txd);
    TEST_GET_UINT_PARAM(testpmd_arg_burst);
    TEST_GET_UINT_PARAM(testpmd_arg_txfreet);
    TEST_GET_UINT_PARAM(n_l2fwd_fwd_cores);
    TEST_GET_UINT_PARAM(packet_size);
    txpkts = TEST_STRING_PARAM(packet_size);

    CHECK_RC(test_create_traffic_generator_params(tst_jobs_ctrl->ta,
                                    TAPI_DPDK_TESTPMD_ARG_PREFIX,
                                    TAPI_DPDK_TESTPMD_COMMAND_PREFIX,
                                    generator_mode, txpkts, FALSE, 0,
                                    testpmd_arg_txd, testpmd_arg_burst,
                                    testpmd_arg_txfreet,
                                    &traffic_generator_params, &n_peer_cores));

    CHECK_RC(cfg_get_string(&iut_mac, "/local:/dpdk:/mac:%s%u",
                            TEST_ENV_IUT_PORT, 0));
    CHECK_RC(te_kvpair_add(traffic_generator_params,
                           TAPI_DPDK_TESTPMD_ARG_PREFIX "eth_peer",
                           "0,%s", iut_mac));

    TEST_STEP("Create l2fwd job");
    CHECK_RC(tapi_dpdk_create_l2fwd_job(iut_jobs_ctrl, &env, n_l2fwd_fwd_cores,
                                        &prop, &test_params, &l2fwd_job));

    TEST_STEP("Create testpmd job to run traffic generator on TST");
    CHECK_RC(tapi_dpdk_create_testpmd_job(tst_jobs_ctrl, &env,
                                          n_peer_cores,
                                          &prop, traffic_generator_params,
                                          &tst_testpmd_job));
    TEST_STEP("Start L2fwd application");
    CHECK_RC(tapi_dpdk_l2fwd_start(&l2fwd_job));

    TEST_STEP("Start traffic generator");
    CHECK_RC(tapi_dpdk_testpmd_start(&tst_testpmd_job));

    TEST_STEP("Retrieve link speed from running testpmd");
    CHECK_RC(tapi_dpdk_testpmd_get_link_speed(&tst_testpmd_job, &link_speed));

    TEST_STEP("Initialize Tx and Rx statistics");
    CHECK_RC(te_meas_stats_init(&meas_stats_tx, TEST_MEAS_MAX_NUM_DATAPOINTS,
                                TEST_MEAS_INIT_FLAGS,
                                TEST_MEAS_MIN_NUM_DATAPOINTS,
                                TEST_MEAS_REQUIRED_CV,
                                TEST_MEAS_ALLOWED_SKIPS,
                                TEST_MEAS_DEVIATION_COEFF));
    CHECK_RC(te_meas_stats_init(&tst_stats_tx, TEST_MEAS_MAX_NUM_DATAPOINTS,
                                TEST_MEAS_INIT_FLAGS,
                                TEST_MEAS_MIN_NUM_DATAPOINTS,
                                TEST_MEAS_REQUIRED_CV,
                                TEST_MEAS_ALLOWED_SKIPS,
                                TEST_MEAS_DEVIATION_COEFF));

    CHECK_RC(te_meas_stats_init(&meas_stats_rx, TEST_MEAS_MAX_NUM_DATAPOINTS,
                                TEST_MEAS_INIT_FLAGS,
                                TEST_MEAS_MIN_NUM_DATAPOINTS,
                                TEST_MEAS_REQUIRED_CV,
                                TEST_MEAS_ALLOWED_SKIPS,
                                TEST_MEAS_DEVIATION_COEFF));

    TEST_STEP("Retrieve stats from running l2fwd");
    CHECK_RC(tapi_dpdk_l2fwd_get_stats(&l2fwd_job, &meas_stats_tx,
                                       &meas_stats_rx));

    CHECK_RC(tapi_dpdk_testpmd_get_stats(&tst_testpmd_job, &tst_stats_tx,
                                         NULL));

    if (meas_stats_tx.data.mean == 0 || meas_stats_rx.data.mean == 0)
        TEST_VERDICT("Failure: zero Rx or Tx packets per second");

    tapi_dpdk_stats_log_rates(TAPI_DPDK_L2FWD_NAME, &meas_stats_tx,
                              packet_size, link_speed, "Tx");

    tapi_dpdk_stats_log_rates(TAPI_DPDK_L2FWD_NAME, &meas_stats_rx,
                              packet_size, link_speed, "Rx");

    tapi_dpdk_stats_log_rates(TAPI_DPDK_TESTPMD_NAME, &tst_stats_tx,
                              packet_size, link_speed, "Tx");

    TEST_SUCCESS;

cleanup:
    tapi_dpdk_l2fwd_destroy(&l2fwd_job);
    tapi_dpdk_testpmd_destroy(&tst_testpmd_job);
    te_kvpair_fini(traffic_generator_params);

    te_meas_stats_free(&meas_stats_tx);
    te_meas_stats_free(&meas_stats_rx);
    te_meas_stats_free(&tst_stats_tx);

    TEST_END;
}
/** @} */
