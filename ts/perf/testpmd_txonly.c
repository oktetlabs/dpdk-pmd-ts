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
    const struct if_nameindex *iut_ifs[TEST_MAX_IUT_PORTS] = { NULL };
    size_t n_ports = 0;

    tapi_dpdk_testpmd_job_t testpmd_job = {0};
    tapi_dpdk_testpmd_job_t testpmd_job_rx = {0};

    tapi_cpu_prop_t prop = { .isolated = TRUE };

    unsigned int packet_size;

    int *testpmd_command_txpkts;
    unsigned int n_fwd_cores;
    unsigned int testpmd_arg_txq;
    uint64_t tx_offloads = 0;
    int txpkts_len;
    unsigned int testpmd_arg_txonly_tso_mss;
    te_bool tso_requested;
    unsigned long rx_pkts;
    unsigned long rx_bytes;
    size_t idx;
    int i;

    unsigned int port;
    unsigned int n_iut_ports = 0;
    unsigned int n_tst_ports = 0;
    unsigned int iut_ports[TEST_MAX_IUT_PORTS] = {};
    unsigned int tst_ports[TEST_MAX_IUT_PORTS] = {};
    unsigned int iut_link_speed[TEST_MAX_IUT_PORTS] = {};
    unsigned int tst_link_speed[TEST_MAX_IUT_PORTS] = {};
    te_meas_stats_t meas_stats_tx[TEST_MAX_IUT_PORTS] = {0};
    te_meas_stats_t meas_stats_rx[TEST_MAX_IUT_PORTS] = {0};

    te_kvpair_h *rx_params = NULL;
    te_string str = TE_STRING_INIT;

    te_bool dbells_supp;
    te_kvpair_h dbells_opt;
    te_kvpair_init(&dbells_opt);

    TEST_START;
    TEST_GET_PCO(iut_jobs_ctrl);
    TEST_GET_PCO(tst_jobs_ctrl);
    TEST_GET_INT_LIST_PARAM(testpmd_command_txpkts, txpkts_len);

    n_fwd_cores = TEST_UINT_PARAM(n_fwd_cores);
    TEST_GET_UINT_PARAM(testpmd_arg_txq);

    for (idx = 0; idx < TE_ARRAY_LEN(iut_ifs); ++idx, ++n_ports)
    {
        unsigned int max_tx_queues;

        te_string_reset(&str);
        te_string_append(&str, TEST_ENV_IUT_PORT "%u", idx);
        iut_ifs[idx] = tapi_env_get_if(&env, te_string_value(&str));
        if (iut_ifs[idx] == NULL)
            break;

        CHECK_RC(test_get_pci_fn_prop(iut_jobs_ctrl, iut_ifs[idx],
                                      "max_tx_queues", &max_tx_queues));
        if (testpmd_arg_txq > max_tx_queues)
        {
            TEST_SKIP("So many Tx queues are not supported");
        }

        if (txpkts_len > 1)
        {
            if (!test_conf_tx_offload_supported(iut_jobs_ctrl, iut_ifs[idx],
                    TARPC_RTE_ETH_TX_OFFLOAD_MULTI_SEGS_BIT))
            {
                TEST_SKIP("Iteration skipped due to unsupported multi seg offload");
            }
            /*
             * Enable MULTI_SEGS Tx offload.
             * TODO avoid hardcodes: RTE_ETH_TX_OFFLOAD_MULTI_SEGS is RTE_BIT64(15)
             */
            tx_offloads |= UINT64_C(1) << 15;
        }

        tso_requested = TEST_HAS_PARAM(testpmd_arg_txonly_tso_mss);
        if (tso_requested)
        {
            if (!test_conf_tx_offload_supported(iut_jobs_ctrl, iut_ifs[idx],
                    TARPC_RTE_ETH_TX_OFFLOAD_TCP_TSO_BIT))
                TEST_SKIP("TSO is not supported");
            /*
             * Enable TSO Tx offload.
             * TODO avoid hardcodes: RTE_ETH_TX_OFFLOAD_TCP_TSO is RTE_BIT64(5)
             */
            tx_offloads |= UINT64_C(1) << 5;
        }
    }

    if (tx_offloads != 0)
    {
        /*
         * TODO testpmd should provide a way to enable Tx offloads
         * without numbers hardcoding.
         */
        CHECK_RC(te_kvpair_add(&test_params, "testpmd_arg_tx_offloads",
                               "0x%llx", (unsigned long long)tx_offloads));
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

    for (idx = 0; idx < n_ports; ++idx)
        test_check_mtu(iut_jobs_ctrl, iut_ifs[idx], packet_size);

    TEST_STEP("Adjust testpmd parameters");
    /*
     * Set number of RxQs equal to the number of TxQs to
     * enable all specified forwarding cores.
     */
    CHECK_RC(te_kvpair_add(&test_params, "testpmd_arg_rxq", "%s",
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

    CHECK_RC(tapi_dpdk_testpmd_start(&testpmd_job_rx));
    CHECK_RC(tapi_dpdk_testpmd_start(&testpmd_job));

    TEST_STEP("Retrieve link speed from running testpmd-s");
    CHECK_RC(tapi_dpdk_testpmd_get_link_speed_many_ports(&testpmd_job,
                                                         n_ports,
                                                         &n_iut_ports,
                                                         iut_ports,
                                                         iut_link_speed));
    CHECK_RC(tapi_dpdk_testpmd_get_link_speed_many_ports(&testpmd_job_rx,
                                                         n_ports,
                                                         &n_tst_ports,
                                                         tst_ports,
                                                         tst_link_speed));

    TEST_STEP("Initialize TST Rx and IUT Tx statistics");
    for (port = 0; port < n_ports; ++port)
    {
        CHECK_RC(test_meas_stats_init(&meas_stats_rx[port]));
        CHECK_RC(test_meas_stats_init(&meas_stats_tx[port]));
    }

    TEST_STEP("Retrieve Rx stats from running testpmd");
    CHECK_RC(tapi_dpdk_testpmd_get_stats_many_ports(&testpmd_job_rx, n_ports,
                                                    &n_tst_ports, tst_ports,
                                                    NULL, meas_stats_rx));

    TEST_STEP("Retrieve Tx stats from running testpmd");
    CHECK_RC(tapi_dpdk_testpmd_get_stats_many_ports(&testpmd_job, n_ports,
                                                    &n_iut_ports, iut_ports,
                                                    meas_stats_tx, NULL));

    if (tso_requested)
    {
        TEST_STEP("Check TSO correctness in the sense of packets size");
        CHECK_RC(tapi_dpdk_get_last_rx_pkts_bytes(&testpmd_job_rx, &rx_pkts,
                                                  &rx_bytes));
        if ((rx_bytes / rx_pkts) > packet_size)
            TEST_VERDICT("Sanity check of TSO on Rx side failed");
    }

    TEST_STEP("Check and log measurement results");
    for (port = 0; port < n_ports; ++port)
    {
        if (meas_stats_rx[port].data.mean == 0 ||
            meas_stats_tx[port].data.mean == 0)
            TEST_VERDICT("Failure: zero Tx or Rx packets per second");

        te_string_reset(&str);
        te_string_append(&str, "Tx");
        if (n_ports > 1)
            te_string_append(&str, "%u", port);
        tapi_dpdk_stats_log_rates(TAPI_DPDK_TESTPMD_NAME, &meas_stats_tx[port],
                                  packet_size, iut_link_speed[port],
                                  te_string_value(&str));

        te_string_reset(&str);
        te_string_append(&str, "Rx");
        if (n_ports > 1)
            te_string_append(&str, "%u", port);
        tapi_dpdk_stats_log_rates(TAPI_DPDK_TESTPMD_NAME, &meas_stats_rx[port],
                                  packet_size, tst_link_speed[port],
                                  te_string_value(&str));

        if (dbells_supp)
            CHECK_RC(tapi_dpdk_stats_log_tx_dbells(&testpmd_job, &meas_stats_tx[port]));
    }

    if (n_ports > 1)
    {
        tapi_dpdk_stats_log_aggr_rates(TAPI_DPDK_TESTPMD_NAME, n_ports,
                                       meas_stats_tx, packet_size,
                                       iut_link_speed, "Tx");
    }

    TEST_SUCCESS;

cleanup:
    tapi_dpdk_testpmd_destroy(&testpmd_job);
    tapi_dpdk_testpmd_destroy(&testpmd_job_rx);
    for (port = 0; port < n_ports; ++port)
    {
        te_meas_stats_free(&meas_stats_tx[port]);
        te_meas_stats_free(&meas_stats_rx[port]);
    }
    te_kvpair_fini(rx_params);
    te_string_free(&str);

    TEST_END;
}
/** @} */
