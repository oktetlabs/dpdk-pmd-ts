/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Traffic filtering
 */

/** @defgroup filters-flow_rule_multi_count Multiple count actions in a flow rule
 * @ingroup filters
 * @{
 *
 * @objective Test multiple count actions in a flow rule
 *
 * @param ingress               Test flow rule ingress if @c TRUE, egress if
 *                              @c FALSE
 * @param promisc               Test with promiscuous mode enabled if @c TRUE
 * @param isolated              Test with isolated flow mode if @c TRUE
 * @param transfer              Set transfer attribute to flow rule if @c TRUE
 * @param flow_rule_pattern     Flow rule pattern
 * @param counter_ids           List of IDs of counters to create for the flow
 *                              rule
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * Test flow rule with multiple count actions.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "filters/flow_rule_multi_count"

#include "dpdk_pmd_test.h"

/**
 * Get expected number of hits for a counter taking all existing
 * counters into account.
 */
static unsigned int
get_number_of_hits(int counter_id, int n_counters, int *counter_ids)
{
    unsigned int result = 0;
    int i;

    for (i = 0; i < n_counters; i++)
    {
        if (counter_ids[i] == counter_id)
            result++;
    }

    if (result > 1)
        result = 1;

    return result;
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server                         *iut_rpcs = NULL;
    tapi_env_host                          *tst_host;
    const struct if_nameindex              *iut_port = NULL;
    const struct if_nameindex              *tst_if = NULL;

    asn_value                              *flow_rule_pattern;
    asn_value                              *tmpl_match;
    asn_value                             **tmpls_mismatch;
    asn_value                              *ndn_actions;
    unsigned int                            tmpls_mismatch_nb;
    rpc_rte_flow_attr_p                     attr = RPC_NULL;
    rpc_rte_flow_item_p                     pattern = RPC_NULL;
    rpc_rte_flow_action_p                   actions = RPC_NULL;
    rpc_rte_flow_action_p                  *count_actions;
    rpc_rte_flow_p                          flow = RPC_NULL;
    tarpc_rte_flow_error                    error;
    te_bool                                 transfer;
    te_bool                                 promisc;
    te_bool                                 ingress;
    te_bool                                 isolated;

    uint32_t                                match_fields;
    uint32_t                                match_fields_mask = ~0;
    uint32_t                                match_pkt_size;
    int                                    *counter_ids;
    int                                     n_counter_ids;
    int                                     i;
    unsigned int                            j;

    struct test_ethdev_config               ethdev_config;
    struct tarpc_ether_addr                 iut_mac;
    const struct sockaddr                  *iut_alien_mac = NULL;
    const struct sockaddr                  *tst_alien_mac = NULL;
    const struct sockaddr                  *tst_lladdr = NULL;
    const struct sockaddr                  *iut_addr = NULL;
    const struct sockaddr                  *tst_addr = NULL;
    struct test_pkt_addresses               addrs;
    struct test_pkt_addresses               ifrm_addrs;
    tarpc_rte_flow_query_data               count_query;
    struct test_transceiver                *trsc_net = NULL;
    struct test_transceiver                *trsc_dpdk = NULL;


    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);
    TEST_GET_NDN_RTE_FLOW_PATTERN(flow_rule_pattern);
    TEST_GET_LINK_ADDR(iut_alien_mac);
    TEST_GET_LINK_ADDR(tst_alien_mac);
    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_ADDR_NO_PORT(iut_addr);
    TEST_GET_ADDR_NO_PORT(tst_addr);
    TEST_GET_BOOL_PARAM(transfer);
    TEST_GET_BOOL_PARAM(promisc);
    TEST_GET_BOOL_PARAM(ingress);
    TEST_GET_BOOL_PARAM(isolated);
    TEST_GET_INT_LIST_PARAM(counter_ids, n_counter_ids);

    count_actions = tapi_calloc(n_counter_ids, sizeof(*count_actions));

    TEST_STEP("Initialize EAL");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &ethdev_config,
                                         TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Enter isolated mode if need be");
    if (isolated)
        tapi_rte_flow_isolate(iut_rpcs, iut_port->if_index, 1);

    TEST_STEP("Start the device");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Prepare IUT and TST sides for transmit or receive");
    trsc_net = test_transceiver_net_init(tst_host->ta, tst_if->if_name);
    trsc_dpdk = test_transceiver_dpdk_init(iut_rpcs, iut_port->if_index,
                                           ethdev_config.mp);

    rpc_rte_eth_macaddr_get(iut_rpcs, iut_port->if_index, &iut_mac);

    if (ingress && !promisc && test_pdus_dst_mac_is_set(flow_rule_pattern))
    {
        match_fields_mask &= ~TEST_TMPL_DST_MAC;
        test_pdus_set_eth_src_dst(flow_rule_pattern, "", NULL,
                                  iut_mac.addr_bytes);
    }

    TEST_STEP("Make flow rule attributes as specified in parameters");
    tapi_rte_flow_make_attr(iut_rpcs, 0, 0, !transfer && ingress,
                            !transfer && !ingress, transfer, &attr);

    TEST_STEP("Make flow rule pattern and prepare matching template");
    CHECK_RC(tapi_ndn_subst_env(flow_rule_pattern, &test_params, &env));
    test_mk_pattern_and_tmpl_by_flow_rule_pattern(iut_rpcs, flow_rule_pattern,
                                                  &pattern, &tmpl_match,
                                                  &match_fields);

    TEST_STEP("Prepare addresses for the matching and non-matching templates");
    /* The test does not support encapsulated traffic */
    test_set_pkt_addresses(&ifrm_addrs, NULL, NULL, NULL, NULL);
    test_set_pkt_addresses(&addrs,
                           ingress ? (const uint8_t *)tst_lladdr->sa_data :
                           (const uint8_t *)iut_alien_mac->sa_data,
                           (promisc || !ingress) ?
                           (const uint8_t *)tst_alien_mac->sa_data :
                           iut_mac.addr_bytes, tst_addr, iut_addr);

    CHECK_RC(test_fill_in_tmpl_req_fields(tmpl_match, &addrs, &ifrm_addrs));
    match_pkt_size = test_get_template_packet_length(iut_rpcs, tmpl_match,
                                                     ethdev_config.mp);

    TEST_STEP("Make flow rule actions \"count\" from test parameters");
    CHECK_NOT_NULL(ndn_actions = asn_init_value(ndn_rte_flow_actions));
    for (i = 0; i < n_counter_ids; i++)
    {
        test_add_and_mk_rte_flow_action_count(counter_ids[i], -1, iut_rpcs,
                                              ndn_actions, &count_actions[i]);
    }

    TEST_STEP("Make flow rule action \"drop\"");
    tapi_rte_flow_add_ndn_action_drop(ndn_actions, -1);
    rpc_rte_mk_flow_rule_components(iut_rpcs, ndn_actions, NULL, NULL,
                                    &actions);

    TEST_STEP("Validate and create the flow rule");
    flow = tapi_rte_flow_validate_and_create_rule(iut_rpcs, iut_port->if_index,
                                                  attr, pattern, actions);

    TEST_STEP("Enable promiscuous mode if need be");
    if (promisc)
        test_rte_eth_promiscuous_enable(iut_rpcs, iut_port->if_index,
                                        TEST_OP_REQUIRED);

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Transmit one packet with prepared match template. "
              "Check that the packet was dropped.");
    test_transciever_simple_exchange_commit(tmpl_match,
            ingress ? trsc_net : trsc_dpdk, 1, 0,
            ingress ? trsc_dpdk : trsc_net, 0, 0,
            NULL, "Matching packet received unexpectedly");

    TEST_STEP("Transmit and receive packets with dissimilar templates. "
              "Check received packets (if it is not ingress isolated).");
    CHECK_RC(test_mk_tmpls_with_change_one_field(tmpl_match,
                                                 match_fields & match_fields_mask,
                                                 &addrs, &ifrm_addrs,
                                                 &tmpls_mismatch,
                                                 &tmpls_mismatch_nb));

    for (j = 0; j < tmpls_mismatch_nb; j++)
    {
        test_transciever_simple_exchange_commit(tmpls_mismatch[j],
                 ingress ? trsc_net : trsc_dpdk, 1, 0,
                 ingress ? trsc_dpdk : trsc_net, (isolated && ingress) ? 0 : 1,
                 0, NULL, NULL);
    }

    TEST_STEP("Check the counters hits and bytes");
    /* FIXME: get sleep time from configurator */
    VSLEEP(1, "Wait for counters to update");
    for (i = 0; i < n_counter_ids; i++)
    {
        unsigned int hits = get_number_of_hits(counter_ids[i], n_counter_ids,
                                               counter_ids);

        memset(&count_query, 0, sizeof(count_query));
        rpc_rte_flow_query(iut_rpcs, iut_port->if_index, flow,
                           count_actions[i], &count_query, &error);
        test_check_flow_query_data(&count_query, TRUE, hits, TRUE,
                                   hits *match_pkt_size);
    }

    TEST_SUCCESS;

cleanup:
    if (flow != RPC_NULL)
        rpc_rte_flow_destroy(iut_rpcs, iut_port->if_index, flow, &error);

    for (i = 0; i < n_counter_ids; i++)
        rpc_rte_free_flow_rule(iut_rpcs, RPC_NULL, RPC_NULL, count_actions[i]);

    rpc_rte_free_flow_rule(iut_rpcs, attr, pattern, actions);

    TEST_END;
}
/** @} */
