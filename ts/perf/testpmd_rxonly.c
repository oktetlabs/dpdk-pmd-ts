/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Performance Test Suite
 */

/** @defgroup perf-testpmd_rxonly Test dpdk-testpmd performance in rxonly mode
 * @ingroup perf
 * @{
 *
 * @objective Test dpdk-testpmd performance in rxonly mode
 *
 *
 * @type performance
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "perf/testpmd_rxonly"

#include "dpdk_pmd_test.h"
#include "tapi_job.h"
#include "tapi_cfg_cpu.h"
#include "tapi_dpdk.h"
#include "tapi_dpdk_stats.h"
#include "dpdk_pmd_test_perf.h"

#define TEST_TESTPMD_TX_GENERATOR_TXD 512U
#define TEST_TESTPMD_TX_GENERATOR_BURST 128U
#define TEST_TESTPMD_TX_GENERATOR_TXFREET 0U

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_jobs_ctrl = NULL;
    rcf_rpc_server *tst_jobs_ctrl = NULL;
    const struct if_nameindex *iut_port = NULL;

    tapi_dpdk_testpmd_job_t iut_testpmd_job = {0};
    tapi_dpdk_testpmd_job_t tst_testpmd_job = {0};

    te_meas_stats_t iut_stats_rx = {0};
    te_meas_stats_t tst_stats_tx = {0};

    tapi_cpu_prop_t prop = { .isolated = TRUE };

    const char *generator_mode;
    unsigned int testpmd_arg_rxq;
    unsigned int n_rx_cores;
    unsigned int n_tx_cores;
    unsigned int iut_link_speed;
    unsigned int tst_link_speed;
    unsigned int mbuf_size;
    unsigned int mtu;
    unsigned int packet_size;
    const char *txpkts;
    char *iut_mac;
    cfg_val_type type = CVT_STRING;
    unsigned int max_rx_queues;

    te_kvpair_h *traffic_generator_params = NULL;

    te_bool dbells_supp;
    te_kvpair_h dbells_opt;
    te_kvpair_init(&dbells_opt);

    TEST_START;
    TEST_GET_PCO(iut_jobs_ctrl);
    TEST_GET_PCO(tst_jobs_ctrl);
    TEST_GET_IF(iut_port);
    TEST_GET_STRING_PARAM(generator_mode);
    TEST_GET_UINT_PARAM(testpmd_arg_rxq);
    TEST_GET_UINT_PARAM(n_rx_cores);
    TEST_GET_UINT_PARAM(packet_size);
    txpkts = TEST_STRING_PARAM(packet_size);

    CHECK_RC(test_get_pci_fn_prop(iut_jobs_ctrl, iut_port,
                                  "max_rx_queues", &max_rx_queues));
    if (testpmd_arg_rxq > max_rx_queues)
    {
        TEST_SKIP("So many Rx queues are not supported");
    }

    test_check_mtu(iut_jobs_ctrl, iut_port, packet_size);

    CHECK_RC(cfg_get_instance_str(&type, &iut_mac,
                                  "/local:/dpdk:/mac:" TEST_ENV_IUT_PORT));
    CHECK_RC(test_create_traffic_generator_params(tst_jobs_ctrl->ta,
                                    TAPI_DPDK_TESTPMD_ARG_PREFIX,
                                    TAPI_DPDK_TESTPMD_COMMAND_PREFIX,
                                    generator_mode, txpkts,
                                    testpmd_arg_rxq > 1, 0,
                                    TEST_TESTPMD_TX_GENERATOR_TXD,
                                    TEST_TESTPMD_TX_GENERATOR_BURST,
                                    TEST_TESTPMD_TX_GENERATOR_TXFREET,
                                    &traffic_generator_params, &n_tx_cores));
    CHECK_RC(te_kvpair_add(traffic_generator_params,
                           TAPI_DPDK_TESTPMD_ARG_PREFIX "eth_peer",
                           "0,%s", iut_mac));

    if (tapi_dpdk_mtu_by_pkt_size(packet_size, &mtu))
    {
        CHECK_RC(te_kvpair_add(&test_params,
                               TAPI_DPDK_TESTPMD_COMMAND_PREFIX "mtu",
                               "%u", mtu));
    }
    if (tapi_dpdk_mbuf_size_by_pkt_size(packet_size, &mbuf_size))
    {
        CHECK_RC(te_kvpair_add(&test_params,
                               TAPI_DPDK_TESTPMD_ARG_PREFIX "mbuf_size",
                               "%u", mbuf_size));
    }

    TEST_STEP("Adjust testpmd parameters");
    /*
     * Set number of TxQs equal to the number of RxQs to
     * enable all specified forwarding cores.
     */
    CHECK_RC(te_kvpair_add(&test_params, "testpmd_arg_txq",
                           TEST_STRING_PARAM(testpmd_arg_rxq)));

    CHECK_RC(tapi_dpdk_add_rx_dbells_display(&dbells_opt,
                                        TEST_STRING_PARAM(testpmd_arg_rxq)));

    CHECK_RC(tapi_dpdk_testpmd_is_opt_supported(iut_jobs_ctrl, &env,
                                                &dbells_opt, &dbells_supp));
    if (dbells_supp)
    {
        CHECK_RC(tapi_dpdk_add_rx_dbells_display(&test_params,
                                        TEST_STRING_PARAM(testpmd_arg_rxq)));
    }

    TEST_STEP("Create testpmd job to run rxonly on IUT");
    CHECK_RC(tapi_dpdk_create_testpmd_job(iut_jobs_ctrl, &env, n_rx_cores,
                                          &prop, &test_params,
                                          &iut_testpmd_job));

    TEST_STEP("Create testpmd job to run traffic generator on TST");
    CHECK_RC(tapi_dpdk_create_testpmd_job(tst_jobs_ctrl, &env, n_tx_cores,
                                          &prop, traffic_generator_params,
                                          &tst_testpmd_job));

    TEST_STEP("Start the jobs");

    if (dbells_supp)
    {
        TEST_STEP("Attach doorbells filters");
        CHECK_RC(tapi_dpdk_attach_dbells_filter_rx(&iut_testpmd_job));
    }

    CHECK_RC(tapi_dpdk_testpmd_start(&iut_testpmd_job));
    CHECK_RC(tapi_dpdk_testpmd_start(&tst_testpmd_job));

    TEST_STEP("Retrieve link speed from running testpmd-s");
    CHECK_RC(tapi_dpdk_testpmd_get_link_speed(&iut_testpmd_job,
                                              &iut_link_speed));
    CHECK_RC(tapi_dpdk_testpmd_get_link_speed(&tst_testpmd_job,
                                              &tst_link_speed));

    TEST_STEP("Initialize IUT Rx and TST Tx statistics");
    CHECK_RC(te_meas_stats_init(&iut_stats_rx, TEST_MEAS_MAX_NUM_DATAPOINTS,
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

    TEST_STEP("Retrieve Rx stats from running testpmd");
    CHECK_RC(tapi_dpdk_testpmd_get_stats(&iut_testpmd_job, NULL,
                                         &iut_stats_rx));

    TEST_STEP("Retrieve Tx stats from running testpmd");
    CHECK_RC(tapi_dpdk_testpmd_get_stats(&tst_testpmd_job, &tst_stats_tx,
                                         NULL));

    TEST_STEP("Check and log measurement results");
    if (iut_stats_rx.data.mean == 0 || tst_stats_tx.data.mean == 0)
        TEST_VERDICT("Failure: zero Tx or Rx packets per second");

    tapi_dpdk_stats_log_rates(TAPI_DPDK_TESTPMD_NAME, &iut_stats_rx,
                              packet_size, iut_link_speed, "Rx");
    tapi_dpdk_stats_log_rates(TAPI_DPDK_TESTPMD_NAME, &tst_stats_tx,
                              packet_size, tst_link_speed, "Tx");

    if (dbells_supp)
        CHECK_RC(tapi_dpdk_stats_log_rx_dbells(&iut_testpmd_job,
                                               &iut_stats_rx));

    TEST_SUCCESS;

cleanup:
    tapi_dpdk_testpmd_destroy(&tst_testpmd_job);
    tapi_dpdk_testpmd_destroy(&iut_testpmd_job);
    te_kvpair_fini(traffic_generator_params);

    te_meas_stats_free(&iut_stats_rx);
    te_meas_stats_free(&tst_stats_tx);

    TEST_END;
}
/** @} */
