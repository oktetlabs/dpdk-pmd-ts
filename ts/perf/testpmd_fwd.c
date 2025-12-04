/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Performance Test Suite
 */

/** @defgroup perf-testpmd_rxonly Test dpdk-testpmd performance in IO forward mode
 * @ingroup perf
 * @{
 *
 * @objective Test dpdk-testpmd performance in IO forward mode
 *
 *
 * @type performance
 *
 * @author Andrew Rybchenko <andrew.rybchenko@oktetlabs.ru>
 *
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "perf/testpmd_fwd"

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
    const struct if_nameindex *iut_ifs[TEST_MAX_IUT_PORTS] = { NULL };
    size_t n_ports = 0;

    tapi_dpdk_testpmd_job_t iut_testpmd_job = {0};
    tapi_dpdk_testpmd_job_t tst_testpmd_job = {0};

    te_string str = TE_STRING_INIT;
    unsigned int port;
    unsigned int n_iut_ports = 0;
    unsigned int n_tst_ports = 0;
    unsigned int iut_ports[TEST_MAX_IUT_PORTS] = {};
    unsigned int tst_ports[TEST_MAX_IUT_PORTS] = {};
    unsigned int iut_link_speed[TEST_MAX_IUT_PORTS];
    unsigned int tst_link_speed[TEST_MAX_IUT_PORTS];
    te_meas_stats_t iut_stats_rx[TEST_MAX_IUT_PORTS] = {0};
    te_meas_stats_t iut_stats_tx[TEST_MAX_IUT_PORTS] = {0};
    te_meas_stats_t tst_stats_rx[TEST_MAX_IUT_PORTS] = {0};
    te_meas_stats_t tst_stats_tx[TEST_MAX_IUT_PORTS] = {0};

    tapi_cpu_prop_t prop = { .isolated = TRUE };

    const char *generator_mode;
    unsigned int testpmd_arg_rxq;
    unsigned int n_cores;
    unsigned int n_tst_cores;
    unsigned int mbuf_size;
    unsigned int mtu;
    unsigned int packet_size;
    const char *txpkts;
    size_t idx;

    te_kvpair_h *traffic_generator_params = NULL;

    te_bool dbells_supp;
    te_kvpair_h dbells_opt;
    te_kvpair_init(&dbells_opt);

    TEST_START;
    TEST_GET_PCO(iut_jobs_ctrl);
    TEST_GET_PCO(tst_jobs_ctrl);
    TEST_GET_STRING_PARAM(generator_mode);
    TEST_GET_UINT_PARAM(testpmd_arg_rxq);
    TEST_GET_UINT_PARAM(n_cores);
    TEST_GET_UINT_PARAM(packet_size);
    txpkts = TEST_STRING_PARAM(packet_size);

    for (idx = 0; idx < TE_ARRAY_LEN(iut_ifs); ++idx, ++n_ports)
    {
        unsigned int max_rx_queues;

        te_string_reset(&str);
        te_string_append(&str, TEST_ENV_IUT_PORT "%u", idx);
        iut_ifs[idx] = tapi_env_get_if(&env, te_string_value(&str));
        if (iut_ifs[idx] == NULL)
            break;

        CHECK_RC(test_get_pci_fn_prop(iut_jobs_ctrl, iut_ifs[idx],
                                      "max_rx_queues", &max_rx_queues));
        if (testpmd_arg_rxq > max_rx_queues)
        {
            TEST_SKIP("So many Rx queues are not supported");
        }

        test_check_mtu(iut_jobs_ctrl, iut_ifs[idx], packet_size);
    }

    CHECK_RC(test_create_traffic_generator_params(tst_jobs_ctrl->ta,
                                    TAPI_DPDK_TESTPMD_ARG_PREFIX,
                                    TAPI_DPDK_TESTPMD_COMMAND_PREFIX,
                                    generator_mode, txpkts,
                                    testpmd_arg_rxq > 1, 0,
                                    TEST_TESTPMD_TX_GENERATOR_TXD,
                                    TEST_TESTPMD_TX_GENERATOR_BURST,
                                    TEST_TESTPMD_TX_GENERATOR_TXFREET,
                                    &traffic_generator_params,
                                    &n_tst_cores));

    for (port = 0; port < n_ports; ++port)
    {
        char *iut_mac;

        CHECK_RC(cfg_get_string(&iut_mac, "/local:/dpdk:/mac:%s%u",
                                TEST_ENV_IUT_PORT, port));

        /*
         * Make argument with extra modifier to be unique, but dropped
         * by TAPI DPDK when testpmd arguments are composed.
         */
        te_string_reset(&str);
        te_string_append(&str, "%seth_peer%c%u",
                         TAPI_DPDK_TESTPMD_ARG_PREFIX,
                         TAPI_DPDK_TESTPMD_ARG_NMAE_CHOP,
                         port);
        CHECK_RC(te_kvpair_add(traffic_generator_params,
                               te_string_value(&str), "%u,%s", port, iut_mac));
        free(iut_mac);
    }

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
    CHECK_RC(te_kvpair_add(&test_params, "testpmd_arg_txq", "%s",
                           TEST_STRING_PARAM(testpmd_arg_rxq)));

    CHECK_RC(tapi_dpdk_add_rx_dbells_display(&dbells_opt,
                                        TEST_STRING_PARAM(testpmd_arg_rxq)));

    CHECK_RC(tapi_dpdk_testpmd_is_opt_supported(iut_jobs_ctrl, &env,
                                                &dbells_opt, &dbells_supp));
    if (dbells_supp)
    {
        CHECK_RC(tapi_dpdk_add_rx_dbells_display(&test_params,
                                        TEST_STRING_PARAM(testpmd_arg_rxq)));
        CHECK_RC(tapi_dpdk_add_tx_dbells_display(&test_params,
                                        TEST_STRING_PARAM(testpmd_arg_rxq)));
    }

    TEST_STEP("Create testpmd job to run rxonly on IUT");
    CHECK_RC(tapi_dpdk_create_testpmd_job(iut_jobs_ctrl, &env, n_cores,
                                          &prop, &test_params,
                                          &iut_testpmd_job));

    TEST_STEP("Create testpmd job to run traffic generator on TST");
    CHECK_RC(tapi_dpdk_create_testpmd_job(tst_jobs_ctrl, &env, n_tst_cores,
                                          &prop, traffic_generator_params,
                                          &tst_testpmd_job));

    TEST_STEP("Start the jobs");

    if (dbells_supp)
    {
        TEST_STEP("Attach doorbells filters");
        CHECK_RC(tapi_dpdk_attach_dbells_filter_rx(&iut_testpmd_job));
        CHECK_RC(tapi_dpdk_attach_dbells_filter_tx(&iut_testpmd_job));
    }

    CHECK_RC(tapi_dpdk_testpmd_start(&iut_testpmd_job));
    CHECK_RC(tapi_dpdk_testpmd_start(&tst_testpmd_job));

    TEST_STEP("Retrieve link speed from running testpmd-s");
    CHECK_RC(tapi_dpdk_testpmd_get_link_speed_many_ports(&iut_testpmd_job,
                                                         n_ports,
                                                         &n_iut_ports,
                                                         iut_ports,
                                                         iut_link_speed));
    CHECK_RC(tapi_dpdk_testpmd_get_link_speed_many_ports(&tst_testpmd_job,
                                                         n_ports,
                                                         &n_tst_ports,
                                                         tst_ports,
                                                         tst_link_speed));

    TEST_STEP("Initialize IUT Rx and TST Tx statistics");
    for (port = 0; port < n_ports; ++port)
    {
        CHECK_RC(test_meas_stats_init(&iut_stats_rx[port]));
        CHECK_RC(test_meas_stats_init(&iut_stats_tx[port]));
        CHECK_RC(test_meas_stats_init(&tst_stats_rx[port]));
        CHECK_RC(test_meas_stats_init(&tst_stats_tx[port]));
    }

    TEST_STEP("Retrieve Rx stats from running testpmd");
    CHECK_RC(tapi_dpdk_testpmd_get_stats_many_ports(&iut_testpmd_job,
                                                    n_ports,
                                                    &n_iut_ports, iut_ports,
                                                    iut_stats_tx,
                                                    iut_stats_rx));

    TEST_STEP("Retrieve Tx stats from running testpmd");
    CHECK_RC(tapi_dpdk_testpmd_get_stats_many_ports(&tst_testpmd_job,
                                                    n_ports,
                                                    &n_tst_ports, tst_ports,
                                                    tst_stats_tx,
                                                    tst_stats_rx));

    TEST_STEP("Check and log measurement results");
    for (port = 0; port < n_ports; ++port)
    {
        if (iut_stats_rx[port].data.mean == 0 ||
            iut_stats_tx[port].data.mean == 0 ||
            tst_stats_rx[port].data.mean == 0 ||
            tst_stats_tx[port].data.mean == 0)
            TEST_VERDICT("Failure: zero Tx or Rx packets per second");

        te_string_reset(&str);
        te_string_append(&str, "FwdRx%u", port);
        tapi_dpdk_stats_log_rates(TAPI_DPDK_TESTPMD_NAME, &iut_stats_rx[port],
                                  packet_size, iut_link_speed[port],
                                  te_string_value(&str));

        te_string_reset(&str);
        te_string_append(&str, "FwdTx%u", port);
        tapi_dpdk_stats_log_rates(TAPI_DPDK_TESTPMD_NAME, &iut_stats_tx[port],
                                  packet_size, iut_link_speed[port],
                                  te_string_value(&str));

        te_string_reset(&str);
        te_string_append(&str, "Rx%u", port);
        tapi_dpdk_stats_log_rates(TAPI_DPDK_TESTPMD_NAME, &tst_stats_rx[port],
                                  packet_size, tst_link_speed[port],
                                  te_string_value(&str));

        te_string_reset(&str);
        te_string_append(&str, "Tx%u", port);
        tapi_dpdk_stats_log_rates(TAPI_DPDK_TESTPMD_NAME, &tst_stats_tx[port],
                                  packet_size, tst_link_speed[port],
                                  te_string_value(&str));

        if (dbells_supp)
        {
            CHECK_RC(tapi_dpdk_stats_log_rx_dbells(&iut_testpmd_job,
                                                   &iut_stats_rx[port]));
            CHECK_RC(tapi_dpdk_stats_log_tx_dbells(&iut_testpmd_job,
                                                   &iut_stats_tx[port]));
        }
    }

    TEST_SUCCESS;

cleanup:
    tapi_dpdk_testpmd_destroy(&tst_testpmd_job);
    tapi_dpdk_testpmd_destroy(&iut_testpmd_job);
    te_kvpair_fini(traffic_generator_params);

    for (port = 0; port < n_ports; ++port)
    {
        te_meas_stats_free(&iut_stats_rx[port]);
        te_meas_stats_free(&tst_stats_tx[port]);
    }

    TEST_END;
}
/** @} */
