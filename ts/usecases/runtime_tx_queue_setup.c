/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-runtime_tx_queue_setup Setup Tx queue when device is started
 * @ingroup usecases
 * @{
 *
 * @objective Setup Tx queue when device is started
 *
 * @type use case
 *
 * @param nb_txq                        Tx queue count
 * @param tmpl                          Traffic template
 * @param txq_runtime_setup_ids         Indices of queues that will be setup
 *                                      and started at run time
 * @param txq_deferred_start_ids        Indices of queues that will be started
 *                                      at run time
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * Make sure that it is possible to setup a Tx queue when the Ethernet device
 * is started
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/runtime_tx_queue_setup"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                         *iut_rpcs = NULL;
    tapi_env_host                          *tst_host;
    const struct if_nameindex              *iut_port = NULL;
    const struct if_nameindex              *tst_if = NULL;
    asn_value                              *tmpl = NULL;
    csap_handle_t                           rx_csap;

    struct test_ethdev_config               ec;
    struct tarpc_rte_eth_conf               eth_conf;
    struct tarpc_rte_eth_txconf             tx_conf;
    rpc_rte_mbuf_p                         *mbufs = NULL;

    unsigned int                            received = 0;
    unsigned int                            sent = 0;
    unsigned int                            nb_txq;
    te_bool                                *txq_runtime_setup;
    te_bool                                *txq_deferred_start;
    int                                     nb_txq_runtime_setup;
    int                                    *txq_runtime_setup_ids;
    int                                     nb_txq_deferred_start;
    int                                    *txq_deferred_start_ids;
    unsigned int                            no_match_pkts;
    unsigned int                            i;
    unsigned int                            queue = 0;
    unsigned int                            count;

    unsigned int nb_stuck_pkts;
    unsigned int nb_pkts_pri;
    unsigned int nb_pkts_sec;
    asn_value *ptrn_pri;
    asn_value *ptrn_sec;
    asn_value *tmpl_pri;
    asn_value *tmpl_sec;
    unsigned int total;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);
    TEST_GET_UINT_PARAM(nb_txq);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_INT_LIST_PARAM(txq_runtime_setup_ids, nb_txq_runtime_setup);
    TEST_GET_INT_LIST_PARAM(txq_deferred_start_ids, nb_txq_deferred_start);

    TEST_STEP("Check runtime Tx queue setup capability");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                            &ec, TEST_ETHDEV_INITIALIZED));

    if ((ec.dev_info.dev_capa &
         (1ULL << TARPC_RTE_ETH_DEV_CAPA_RUNTIME_TX_QUEUE_SETUP_BIT)) == 0)
    {
        TEST_SKIP("Runtime Tx queue setup is not supported by the device");
    }

    TEST_STEP("Check maximum number of Tx queues");
    if (nb_txq > ec.dev_info.max_tx_queues)
        TEST_SKIP("So many Tx queues are not supported");

    /* Initialize test parameters - deferred start and runtime setup */
    txq_runtime_setup = tapi_calloc(nb_txq, sizeof(*txq_runtime_setup));
    for (i = 0; i < (unsigned int)nb_txq_runtime_setup; i++)
    {
        if (txq_runtime_setup_ids[i] >= 0)
            txq_runtime_setup[txq_runtime_setup_ids[i]] = TRUE;
    }

    txq_deferred_start = tapi_calloc(nb_txq, sizeof(*txq_deferred_start));
    for (i = 0; i < (unsigned int)nb_txq_deferred_start; i++)
    {
        if (txq_deferred_start_ids[i] >= 0)
            txq_deferred_start[txq_deferred_start_ids[i]] = TRUE;
    }

    TEST_STEP("Configure the Ethernet device");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ec);

    ec.eth_conf = test_rpc_rte_eth_make_eth_conf(iut_rpcs, iut_port->if_index,
                                                 &eth_conf);

    ec.nb_tx_queue = nb_txq;

    ec.mp = test_rte_pktmbuf_rx_pool_create(iut_rpcs, iut_port->if_index,
                                            &ec.dev_info,
                                            TEST_PKTS_MEMPOOL_NAME,
                                            TEST_RTE_MEMPOOL_DEF_SIZE,
                                            TEST_RTE_MEMPOOL_DEF_CACHE,
                                            TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                            TEST_RTE_MEMPOOL_DEF_DATA_ROOM,
                                            ec.socket_id);

    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_RX_SETUP_DONE));

    memcpy(&tx_conf, &ec.dev_info.default_txconf, sizeof(tx_conf));
    tx_conf.tx_deferred_start = 1;

    ec.tx_confs = tapi_calloc(nb_txq, sizeof(*ec.tx_confs));

    TEST_STEP("Setup the queues that were not marked for runtime setup");
    for (i = 0; i < nb_txq; i++)
    {
        if (txq_deferred_start[i])
            ec.tx_confs[i] = &tx_conf;

        if (!txq_runtime_setup[i])
        {
            rpc_rte_eth_tx_queue_setup(ec.rpcs, ec.port_id, i,
                                       ec.dev_info.tx_desc_lim.nb_min,
                                       ec.socket_id, ec.tx_confs[i]);
        }
    }

    TEST_STEP("Start the device");
    RPC_AWAIT_IUT_ERROR(ec.rpcs);
    rc = rpc_rte_eth_dev_start(ec.rpcs, ec.port_id);
    if (rc != 0)
        TEST_VERDICT("rte_eth_dev_start() failed: %r", -rc);

    TEST_STEP("Stop and start the device again to make sure that the device can be "
              "stopped while some of its queues are not set up");
    tapi_rpc_rte_eth_dev_stop(ec.rpcs, ec.port_id);
    rpc_rte_eth_dev_start(ec.rpcs, ec.port_id);

    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));
    CHECK_RC(asn_write_int32(tmpl, 1, "arg-sets.0.#simple-for.begin"));
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));

    tmpl_pri = asn_copy_value(tmpl);

    for (nb_pkts_pri = 0, i = 0; i < nb_txq; ++i)
    {
            nb_pkts_pri +=
                (!txq_runtime_setup[i] && !txq_deferred_start[i]) ? 1 : 0;
    }

    CHECK_RC(asn_write_int32(tmpl_pri, nb_pkts_pri,
                             "arg-sets.0.#simple-for.end"));

    TEST_STEP("Prepare mbufs to send and pattern to match from @p tmpl_pri");
    tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, tmpl_pri, ec.mp,
                                     NULL, &mbufs, &count, &ptrn_pri);
    if (count != nb_pkts_pri)
    {
        TEST_VERDICT("Unexpected number of prepared mbufs: %d mbufs have "
                     "been produced, but should be %u", count, nb_pkts_pri);
    }

    TEST_STEP("Create Ethernet-based CSAP");
    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(tst_host->ta, 0,
                                                tst_if->if_name,
                                                TAD_ETH_RECV_DEF,
                                                tmpl, &rx_csap));

    TEST_STEP("TST: start listening to network");
    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, rx_csap, ptrn_pri,
                                   RECEIVE_TIMEOUT_DEF, 0,
                                   RCF_TRRECV_PACKETS | RCF_TRRECV_MISMATCH));

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Validate and transmit one packet from already started "
              "Tx queues on @p iut_port");
    for (total = 0, queue = 0; queue < nb_txq; queue++)
    {
        if (!txq_runtime_setup[queue] && !txq_deferred_start[queue])
        {
            sent = test_tx_prepare_and_burst(iut_rpcs, iut_port->if_index,
                                             queue, &mbufs[total++], 1);
            CHECK_PACKETS_NUM(sent, 1);
        }
    }

    TEST_STEP("Receive packets on @p iut_port "
              "Check that the port has received packets from every started Tx queue, "
              "and these packets match the packets sent from @p iut_port");
    CHECK_RC(test_rx_await_pkts(tst_host->ta, rx_csap, nb_pkts_pri, 0));
    CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL, &received));

    CHECK_RC(tapi_tad_csap_get_no_match_pkts(tst_host->ta, 0, rx_csap,
                                             &no_match_pkts));
    if (no_match_pkts != 0)
        TEST_VERDICT("%u not matching packets received", no_match_pkts);

    CHECK_PACKETS_NUM(received, nb_pkts_pri);

    TEST_STEP("Setup the queues marked for runtime setup");
    for (i = 0; i < nb_txq; i++)
    {
        if (txq_runtime_setup[i])
        {
            rpc_rte_eth_tx_queue_setup(ec.rpcs, ec.port_id, i,
                                       ec.dev_info.tx_desc_lim.nb_min,
                                       ec.socket_id, ec.tx_confs[i]);
        }

        TEST_SUBSTEP("Restart the device after setup of every queue to start all Tx "
                     "queues that were setup previously except for deferred start queues. "
                     "Also make sure that the current state of the "
                     "queues does not cause device start and stop failures");
        tapi_rpc_rte_eth_dev_stop(ec.rpcs, ec.port_id);
        rpc_rte_eth_dev_start(ec.rpcs, ec.port_id);
    }

    tmpl_sec = asn_copy_value(tmpl);

    for (nb_pkts_sec = nb_txq, i = 0; i < nb_txq; ++i)
            nb_pkts_sec += (txq_deferred_start[i]) ? 1 : 0;

    CHECK_RC(asn_write_int32(tmpl_sec, nb_pkts_sec,
                             "arg-sets.0.#simple-for.end"));

    TEST_STEP("Prepare mbufs to send and pattern to match from @p tmpl_sec");
    tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, tmpl_sec, ec.mp,
                                     NULL, &mbufs, &count, &ptrn_sec);
    if (count != nb_pkts_sec)
    {
        TEST_VERDICT("Unexpected number of prepared mbufs: %u mbufs have "
                     "been produced, but should be %u", count, nb_pkts_sec);
    }

    TEST_STEP("TST: start listening to network");
    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, rx_csap, ptrn_sec,
                                   RECEIVE_TIMEOUT_DEF, 0,
                                   RCF_TRRECV_PACKETS | RCF_TRRECV_MISMATCH));

    TEST_STEP("Validate and try to transmit the packet from @p iut_port from "
              "@p txq_deferred_start_ids Tx queues");
    for (nb_stuck_pkts = 0, total = 0, i = 0; i < nb_txq; i++)
    {
        if (txq_deferred_start[i])
        {
            sent = test_tx_prepare_and_burst(iut_rpcs, iut_port->if_index,
                                             i, &mbufs[total++], 1);
            if (sent > 1)
            {
                TEST_VERDICT("Inactive queue %u reported that it had sent %hu "
                             "packets despite only 1 being passed to it",
                             i, sent);
            }

            nb_stuck_pkts += sent;
        }
    }

    TEST_STEP("Check that @p iut_port port hasn't transmitted any packets");
    if (nb_stuck_pkts != 0 && nb_stuck_pkts == nb_pkts_sec - nb_txq)
    {
        WARN_VERDICT("The inactive queue(s) accepted the packet(s) for Tx");

        TEST_SUBSTEP("TST: check that no packets arrived");
        CHECK_RC(test_rx_await_pkts(tst_host->ta, rx_csap, nb_stuck_pkts, 0));
        CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL,
                                      &received));
        if (received != 0)
            TEST_VERDICT("The packet(s) really hit the wire");
    }
    else if (nb_stuck_pkts != 0)
    {
        TEST_VERDICT("The inactive queue(s) denied the packet(s) partially");
    }
    else
    {
        CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL, NULL));
    }

    TEST_STEP("Start deferred queues");
    for (i = 0; i < nb_txq; i++)
    {
        if (txq_deferred_start[i])
             test_start_tx_queue(ec.rpcs, ec.port_id, i);
    }

    TEST_STEP("TST: continue listening to network");
    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, rx_csap, ptrn_sec,
                                   RECEIVE_TIMEOUT_DEF, 0,
                                   RCF_TRRECV_PACKETS | RCF_TRRECV_MISMATCH));


    TEST_STEP("Validate and transmit one packet from every Tx queue on @p iut_port");
    for (queue = 0; queue < nb_txq; queue++)
    {
        sent = test_tx_prepare_and_burst(iut_rpcs, iut_port->if_index,
                                         queue, &mbufs[total + queue], 1);
        CHECK_PACKETS_NUM(sent, 1);
    }

    TEST_STEP("Receive packets on @p iut_port "
              "Check that the port has received packets from every Tx queue, and these "
              "packets match the packets sent from @p iut_port");
    CHECK_RC(test_rx_await_pkts(tst_host->ta, rx_csap, nb_pkts_sec, 0));
    CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL, &received));

    TEST_STEP("Check that no extra packets are received on Tester");
    CHECK_RC(tapi_tad_csap_get_no_match_pkts(tst_host->ta, 0, rx_csap,
                                             &no_match_pkts));
    if (no_match_pkts != 0)
        TEST_VERDICT("%u not matching packets received", no_match_pkts);

    if (nb_stuck_pkts != 0 && received == nb_stuck_pkts + nb_txq)
    {
        WARN_VERDICT("The packet(s) got stuck, then hit the wire "
                     "as a consequence of starting the queue(s)");
    }
    else
    {
        CHECK_PACKETS_NUM(received, nb_txq);
    }

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
