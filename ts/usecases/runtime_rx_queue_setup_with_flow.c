/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-runtime_rx_queue_setup_with_flow Setup Rx queue at run time
 * @ingroup usecases
 * @{
 *
 * @objective Setup Rx queue when device is started, perform the check
 *            using flow API
 *
 * @param isolated                  Isolated mode toggle
 * @param flow_rule_pattern         Flow rule
 * @param nb_rxq                    Rx queue count
 * @param rxq_runtime_setup_ids     Indices of queues that will be setup and
 *                                  started at run time
 * @param deferred_start            Should the queues that setup at run time
 *                                  be marked as deferred
 *
 * @type use case
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * Make sure that it is possible to setup a Rx queue when the Ethernet device
 * is started. Perform the check using flow API rules.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/runtime_rx_queue_setup_with_flow"

/**
 * The default queue number
 */
#define TEST_DEF_QUEUE_NB 0

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                         *iut_rpcs = NULL;
    tapi_env_host                          *tst_host;
    const struct if_nameindex              *iut_port = NULL;
    const struct if_nameindex              *tst_if = NULL;
    struct tarpc_rte_eth_conf               eth_conf;
    struct tarpc_rte_eth_dev_info           dev_info;

    struct tarpc_rte_eth_rxconf             rx_conf;
    asn_value                              *flow_rule_pattern = RPC_NULL;
    asn_value                              *flow_rule_pattern_copy = RPC_NULL;
    asn_value                              *tmpl_match;
    asn_value                             **tmpls_mismatch;
    unsigned int                            tmpls_mismatch_nb;
    rpc_rte_flow_attr_p                     attr = RPC_NULL;
    rpc_rte_flow_attr_p                     pattern = RPC_NULL;
    rpc_rte_flow_action_p                   actions = RPC_NULL;
    rpc_rte_flow_p                          flow = RPC_NULL;
    tarpc_rte_flow_error                    error;

    rpc_rte_mbuf_p                          mbufs[BURST_SIZE] = {};
    uint32_t                                match_fields;
    unsigned int                            nb_rxq;
    te_bool                                *rxq_runtime_setup;
    te_bool                                 deferred_start;
    int                                     nb_rxq_runtime_setup;
    int                                    *rxq_runtime_setup_ids;
    unsigned int                            nb_rxq_started = 0;
    int                                    *rxq_started;
    unsigned int                            i;
    unsigned int                            j;
    unsigned int                            k;
    unsigned int                            queue;
    unsigned int                            new_queue;
    te_bool                                 isolated;
    te_bool                                 def_queue_started = FALSE;

    struct test_ethdev_config               ec;
    const struct sockaddr                  *tst_alien_mac = NULL;
    const struct sockaddr                  *tst_lladdr = NULL;
    struct tarpc_ether_addr                 iut_mac;
    const struct sockaddr                  *iut_addr = NULL;
    const struct sockaddr                  *tst_addr = NULL;
    const struct sockaddr                  *alien_addr = NULL;
    struct test_pkt_addresses               addrs;
    struct test_pkt_addresses               ifrm_addrs;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);
    TEST_GET_BOOL_PARAM(isolated);
    TEST_GET_BOOL_PARAM(deferred_start);
    TEST_GET_NDN_RTE_FLOW_PATTERN(flow_rule_pattern);
    TEST_GET_LINK_ADDR(tst_alien_mac);
    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_ADDR_NO_PORT(iut_addr);
    TEST_GET_ADDR_NO_PORT(tst_addr);
    TEST_GET_ADDR_NO_PORT(alien_addr);
    TEST_GET_UINT_PARAM(nb_rxq);
    TEST_GET_INT_LIST_PARAM(rxq_runtime_setup_ids, nb_rxq_runtime_setup);

    /* Prepare test parameters */
    rxq_started = tapi_calloc(nb_rxq, sizeof(*rxq_started));
    rxq_runtime_setup = tapi_calloc(nb_rxq, sizeof(*rxq_runtime_setup));
    for (i = 0; i < (unsigned int)nb_rxq_runtime_setup; i++)
        rxq_runtime_setup[rxq_runtime_setup_ids[i]] = TRUE;

    CHECK_RC(tapi_ndn_subst_env(flow_rule_pattern, &test_params, &env));

    TEST_STEP("Configure the Ethernet device");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ec);

    ec.eth_conf = test_rpc_rte_eth_make_eth_conf(iut_rpcs, iut_port->if_index,
                                                 &eth_conf);
    ec.nb_rx_queue = nb_rxq;

    ec.mp = test_rte_pktmbuf_rx_pool_create(iut_rpcs, iut_port->if_index,
                                            &ec.dev_info,
                                            TEST_PKTS_MEMPOOL_NAME,
                                            TEST_RTE_MEMPOOL_DEF_SIZE,
                                            TEST_RTE_MEMPOOL_DEF_CACHE,
                                            TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                            TEST_RTE_MEMPOOL_DEF_DATA_ROOM,
                                            ec.socket_id);

    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Change destination MAC to NIC MAC if it is present in pattern");
    rpc_rte_eth_macaddr_get(iut_rpcs, iut_port->if_index, &iut_mac);
    if (test_pdus_dst_mac_is_set(flow_rule_pattern))
    {
        test_pdus_set_eth_src_dst(flow_rule_pattern, "", NULL,
                                  iut_mac.addr_bytes);
    }

    TEST_STEP("Check for Rx queue runtime setup capability");
    memset(&dev_info, 0, sizeof(dev_info));
    rpc_rte_eth_dev_info_get(iut_rpcs, iut_port->if_index, &dev_info);

    if (!(dev_info.dev_capa &
          (1ULL << TARPC_RTE_ETH_DEV_CAPA_RUNTIME_RX_QUEUE_SETUP_BIT)))
    {
        TEST_SKIP("Runtime Rx queue setup is not supported by the device");
    }

    TEST_STEP("Setup isolation mode if requested by @p isolated");
    if (isolated)
        tapi_rte_flow_isolate(iut_rpcs, iut_port->if_index, 1);

    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_CONFIGURED));

    TEST_STEP("Setup the queues that are not in @p rxq_runtime_setup_ids");
    for (i = 0; i < nb_rxq; i++)
    {
        if (!rxq_runtime_setup[i])
        {
            rpc_rte_eth_rx_queue_setup(ec.rpcs, ec.port_id, i,
                                       ec.dev_info.rx_desc_lim.nb_min,
                                       ec.socket_id, NULL, ec.mp);

            /*
             * Add the index of the queue to started queues because the queue
             * will be started immidiately after device start
             */
            rxq_started[nb_rxq_started++] = i;

            if (i == TEST_DEF_QUEUE_NB)
                def_queue_started = TRUE;
        }
    }

    TEST_STEP("Start the device");
    rpc_rte_eth_dev_start(ec.rpcs, ec.port_id);

    TEST_STEP("Stop and start the device again to make sure that the device can be "
              "stopped while some of its queues are not set up");
    rpc_rte_eth_dev_stop(ec.rpcs, ec.port_id);
    rpc_rte_eth_dev_start(ec.rpcs, ec.port_id);

    TEST_STEP("Make flow rule attributes with only one \"ingress\" attribute");
    CHECK_RC(test_mk_rte_flow_attr_ingress(iut_rpcs, &attr));

    flow_rule_pattern_copy = asn_copy_value(flow_rule_pattern);
    TEST_STEP("Make flow rule pattern by @p flow_rule_pattern");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_mk_flow_rule_components(iut_rpcs, flow_rule_pattern,
                                         NULL, &pattern, NULL);
    if (rc == -TE_RC(TE_RPCS, TE_EPROTONOSUPPORT))
        TEST_SKIP("The protocol used in the flow rule is not supported");
    if (rc != 0)
        TEST_VERDICT("Failed to make a flow rule pattern");

    TEST_STEP("Prepare template that matches the filter");
    CHECK_RC(test_mk_template_get_match_fields_from_flow_ndn(
                flow_rule_pattern_copy, &tmpl_match, &match_fields));
    /*
     * Do not create template with mismatching destination MAC, because
     * promiscuous mode is not enabled - only NIC's destination MAC should
     * be used.
     */
    match_fields &= ~TEST_TMPL_DST_MAC;

    /* There is no support for encapsulated traffic */
    test_set_pkt_addresses(&ifrm_addrs, NULL, NULL, NULL, NULL);
    test_set_pkt_addresses(&addrs, (uint8_t *)tst_lladdr->sa_data,
                           iut_mac.addr_bytes, tst_addr, iut_addr);
    CHECK_RC(test_fill_in_tmpl_req_fields(tmpl_match, &addrs, &ifrm_addrs));

    TEST_STEP("Prepare template that does not match the filter");
    test_set_pkt_addresses(&addrs, (uint8_t *)tst_alien_mac->sa_data,
                           NULL, alien_addr, alien_addr);
    CHECK_RC(test_mk_tmpls_with_change_one_field(tmpl_match, match_fields,
                                                 &addrs, &ifrm_addrs,
                                                 &tmpls_mismatch,
                                                 &tmpls_mismatch_nb));

    TEST_STEP("Repeat following steps for all @p rxq_runtime_setup_ids");
    memcpy(&rx_conf, &ec.dev_info.default_rxconf, sizeof(rx_conf));
    rx_conf.rx_deferred_start = 1;

    i = 0;
    while (1)
    {
        TEST_STEP("Check that all previously started queues are able to receive "
                  "traffic");
        for (j = 0; j < nb_rxq_started; j++)
        {
            queue = rxq_started[j];

            CHECK_RC(test_mk_rte_flow_action_queue(iut_rpcs, queue, &actions));

            TEST_SUBSTEP("Validate and create the flow rule that redirects packets that "
                         "match flow rule pattern to the target queue");
            flow = tapi_rte_flow_validate_and_create_rule(iut_rpcs,
                        iut_port->if_index, attr, pattern, actions);

            TEST_SUBSTEP("Transmit one packet with prepared match template. "
                         "Check that the packet was recieved on target queue");
            test_send_and_match_one_packet(iut_rpcs, iut_port->if_index,
                                           tst_host->ta, tst_if->if_name,
                                           mbufs, tmpl_match, queue, 1u);

            TEST_SUBSTEP("Transmit one packet with mismatching template. "
                         "Check that the packet was recieved on default queue. "
                         "In case of isolated flow - check that the packet appeared "
                         "on neither target nor default queue");
            for (k = 0; k < tmpls_mismatch_nb; k++)
            {
                if (def_queue_started)
                {
                    test_send_and_match_one_packet(iut_rpcs, iut_port->if_index,
                                                   tst_host->ta,
                                                   tst_if->if_name, mbufs,
                                                   tmpls_mismatch[k],
                                                   TEST_DEF_QUEUE_NB,
                                                   isolated ? 0u : 1u);
                }

                if (isolated)
                {
                    test_send_and_match_one_packet(iut_rpcs, iut_port->if_index,
                                                   tst_host->ta,
                                                   tst_if->if_name, mbufs,
                                                   tmpls_mismatch[k],
                                                   queue, 0u);
                }
            }

            TEST_STEP("Destroy the flow rule");
            rpc_rte_flow_destroy(iut_rpcs, iut_port->if_index, flow, &error);
            flow = RPC_NULL;
            rpc_rte_free_flow_rule(iut_rpcs, RPC_NULL, RPC_NULL, actions);
            actions = RPC_NULL;
        }

        if (i >= (unsigned int)nb_rxq_runtime_setup)
            break;

        TEST_STEP("Setup a queue, add deferred start flag if "
                  "requested by @p deferred_start");
        new_queue = rxq_runtime_setup_ids[i++];

        rpc_rte_eth_rx_queue_setup(ec.rpcs, ec.port_id, new_queue,
                                   ec.dev_info.rx_desc_lim.nb_min,
                                   ec.socket_id,
                                   deferred_start ? &rx_conf : NULL, ec.mp);

        TEST_STEP("Restart the device to start all Rx queues that were setup previously "
                  "except deferred start queues. Also make sure that the current state "
                  "of the queues does not cause device start and stop failures");
        rpc_rte_eth_dev_stop(ec.rpcs, ec.port_id);
        rpc_rte_eth_dev_start(ec.rpcs, ec.port_id);

        TEST_STEP("Start the queue if it is deferred (@p deferred_start)");
        if (deferred_start)
            test_start_rx_queue(ec.rpcs, ec.port_id, new_queue);

        if (new_queue == TEST_DEF_QUEUE_NB)
            def_queue_started = TRUE;

        rxq_started[nb_rxq_started++] = new_queue;
    }

    TEST_SUCCESS;

cleanup:

    if (flow != RPC_NULL)
        rpc_rte_flow_destroy(iut_rpcs, iut_port->if_index, flow, &error);
    if (attr != RPC_NULL)
        rpc_rte_free_flow_rule(iut_rpcs, attr, RPC_NULL, RPC_NULL);
    if (pattern != RPC_NULL)
        rpc_rte_free_flow_rule(iut_rpcs, RPC_NULL, pattern, RPC_NULL);
    if (actions != RPC_NULL)
        rpc_rte_free_flow_rule(iut_rpcs, RPC_NULL, RPC_NULL, actions);

    TEST_END;
}
/** @} */
