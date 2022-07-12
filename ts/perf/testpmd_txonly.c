/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Performance Test Suite
 */

/** @defgroup perf-testpmd_txonly Test dpdk-testpmd performance in Tx only mode
 * @ingroup perf
 * @{
 *
 * @objective Test dpdk-testpmd performance in Tx only mode
 *
 *
 * @type performance
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "perf/testpmd_txonly"

#include "dpdk_pmd_test.h"
#include "tapi_job.h"
#include "tapi_cfg_cpu.h"
#include "tapi_dpdk.h"
#include "tapi_dpdk_stats.h"
#include "dpdk_pmd_test_perf.h"

#define TEST_TESTPMD_RX_QUEUES_NUM 4
#define TEST_TESTPMD_RX_CPUS_NUM 4
#define TEST_DEFAULT_TSO 512
#define TEST_HDRS_LEN (ETHER_HDR_LEN + TAD_IP4_HDR_LEN + TAD_TCP_HDR_LEN)

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_jobs_ctrl = NULL;
    rcf_rpc_server *tst_jobs_ctrl = NULL;
    const struct if_nameindex *iut_port = NULL;

    tapi_dpdk_testpmd_job_t testpmd_job = {0};
    tapi_dpdk_testpmd_job_t testpmd_job_rx = {0};

    tapi_cpu_prop_t prop = { .isolated = TRUE };

    unsigned int packet_size;

    int *testpmd_command_txpkts;
    unsigned int n_fwd_cores;
    unsigned int testpmd_arg_txq;
    unsigned int link_speed;
    unsigned int link_speed_rx;
    int txpkts_len;
    unsigned int max_tx_queues;
    unsigned int testpmd_arg_txonly_tso_mss;
    te_bool tso_requested;
    unsigned long rx_pkts;
    unsigned long rx_bytes;
    int i;

    te_meas_stats_t meas_stats_tx = {0};
    te_meas_stats_t meas_stats_rx = {0};

    te_kvpair_h *rx_params = NULL;

    te_bool dbells_supp;
    te_kvpair_h dbells_opt;
    te_kvpair_init(&dbells_opt);

    TEST_START;
    TEST_GET_PCO(iut_jobs_ctrl);
    TEST_GET_PCO(tst_jobs_ctrl);
    TEST_GET_IF(iut_port);
    TEST_GET_INT_LIST_PARAM(testpmd_command_txpkts, txpkts_len);

    n_fwd_cores = TEST_UINT_PARAM(n_fwd_cores);
    TEST_GET_UINT_PARAM(testpmd_arg_txq);

    CHECK_RC(test_get_pci_fn_prop(iut_jobs_ctrl, iut_port,
                                  "max_tx_queues", &max_tx_queues));
    if (testpmd_arg_txq > max_tx_queues)
    {
        TEST_SKIP("So many Tx queues are not supported");
    }

    if (txpkts_len > 1 &&
        !test_conf_tx_offload_supported(TARPC_RTE_DEV_TX_OFFLOAD_MULTI_SEGS_BIT))
    {
        TEST_SKIP("Iteration skipped due to unsupported multi seg offload");
    }

    tso_requested = TEST_HAS_PARAM(testpmd_arg_txonly_tso_mss);
    if (tso_requested &&
       !test_conf_tx_offload_supported(TARPC_RTE_DEV_TX_OFFLOAD_TCP_TSO_BIT))
    {
        TEST_SKIP("TSO is not supported");
    }


    if (tso_requested)
    {
        TEST_GET_UINT_PARAM(testpmd_arg_txonly_tso_mss);
        packet_size = TAPI_DPDK_TESTPMD_TSO_MSS_HDRS_LEN +
                      testpmd_arg_txonly_tso_mss;
    }
    else
    {
        for (i = 0, packet_size = 0; i < txpkts_len; i++)
                packet_size += (unsigned int)testpmd_command_txpkts[i];
    }

    test_check_mtu(iut_jobs_ctrl, iut_port, packet_size);

    TEST_STEP("Adjust testpmd parameters");
    /*
     * Set number of RxQs equal to the number of TxQs to
     * enable all specified forwarding cores.
     */
    CHECK_RC(te_kvpair_add(&test_params, "testpmd_arg_rxq",
                           TEST_STRING_PARAM(testpmd_arg_txq)));

    CHECK_RC(tapi_dpdk_add_tx_dbells_display(&dbells_opt,
                                        TEST_STRING_PARAM(testpmd_arg_txq)));

    CHECK_RC(tapi_dpdk_testpmd_is_opt_supported(iut_jobs_ctrl, &env,
                                                &dbells_opt, &dbells_supp));
    if (dbells_supp)
    {
        CHECK_RC(tapi_dpdk_add_tx_dbells_display(&test_params,
                                        TEST_STRING_PARAM(testpmd_arg_txq)));
    }

    if (tso_requested)
    {
        te_bool tso_mss_supp;
        te_kvpair_h tso_mss_opt;
        te_kvpair_init(&tso_mss_opt);

        CHECK_RC(te_kvpair_add(&tso_mss_opt,
                     TAPI_DPDK_TESTPMD_ARG_PREFIX "txonly-tso-mss", "%d",
                     TEST_DEFAULT_TSO));
        CHECK_RC(tapi_dpdk_testpmd_is_opt_supported(iut_jobs_ctrl, &env,
                                                  &tso_mss_opt, &tso_mss_supp));
        if (!tso_mss_supp)
            TEST_SKIP("testpmd does not support '--txonly-tso-mss' option");
    }

    /* This enables RSS that makes tester more likely to receive all traffic */
    CHECK_RC(te_kvpair_add(&test_params, "testpmd_arg_txonly_multi_flow",
                           "TRUE"));

    CHECK_RC(test_create_traffic_receiver_params(TAPI_DPDK_TESTPMD_ARG_PREFIX,
                                             TAPI_DPDK_TESTPMD_COMMAND_PREFIX,
                                             TEST_TESTPMD_RX_QUEUES_NUM,
                                             packet_size, &rx_params));

    TEST_STEP("Create testpmd jobs");
    CHECK_RC(tapi_dpdk_create_testpmd_job(tst_jobs_ctrl, &env,
                                          TEST_TESTPMD_RX_CPUS_NUM, &prop,
                                          rx_params, &testpmd_job_rx));
    CHECK_RC(tapi_dpdk_create_testpmd_job(iut_jobs_ctrl, &env, n_fwd_cores,
                                          &prop, &test_params, &testpmd_job));

    TEST_STEP("Start the jobs");

    if (dbells_supp)
    {
        TEST_STEP("Attach doorbells filters");
        CHECK_RC(tapi_dpdk_attach_dbells_filter_tx(&testpmd_job));
    }

    if (tso_requested)
    {
        TEST_STEP("Attach Rx packets/bytes filters");
        CHECK_RC(tapi_dpdk_attach_rx_pkts_bytes_filters(&testpmd_job_rx));
    }

    CHECK_RC(tapi_dpdk_testpmd_start(&testpmd_job));
    CHECK_RC(tapi_dpdk_testpmd_start(&testpmd_job_rx));

    TEST_STEP("Retrieve link speed from running testpmd-s");
    CHECK_RC(tapi_dpdk_testpmd_get_link_speed(&testpmd_job, &link_speed));
    CHECK_RC(tapi_dpdk_testpmd_get_link_speed(&testpmd_job_rx, &link_speed_rx));

    TEST_STEP("Initialize TST Rx and IUT Tx statistics");
    CHECK_RC(te_meas_stats_init(&meas_stats_rx, TEST_MEAS_MAX_NUM_DATAPOINTS,
                                TEST_MEAS_INIT_FLAGS,
                                TEST_MEAS_MIN_NUM_DATAPOINTS,
                                TEST_MEAS_REQUIRED_CV,
                                TEST_MEAS_ALLOWED_SKIPS,
                                TEST_MEAS_DEVIATION_COEFF));
    CHECK_RC(te_meas_stats_init(&meas_stats_tx, TEST_MEAS_MAX_NUM_DATAPOINTS,
                                TEST_MEAS_INIT_FLAGS,
                                TEST_MEAS_MIN_NUM_DATAPOINTS,
                                TEST_MEAS_REQUIRED_CV,
                                TEST_MEAS_ALLOWED_SKIPS,
                                TEST_MEAS_DEVIATION_COEFF));

    TEST_STEP("Retrieve Rx stats from running testpmd");
    CHECK_RC(tapi_dpdk_testpmd_get_stats(&testpmd_job_rx, NULL,
                                         &meas_stats_rx));

    TEST_STEP("Retrieve Tx stats from running testpmd");
    CHECK_RC(tapi_dpdk_testpmd_get_stats(&testpmd_job, &meas_stats_tx,
                                         NULL));

    if (tso_requested)
    {
        TEST_STEP("Check TSO correctness in the sense of packets size");
        CHECK_RC(tapi_dpdk_get_last_rx_pkts_bytes(&testpmd_job_rx, &rx_pkts,
                                                  &rx_bytes));
        if ((rx_bytes / rx_pkts) > packet_size)
            TEST_VERDICT("Sanity check of TSO on Rx side failed");
    }

    TEST_STEP("Check and log measurement results");
    if (meas_stats_rx.data.mean == 0 || meas_stats_tx.data.mean == 0)
        TEST_VERDICT("Failure: zero Tx or Rx packets per second");

    tapi_dpdk_stats_log_rates(TAPI_DPDK_TESTPMD_NAME, &meas_stats_tx,
                              packet_size, link_speed, "Tx");
    tapi_dpdk_stats_log_rates(TAPI_DPDK_TESTPMD_NAME, &meas_stats_rx,
                              packet_size, link_speed_rx, "Rx");

    if (dbells_supp)
        CHECK_RC(tapi_dpdk_stats_log_tx_dbells(&testpmd_job, &meas_stats_tx));

    TEST_SUCCESS;

cleanup:
    tapi_dpdk_testpmd_destroy(&testpmd_job);
    tapi_dpdk_testpmd_destroy(&testpmd_job_rx);
    te_meas_stats_free(&meas_stats_tx);
    te_meas_stats_free(&meas_stats_rx);
    te_kvpair_fini(rx_params);

    TEST_END;
}
/** @} */
