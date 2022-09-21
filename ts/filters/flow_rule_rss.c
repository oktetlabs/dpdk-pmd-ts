/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Traffic filtering
 */

/** @defgroup filters-flow_rule_rss Check RTE flow API rules implying RSS actions
 * @ingroup filters
 * @{
 *
 * @objective Make sure that RTE flow API RSS action is carried out correctly
 *
 * @param flow_rule_rss A flow rule featuring RSS action
 * @param isolated      Isolated mode toggle
 * @param promiscuous   Promiscuous mode toggle
 *
 * @type conformance
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 *
 * The test affirms that RSS flow action has proper effect on ingress traffic
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "filters/flow_rule_rss"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                *iut_rpcs = NULL;
    rcf_rpc_server                *tst_rpcs = NULL;
    tapi_env_host                 *tst_host = NULL;
    const struct if_nameindex     *iut_port = NULL;
    const struct if_nameindex     *tst_if   = NULL;
    asn_value                     *flow_rule_rss = NULL;
    te_bool                        isolated;
    te_bool                        promiscuous;
    uint16_t                      *rss_queues = NULL;
    uint16_t                       nb_rss_queues;
    uint16_t                       nb_rx_queues;
    uint64_t                       rss_hf = 0;
    uint8_t                       *rss_key = NULL;
    uint8_t                       *rss_key_global = NULL;
    struct test_ethdev_config      ethdev_config;
    struct tarpc_rte_eth_conf      eth_conf;
    struct tarpc_rte_eth_rss_conf  rss_conf_global;
    rpc_rte_flow_attr_p            attr = RPC_NULL;
    rpc_rte_flow_item_p            pattern = RPC_NULL;
    rpc_rte_flow_action_p          actions = RPC_NULL;
    rpc_rte_flow_p                 flow = RPC_NULL;
    tarpc_rte_flow_error           error;
    asn_value                     *template = NULL;
    uint32_t                       match_flags = 0;
    uint32_t                       match_flags_mask = ~0;
    struct tarpc_ether_addr        iut_lladdr;
    const struct sockaddr         *iut_alien_mac = NULL;
    const struct sockaddr         *tst_alien_mac = NULL;
    const struct sockaddr         *tst_lladdr = NULL;
    const struct sockaddr         *iut_addr = NULL;
    const struct sockaddr         *tst_addr = NULL;
    const struct sockaddr         *alien_addr = NULL;
    unsigned int                   nb_packets;
    unsigned int                   nb_packets_expected;
    asn_value                     *traffic_pattern = NULL;
    rpc_rte_mbuf_p                *m = NULL;
    asn_value                     *template_secondary = NULL;
    asn_value                     *traffic_pattern_secondary = NULL;
    unsigned int                   i;
    struct test_pkt_addresses      addrs;
    struct test_pkt_addresses      ifrm_addrs;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_NDN_RTE_FLOW_RULE(flow_rule_rss);
    TEST_GET_BOOL_PARAM(isolated);
    TEST_GET_BOOL_PARAM(promiscuous);
    TEST_GET_LINK_ADDR(iut_alien_mac);
    TEST_GET_LINK_ADDR(tst_alien_mac);
    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_ADDR_NO_PORT(iut_addr);
    TEST_GET_ADDR_NO_PORT(tst_addr);
    TEST_GET_ADDR_NO_PORT(alien_addr);

    TEST_STEP("Determine Rx settings based on the flow rule");
    CHECK_RC(test_get_rx_info_by_rss_action(flow_rule_rss,
                                            &rss_queues, &nb_rss_queues,
                                            &nb_rx_queues, &rss_hf,
                                            &rss_key));

    TEST_STEP("Arrange a handful of Rx queues");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);
    ethdev_config.nb_rx_queue = nb_rx_queues;

    TEST_STEP("Prepare @c TEST_ETHDEV_INITIALIZED state");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Check maximum number of Rx queues");
    if (nb_rx_queues > ethdev_config.dev_info.max_rx_queues)
        TEST_SKIP("So many Rx queues are not supported");

    TEST_STEP("Enable isolated mode if need be");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    if (isolated)
        tapi_rte_flow_isolate(iut_rpcs, iut_port->if_index, 1);

    CHECK_RC(rc);

    TEST_STEP("Query global RSS key and RSS HF if need be");
    if (rss_key == NULL)
    {
        /* Temporary device operating cycle featuring global RSS */
        test_rpc_rte_eth_make_eth_conf(iut_rpcs, iut_port->if_index, &eth_conf);
        ethdev_config.eth_conf = &eth_conf;
        ethdev_config.eth_conf->rxmode.mq_mode = TARPC_ETH_MQ_RX_RSS;
        CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

        rss_key_global = TE_ALLOC(RPC_RSS_HASH_KEY_LEN_DEF);
        CHECK_NOT_NULL(rss_key_global);

        memset(&rss_conf_global, 0, sizeof(rss_conf_global));
        rss_conf_global.rss_key.rss_key_val = rss_key_global;
        rss_conf_global.rss_key.rss_key_len = RPC_RSS_HASH_KEY_LEN_DEF;
        rss_conf_global.rss_key_len = RPC_RSS_HASH_KEY_LEN_DEF;

        RPC_AWAIT_IUT_ERROR(iut_rpcs);
        rc = rpc_rte_eth_dev_rss_hash_conf_get(iut_rpcs, iut_port->if_index,
                                               &rss_conf_global);
        if (rc == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
            TEST_VERDICT("rte_eth_dev_rss_hash_conf_get() RPC is unavailable");

        CHECK_RC(rc);

        rss_key = rss_key_global;
        rss_hf = rss_conf_global.rss_hf;

        TEST_SUBSTEP("Prepare @c TEST_ETHDEV_CONFIGURED state");
        CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_CONFIGURED));

        /* The target configuration (no global RSS) clearing */
        ethdev_config.eth_conf->rxmode.mq_mode = TARPC_ETH_MQ_RX_NONE;
        ethdev_config.eth_conf->rx_adv_conf.rss_conf.rss_hf = 0;

        TEST_SUBSTEP("Set device state to dummy value to allow to reconfigure "
                     "device with the new MQ mode. This is acceptable because "
                     "device configure API may be called multiple times.");
        ethdev_config.cur_state = TEST_ETHDEV_INITIALIZED;
    }

    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Change destination MAC to NIC MAC if it is present in pattern "
              "and promiscuous is disabled");
    rpc_rte_eth_macaddr_get(iut_rpcs, iut_port->if_index, &iut_lladdr);
    if (!promiscuous &&
        test_pdus_dst_mac_is_set(asn_find_descendant(flow_rule_rss, NULL,
                                                     "pattern")))
    {
        match_flags_mask &= ~TEST_TMPL_DST_MAC;
        test_pdus_set_eth_src_dst(flow_rule_rss, "pattern", NULL,
                                  iut_lladdr.addr_bytes);
    }

    CHECK_RC(tapi_ndn_subst_env(flow_rule_rss, &test_params, &env));
    rpc_rte_mk_flow_rule_components(iut_rpcs, flow_rule_rss,
                                    &attr, &pattern, &actions);

    TEST_STEP("Validate and create @p flow_rule_rss");
    flow = tapi_rte_flow_validate_and_create_rule(iut_rpcs, iut_port->if_index,
                                                  attr, pattern, actions);

    TEST_STEP("Produce a template to generate traffic expected by the flow rule");
    CHECK_RC(test_mk_template_get_match_fields_from_flow_ndn(flow_rule_rss,
                                                             &template,
                                                             &match_flags));
    CHECK_NOT_NULL(template);

    test_set_pkt_addresses(&addrs, (uint8_t *)tst_lladdr->sa_data,
                           promiscuous ?
                           (const uint8_t *)iut_alien_mac->sa_data :
                           iut_lladdr.addr_bytes,
                           tst_addr, iut_addr);
    /* There is no support of encapsulated traffic */
    test_set_pkt_addresses(&ifrm_addrs, NULL, NULL, NULL, NULL);
    CHECK_RC(test_fill_in_tmpl_req_fields(template, &addrs, &ifrm_addrs));

    nb_packets = ethdev_config.dev_info.rx_desc_lim.nb_min >> 1;
    CHECK_RC(asn_write_int32(template, 1, "arg-sets.0.#simple-for.begin"));
    CHECK_RC(asn_write_int32(template, nb_packets,
                             "arg-sets.0.#simple-for.end"));

    TEST_STEP("Enable promiscuous mode if need be");
    if (promiscuous)
        test_rte_eth_promiscuous_enable(iut_rpcs, iut_port->if_index,
                                        TEST_OP_REQUIRED);

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Generate traffic and obtain an overall pattern");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                            tst_if->if_name, template, NULL, &traffic_pattern));
    CHECK_NOT_NULL(traffic_pattern);

    CHECK_NOT_NULL(m = TE_ALLOC((nb_packets << 1) * sizeof(*m)));

    TEST_STEP("Make sure that traffic hits the relevant queues");
    for (i = 0; i < nb_packets; ++i)
    {
        uint32_t packet_hash;
        int      reta_idx;
        uint16_t rx_queue;

        CHECK_RC(test_rss_get_hash_by_pattern_unit(rss_hf, rss_key,
                                                   traffic_pattern,
                                                   i, &packet_hash, NULL));

        reta_idx = packet_hash % ethdev_config.dev_info.reta_size;
        rx_queue = rss_queues[reta_idx % nb_rss_queues];


        CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index,
                                             rx_queue, &m[i], 1, 1,
                                             test_mk_pattern_by_unit(traffic_pattern, i),
                                             TRUE));
    }

    TEST_STEP("Produce a secondary template to generate irrelevant traffic");
    test_set_pkt_addresses(&addrs, (uint8_t *)tst_alien_mac->sa_data,
                           (uint8_t *)iut_alien_mac->sa_data,
                           alien_addr, alien_addr);
    template_secondary = test_tmpl_copy_with_changed_fields(template, match_flags &
                                                            match_flags_mask,
                                                            &addrs, &ifrm_addrs);
    CHECK_NOT_NULL(template_secondary);

    TEST_STEP("Generate irrelevant traffic and obtain an overall pattern");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                    tst_if->if_name, template_secondary, NULL,
                                    &traffic_pattern_secondary));

    TEST_STEP("Make sure that traffic now hits the default queue (if need be)");
    nb_packets_expected = (isolated) ? 0 : nb_packets;
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, 0,
                                         m + nb_packets, nb_packets,
                                         nb_packets_expected,
                                         traffic_pattern_secondary, TRUE));

    TEST_SUCCESS;

cleanup:
    if (flow != RPC_NULL)
        rpc_rte_flow_destroy(iut_rpcs, iut_port->if_index, flow, &error);

    rpc_rte_free_flow_rule(iut_rpcs, attr, pattern, actions);

    if (m != NULL)
    {
        for (i = 0; i < (nb_packets << 1); ++i)
        {
            if (m[i] != RPC_NULL)
                rpc_rte_pktmbuf_free(iut_rpcs, m[i]);
        }
    }

    TEST_END;
}
/** @} */
