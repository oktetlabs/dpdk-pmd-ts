/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-dev_reconfigure Test to check device dev_reconfigure
 * @ingroup usecases
 * @{
 *
 * @objective Reconfigure the device in stopped state and check
 *            that RSS configuration are applied and all queues
 *            could transmit packets.
 *
 * @param rx_tmpl               Rx traffic template
 * @param tx_tmpl               Tx traffic template
 * @param nb_rx_queue           The number of Rx queues
 * @param nb_tx_queue           The number of Tx queues
 * @param nb_rx_queue_reconf    The number of Rx queues after
 *                              reconfiguring
 * @param nb_tx_queue_reconf    The number of Tx queues after
 *                              reconfiguring
 * @param all_queues_reconf     Re-setup all Rx and Tx queues if
 *                              @c TRUE, otherwise only new queues
 *
 * @type use case
 *
 * @author Roman Zhukov <Roman.Zhukov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/dev_reconfigure"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                         *iut_rpcs = NULL;
    rcf_rpc_server                         *tst_rpcs  = NULL;
    tapi_env_host                          *tst_host  = NULL;
    const struct if_nameindex              *iut_port = NULL;
    const struct sockaddr                  *tst_addr  = NULL;
    const struct if_nameindex              *tst_if    = NULL;

    struct test_ethdev_config               test_ethdev_config;
    uint16_t                                queue, queue_first, queue_last;
    uint16_t                                nb_rx_queue;
    uint16_t                                nb_tx_queue;
    uint16_t                                nb_rx_queue_reconf;
    uint16_t                                nb_tx_queue_reconf;
    te_bool                                 all_queues_reconf;

    const struct tarpc_rte_eth_rss_conf    *rss_conf;
    uint64_t                                reta_size;
    struct tarpc_rte_eth_rss_reta_entry64  *reta_conf;
    tarpc_rss_hash_protos_t                 hash_functions;

    asn_value                              *rx_tmpl;
    asn_value                              *tx_tmpl;
    asn_value                             **tmpls;
    asn_value                             **ptrns;
    asn_value                              *tx_ptrn;
    rpc_rte_mbuf_p                          rx_mbufs[BURST_SIZE] = {};
    rpc_rte_mbuf_p                        **tx_mbufs;
    csap_handle_t                           csap;
    uint16_t                                sent;
    unsigned int                            count;
    unsigned int                            no_match_pkts;
    unsigned int                            i;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);
    TEST_GET_UINT_PARAM(nb_rx_queue);
    TEST_GET_UINT_PARAM(nb_tx_queue);
    TEST_GET_UINT_PARAM(nb_rx_queue_reconf);
    TEST_GET_UINT_PARAM(nb_tx_queue_reconf);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(rx_tmpl);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tx_tmpl);
    TEST_GET_BOOL_PARAM(all_queues_reconf);

    TEST_STEP("Check maximum number of Rx/Tx queues");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                            &test_ethdev_config, TEST_ETHDEV_INITIALIZED));
    if (MAX(nb_rx_queue, nb_rx_queue_reconf) >
                    test_ethdev_config.dev_info.max_rx_queues)
        TEST_SKIP("So many Rx queues are not supported");

    if (MAX(nb_tx_queue, nb_tx_queue_reconf) >
                    test_ethdev_config.dev_info.max_tx_queues)
        TEST_SKIP("So many Tx queues are not supported");

    TEST_STEP("Prepare stopped Ethernet device using @p nb_rx_queue Rx queues "
              "and @p nb_tx_queue Tx queues");
    test_ethdev_config.nb_rx_queue = nb_rx_queue;
    test_ethdev_config.nb_tx_queue = nb_tx_queue;

    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_CONFIGURED));

    test_ethdev_config.mp =
        test_rte_pktmbuf_rx_pool_create(iut_rpcs, iut_port->if_index,
                                        &test_ethdev_config.dev_info,
                                        TEST_PKTS_MEMPOOL_NAME,
                                        TEST_RTE_MEMPOOL_DEF_SIZE,
                                        TEST_RTE_MEMPOOL_DEF_CACHE,
                                        TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                        TEST_RTE_MEMPOOL_DEF_DATA_ROOM,
                                        test_ethdev_config.socket_id);

    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_STOPPED));
    /*
     * Below code needs to reconfigure the ethdev without releasing
     * the queues that were set up above. Doing so requires that
     * the formal state of the ethdev be adjusted like this.
     */
    test_ethdev_config.cur_state = TEST_ETHDEV_INITIALIZED;

    TEST_STEP("Reconfigure the device using @p nb_rx_queue_reconf Rx queues, "
              "@p nb_tx_queue_reconf Tx queues and RSS configuration");
    CHECK_RC(test_get_rss_hf_by_tmpl(rx_tmpl, &hash_functions));
    hash_functions &= test_ethdev_config.dev_info.flow_type_rss_offloads;
    test_rx_mq_rss_prepare(&test_ethdev_config, hash_functions);

    test_ethdev_config.nb_rx_queue = nb_rx_queue_reconf;
    test_ethdev_config.nb_tx_queue = nb_tx_queue_reconf;

    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_CONFIGURED));

    TEST_STEP("Setup all Rx and Tx queues if @p all_queues_reconf is @c TRUE, "
              "otherwise setup only those queues that were not setup before");
    queue_first = all_queues_reconf ? 0 : nb_rx_queue;
    queue_last = nb_rx_queue_reconf;

    for (queue = queue_first; queue < queue_last; queue++)
    {
        rpc_rte_eth_rx_queue_setup(test_ethdev_config.rpcs,
                                   test_ethdev_config.port_id, queue,
                                   test_ethdev_config.dev_info.rx_desc_lim.nb_min,
                                   test_ethdev_config.socket_id,
                                   (test_ethdev_config.rx_confs == NULL) ?
                                   NULL : test_ethdev_config.rx_confs[queue],
                                   test_ethdev_config.mp);
    }


    queue_first = all_queues_reconf ? 0 : nb_tx_queue;
    queue_last = nb_tx_queue_reconf;

    for (queue = queue_first; queue < queue_last; queue++)
    {
        rpc_rte_eth_tx_queue_setup(test_ethdev_config.rpcs,
                                   test_ethdev_config.port_id, queue,
                                   test_ethdev_config.dev_info.tx_desc_lim.nb_min,
                                   test_ethdev_config.socket_id,
                                   (test_ethdev_config.tx_confs == NULL) ?
                                   NULL : test_ethdev_config.tx_confs[queue]);
    }

    TEST_STEP("Start the Ethernet device");
    rpc_rte_eth_dev_start(test_ethdev_config.rpcs,
                          test_ethdev_config.port_id);

    test_await_link_up(test_ethdev_config.rpcs,
                       test_ethdev_config.port_id);

    test_ethdev_config.cur_state = TEST_ETHDEV_STARTED;

    TEST_STEP("Prepare templates for test using @p rx_tmpl and @p tx_tmpl");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));

    CHECK_RC(tapi_ndn_subst_env(rx_tmpl, &test_params, &env));
    CHECK_RC(tapi_ndn_subst_env(tx_tmpl, &test_params, &env));

    ptrns = tapi_calloc((nb_rx_queue_reconf > nb_tx_queue_reconf) ?
                        nb_rx_queue_reconf : nb_tx_queue_reconf, sizeof(*ptrns));
    tmpls = tapi_calloc(nb_rx_queue_reconf, sizeof(rx_tmpl));

    TEST_STEP("Establish effective RSS hash configuration");
    rss_conf = test_rx_mq_rss_establish(&test_ethdev_config, FALSE);

    TEST_STEP("Get RSS Redirection Table. If the corresponding RPC is not supported, "
              "use default Redirection Table");
    test_get_rss_reta(iut_rpcs, iut_port->if_index, &reta_size, &reta_conf);

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Change templates to be sure that packets will be received "
              "on specific queues using Redirection Table and RSS hash configuration");
    for (i = 0; i < nb_rx_queue_reconf; i++)
    {
        tmpls[i] = asn_copy_value(rx_tmpl);
        CHECK_NOT_NULL(tmpls[i]);

        CHECK_RC(test_change_tmpl_ip_src_addr_by_queue_nb(tmpls[i], i,
                                                          reta_size,
                                                          reta_conf,
                                                          rss_conf));

        TEST_SUBSTEP("Transmit and sniff packet from @p tst_if");
        CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                tst_if->if_name, tmpls[i], NULL, &ptrns[i]));
    }

    TEST_STEP("Receive packets on port @p iut_port on all Rx queues and check "
              "that received packets are on corresponding queues");
    for (i = 0; i < nb_rx_queue_reconf; i++)
    {
        CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, i,
                                             rx_mbufs, TE_ARRAY_LEN(rx_mbufs),
                                             1, ptrns[i], TRUE));

        rpc_rte_pktmbuf_free(iut_rpcs, rx_mbufs[0]);
        rx_mbufs[0] = RPC_NULL;
    }

    TEST_STEP("Prepare tx_mbufs to be sent and patterns to match it by Tx template");
    tx_mbufs = tapi_calloc(nb_tx_queue_reconf, sizeof(*tx_mbufs));

    for (i = 0; i < nb_tx_queue_reconf; i++)
    {
        tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, tx_tmpl, test_ethdev_config.mp,
                                         NULL, &tx_mbufs[i], &count, &ptrns[i]);
        if (count != 1)
            TEST_VERDICT("Unexpected number of prepared mbufs: %d mbufs have "
                         "been produced, but should be 1", count);
    }

    CHECK_RC(tapi_tad_aggregate_patterns(ptrns, nb_tx_queue_reconf, &tx_ptrn));

    TEST_STEP("Create Ethernet-based CSAP which starts to listen to network "
              "immediately");
    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(tst_host->ta, 0,
                                                tst_if->if_name,
                                                TAD_ETH_RECV_DEF,
                                                tx_tmpl, &csap));

    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, csap, tx_ptrn,
                                   RECEIVE_TIMEOUT_DEF, 0,
                                   RCF_TRRECV_PACKETS | RCF_TRRECV_MISMATCH));

    TEST_STEP("Validate and send tx_mbufs from all queues to @p iut_port and "
              "make sure that all are sent");
    for (i = 0; i < nb_tx_queue_reconf; i++)
    {
        sent = test_tx_prepare_and_burst(iut_rpcs, iut_port->if_index, i,
                                         tx_mbufs[i], 1);
        if (sent != 1)
            TEST_VERDICT("Cannot send packet");
    }

    TEST_STEP("Receive packets on @p tst_if and "
              "check that the received packet matches the sent from @p iut_port");
    CHECK_RC(test_rx_await_pkts(tst_host->ta, csap, nb_tx_queue_reconf, 0));

    CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, csap, NULL, &count));

    TEST_STEP("Check that no extra packets are received on Tester");
    CHECK_RC(tapi_tad_csap_get_no_match_pkts(tst_host->ta, 0, csap,
                                             &no_match_pkts));
    if (no_match_pkts != 0)
        TEST_VERDICT("%u not matching packets %s received", no_match_pkts,
                     (no_match_pkts > 1) ? "were" : "was");

    TEST_STEP("Check that number of received packets equal to sent");
    CHECK_PACKETS_NUM(count, nb_tx_queue_reconf);

    TEST_SUCCESS;

cleanup:
    rpc_rte_pktmbuf_free_array(iut_rpcs, rx_mbufs, TE_ARRAY_LEN(rx_mbufs));

    TEST_END;
}
/* @} */
