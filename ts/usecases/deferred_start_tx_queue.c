/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-deferred_start_tx_queue Deferred start TX queue test
 * @ingroup usecases
 * @{
 *
 * @objective Deferred start TX queue and checking that it works properly
 *
 * @param tmpl         Traffic template
 * @param n_rxq        The number of TX queues
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Make sure that if we use deferred start on randomly chosen queue,
 * then this queue actually will not receive packets until we do it manually
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/deferred_start_tx_queue"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                *iut_rpcs = NULL;
    rcf_rpc_server                *tst_rpcs = NULL;
    tapi_env_host                 *tst_host = NULL;
    const struct if_nameindex     *iut_port = NULL;
    const struct if_nameindex     *tst_if   = NULL;
    asn_value                     *tmpl     = NULL;
    asn_value                     *ptrn     = NULL;
    csap_handle_t                  rx_csap;

    unsigned int                   deferred_queue;
    struct test_ethdev_config      ethdev_config;
    struct tarpc_rte_eth_conf      eth_conf;
    struct tarpc_rte_eth_txq_info  tx_qinfo;
    struct tarpc_rte_eth_txconf    tx_conf;
    unsigned int                   n_txq;

    unsigned int                   no_match_pkts;
    unsigned int                   received;
    unsigned int                   count;
    rpc_rte_mbuf_p                *mbufs;
    uint16_t                       sent;
    te_bool                        tx_queue_info_get_supported;
    te_bool                        suspect_stuck_pkt = FALSE;
    te_bool                        fail = FALSE;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_UINT_PARAM(n_txq);

    TEST_STEP("Check maximum number of Tx queues");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                            &ethdev_config, TEST_ETHDEV_INITIALIZED));
    if (n_txq > ethdev_config.dev_info.max_tx_queues)
        TEST_SKIP("So many Tx queues are not supported");

    TEST_STEP("Prepare @p n_txq TX queues and set deffered flag randomly");
    ethdev_config.eth_conf = test_rpc_rte_eth_make_eth_conf(
                                      iut_rpcs, iut_port->if_index, &eth_conf);
    ethdev_config.nb_tx_queue = n_txq;

    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_CONFIGURED));

    deferred_queue = rand() % n_txq;
    memcpy(&tx_conf, &ethdev_config.dev_info.default_txconf, sizeof(tx_conf));
    tx_conf.tx_deferred_start = 1;

    ethdev_config.tx_confs = tapi_calloc(n_txq,
                                         sizeof(*ethdev_config.tx_confs));

    TEST_STEP("Probe the feature support on queue number 0");
    ethdev_config.tx_confs[0] = &tx_conf;
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_RXTX_SETUP_DONE));
    TEST_STEP("Setup Tx queues once again");
    ethdev_config.tx_confs[0] = NULL;
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_CONFIGURED));

    ethdev_config.tx_confs[deferred_queue] = &tx_conf;

    TEST_STEP("Start the Ethernet device and wait for link up");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Retrieve information about @p deferred_queue TX queue "
              "and make sure that the tx_deferred_start flag is set "
              "Ignore this check if tx_queue_info_get function is not supported");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_tx_queue_info_get(iut_rpcs, iut_port->if_index,
                                       deferred_queue, &tx_qinfo);
    switch (-rc)
    {
        case 0:
            tx_queue_info_get_supported = TRUE;
            if (tx_qinfo.conf.tx_deferred_start != 1)
            {
                TEST_VERDICT("Deferred start has not been applied for %u "
                             "Tx queue", deferred_queue);
            }
            break;
        case TE_RC(TE_RPC, TE_EOPNOTSUPP):
            tx_queue_info_get_supported = FALSE;
            break;
        default:
            TEST_VERDICT("Get Tx queue info operation failed: %s",
                         errno_rpc2str(-rc));
            break;
    }

    TEST_STEP("Prepare mbuf to be sent and pattern to match it by @p tmpl");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));
    CHECK_RC(asn_write_int32(tmpl, 1, "arg-sets.0.#simple-for.begin"));
    CHECK_RC(asn_write_int32(tmpl, 2, "arg-sets.0.#simple-for.end"));
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));

    tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, tmpl, ethdev_config.mp,
                                     NULL, &mbufs, &count, &ptrn);
    if (count != 2)
    {
        TEST_VERDICT("Unexpected number of prepared mbufs: %d mbufs have "
                     "been produced, but must be %d", count, 2);
    }

    TEST_STEP("TST: start listening to network");
    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(tst_host->ta, 0,
                                                tst_if->if_name,
                                                TAD_ETH_RECV_DEF,
                                                tmpl, &rx_csap));
    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, rx_csap, ptrn,
                                   RECEIVE_TIMEOUT_DEF, 0, RCF_TRRECV_PACKETS));

    TEST_STEP("Validate and try to transmit the packet from @p iut_port from "
              "@p deferred_queue TX queue");
    sent = test_tx_prepare_and_burst(iut_rpcs, iut_port->if_index,
                                     deferred_queue, mbufs, 1);

    TEST_STEP("Check that @p iut_port port hasn't transmitted any packets");
    if (sent == 1)
    {
        WARN_VERDICT("The inactive queue(s) accepted the packet(s) for Tx");

        TEST_SUBSTEP("TST: check that no packets arrived");
        CHECK_RC(test_rx_await_pkts(tst_host->ta, rx_csap, 1, 0));
        CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL,
                                      &received));
        if (received != 0)
            TEST_VERDICT("The packet(s) really hit the wire");

        suspect_stuck_pkt = TRUE;
    }
    else if (sent != 0)
    {
        TEST_VERDICT("Inactive queue %u reported that it had sent %hu packets "
                     "despite only 1 being passed to it", deferred_queue, sent);
    }
    else
    {
        CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL, NULL));
    }

    TEST_STEP("Start TX queue");
    test_start_tx_queue(iut_rpcs, iut_port->if_index, deferred_queue);

    TEST_STEP("TST: continue listening to network");
    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, rx_csap, ptrn,
                                   RECEIVE_TIMEOUT_DEF, 0,
                                   RCF_TRRECV_PACKETS | RCF_TRRECV_MISMATCH));

    TEST_STEP("Validate and transmit the packet from @p deferred_queue Tx queue "
              "on @p iut_port");
    sent = test_tx_prepare_and_burst(iut_rpcs, iut_port->if_index,
                                     deferred_queue, mbufs + sent, 1);

    TEST_STEP("Receive packet on @p iut_port "
              "Check that the port has received only one packet, and this packet "
              "match the packet sent one from @p iut_port");
    CHECK_RC(test_rx_await_pkts(tst_host->ta, rx_csap, 2, 0));
    CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL, &received));
    if (suspect_stuck_pkt && received == 2)
    {
        WARN_VERDICT("The packet(s) got stuck, then hit the wire "
                     "as a consequence of starting the queue(s)");
        fail = TRUE;
    }
    else
    {
        CHECK_PACKETS_NUM(received, 1);
    }

    TEST_STEP("Check that no extra packets are received on Tester");
    CHECK_RC(tapi_tad_csap_get_no_match_pkts(tst_host->ta, 0, rx_csap,
                                             &no_match_pkts));
    if (no_match_pkts != 0)
        TEST_VERDICT("%u not matching packets %s received", no_match_pkts,
                     (no_match_pkts > 1) ? "were" : "was");

    TEST_STEP("Stop @p deferred_queue TX queue");
    rpc_rte_eth_dev_tx_queue_stop(iut_rpcs, iut_port->if_index,
                                  deferred_queue);

    TEST_STEP("Retrieve information about @p deferred_queue TX queue "
              "and make sure that the tx_deferred_start flag is set "
              "after stopping of TX queue "
              "Ignore this check if tx_queue_info_get function is not supported");
    if (tx_queue_info_get_supported)
    {
        rpc_rte_eth_tx_queue_info_get(iut_rpcs, iut_port->if_index,
                                      deferred_queue, &tx_qinfo);
        if (tx_qinfo.conf.tx_deferred_start != 1)
            TEST_VERDICT("Deferred start has not been set for %u Tx queue",
                         deferred_queue);
    }

    if (fail)
        TEST_STOP;

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
