/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Traffic filtering
 */

/** @defgroup filters-flow_rule_counters Check RTE flow API rules implying COUNT actions
 * @ingroup filters
 * @{
 *
 * @objective Make sure that RTE flow API COUNT actions are
 *            carried out correctly
 *
 * @param flow_rule_pattern     Flow rule pattern
 * @param n_counters            Number of counters
 * @param field_path            ASN.1 path to a field in the pattern
 *                              to change to make additional patterns
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "filters/flow_rule_counters"

#include "dpdk_pmd_test.h"

/**
 * The default queue number to which mismatched packets are directed
 * in case of non-isolated RTE flow.
 */
#define TEST_DEF_QUEUE_NB 0

/** Maximum created number of flow rules */
#define TEST_FLOWS_MAX 2

/** Number of mismatched packets to send */
#define TEST_N_MISMATCH 1

int
main(int argc, char *argv[])
{
    rcf_rpc_server                         *iut_rpcs = NULL;
    tapi_env_host                          *tst_host;
    const struct if_nameindex              *iut_port = NULL;
    const struct if_nameindex              *tst_if = NULL;

    asn_value                              *flow_rule_pattern;
    /*
     * Flow rule patterns: the first is from the test parameters, the rest are
     * generated from the first by copying it and changing a field.
     * First @p n_counters elements become flow rules, after them,
     * @c TEST_N_MISMATCH are used to create mismatching packet templates.
     */
    asn_value                              *flow_rule_patterns[TEST_FLOWS_MAX + TEST_N_MISMATCH];
    asn_value                              *flow_rule_actions[TEST_FLOWS_MAX];
    asn_value                              *tmpl_match[TEST_FLOWS_MAX];
    uint32_t                                pkt_sizes[TEST_FLOWS_MAX];
    /* Total size of packets that hit counters */
    uint32_t                                count_pkts_size;
    asn_value                              *tmpls_mismatch[TEST_N_MISMATCH];
    asn_value                              *packet_patterns;
    rpc_rte_flow_attr_p                     attr = RPC_NULL;
    rpc_rte_flow_item_p                     rte_patterns[TEST_FLOWS_MAX] = {RPC_NULL};
    rpc_rte_flow_action_p                   rte_actions[TEST_FLOWS_MAX] = {RPC_NULL};
    rpc_rte_flow_action_p                   query_actions[TEST_FLOWS_MAX];
    rpc_rte_flow_p                          flows[TEST_FLOWS_MAX] = {RPC_NULL};
    tarpc_rte_flow_error                    error;
    const char                             *field_path;

    rpc_rte_mbuf_p                          mbufs[BURST_SIZE] = {};
    uint32_t                                match_fields[TEST_FLOWS_MAX];
    unsigned int                            n_counters;
    unsigned int                            i;
    unsigned int                            transmitted = 0;
    unsigned int                            received = 0;
    te_bool                                 promisc_enabled = FALSE;

    struct test_ethdev_config               ethdev_config;
    const struct sockaddr                  *iut_alien_mac = NULL;
    const struct sockaddr                  *tst_alien_mac = NULL;
    const struct sockaddr                  *tst_lladdr = NULL;
    const struct sockaddr                  *iut_addr = NULL;
    const struct sockaddr                  *tst_addr = NULL;
    const struct sockaddr                  *alien_addr = NULL;
    struct test_pkt_addresses               addrs;
    struct test_pkt_addresses               ifrm_addrs;

    tarpc_rte_flow_query_data               count_query;


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
    TEST_GET_ADDR_NO_PORT(alien_addr);
    TEST_GET_UINT_PARAM(n_counters);
    TEST_GET_STRING_PARAM(field_path);

    flow_rule_patterns[0] = flow_rule_pattern;
    memset(&count_query, 0, sizeof(count_query));

    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Make flow rule attributes with only one ingress attribute");
    CHECK_RC(test_mk_rte_flow_attr_ingress(iut_rpcs, &attr));

    TEST_STEP("Generate flow rule patterns that are different from the original");
    if (n_counters + TEST_N_MISMATCH > 1)
    {
        CHECK_RC(test_generate_changed_flow_patterns(flow_rule_pattern,
                                                     field_path, n_counters -
                                                     1 + TEST_N_MISMATCH,
                                                     &flow_rule_patterns[1]));
    }

    TEST_STEP("Create packet templates that are not matching any of created "
              "flow rule patterns");
    for (i = 0; i < TEST_N_MISMATCH; i++)
    {
        CHECK_RC(test_mk_template_get_match_fields_from_flow_ndn(
                 flow_rule_patterns[n_counters + i], &tmpls_mismatch[i], NULL));
    }

    packet_patterns = asn_init_value(ndn_traffic_pattern);
    CHECK_NOT_NULL(packet_patterns);

    TEST_STEP("Transmit packets with matching templates");
    for (i = 0; i < n_counters; i++)
    {
        asn_value *ptrn;

        CHECK_RC(tapi_ndn_subst_env(flow_rule_patterns[i], &test_params, &env));

        TEST_SUBSTEP("Create flow rule pattern and matching template");
        test_mk_pattern_and_tmpl_by_flow_rule_pattern(iut_rpcs, flow_rule_patterns[i],
                                                      &rte_patterns[i], &tmpl_match[i],
                                                      &match_fields[i]);

        flow_rule_actions[i] = asn_init_value(ndn_rte_flow_actions);
        CHECK_NOT_NULL(flow_rule_actions[i]);
        tapi_rte_flow_add_ndn_action_count(flow_rule_actions[i], 0, i);

        TEST_SUBSTEP("Make COUNT flow rule action to pass it to rte_flow_query");
        rpc_rte_mk_flow_rule_components(iut_rpcs, flow_rule_actions[i],
                                        NULL, NULL, &query_actions[i]);

        tapi_rte_flow_add_ndn_action_queue(flow_rule_actions[i], 1, TEST_DEF_QUEUE_NB);

        TEST_SUBSTEP("Make flow rule actions COUNT / QUEUE");
        rpc_rte_mk_flow_rule_components(iut_rpcs, flow_rule_actions[i],
                                        NULL, NULL, &rte_actions[i]);

        TEST_SUBSTEP("Validate and create the flow rule");
        flows[i] = tapi_rte_flow_validate_and_create_rule(iut_rpcs,
                    iut_port->if_index, attr, rte_patterns[i], rte_actions[i]);

        /* There is no support of encapsulated traffic */
        test_set_pkt_addresses(&ifrm_addrs, NULL, NULL, NULL, NULL);
        test_set_pkt_addresses(&addrs, (uint8_t *)tst_lladdr->sa_data,
                               (uint8_t *)iut_alien_mac->sa_data,
                               tst_addr, iut_addr);
        CHECK_RC(test_fill_in_tmpl_req_fields(tmpl_match[i], &addrs, &ifrm_addrs));

        TEST_STEP("Enable promiscuous mode to recieve packets with alien "
                  "MAC addresses");
        if (!promisc_enabled)
        {
            test_rte_eth_promiscuous_enable(iut_rpcs, iut_port->if_index,
                                            TEST_OP_REQUIRED);
            promisc_enabled = TRUE;
        }

        pkt_sizes[i] = test_get_template_packet_length(iut_rpcs, tmpl_match[i],
                                                       ethdev_config.mp);
        TEST_SUBSTEP("Transmit one packet with prepared match template");
        CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0, tst_if->if_name,
                                           tmpl_match[i], NULL, &ptrn));
        CHECK_RC(tapi_tad_concat_patterns(packet_patterns, ptrn));
        transmitted++;
    }

    TEST_STEP("Transmit packets with dissimilar templates");
    test_set_pkt_addresses(&addrs, (uint8_t *)tst_alien_mac->sa_data,
                           (uint8_t *)iut_alien_mac->sa_data,
                           alien_addr, alien_addr);
    for (i = 0; i < TEST_N_MISMATCH; i++)
    {
        asn_value *ptrn;

        CHECK_RC(test_fill_in_tmpl_req_fields(tmpls_mismatch[i], &addrs, &ifrm_addrs));
        CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0, tst_if->if_name,
                                           tmpls_mismatch[i], NULL, &ptrn));
        CHECK_RC(tapi_tad_concat_patterns(packet_patterns, ptrn));
        transmitted++;
    }

    TEST_STEP("Receive all packets");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index,
                                         TEST_DEF_QUEUE_NB, mbufs, BURST_SIZE,
                                         transmitted, packet_patterns, TRUE));
    received += transmitted;

    /* FIXME: get sleep time from configurator */
    SLEEP(1);

    for (count_pkts_size = 0, i = 0; i < n_counters; i++)
        count_pkts_size += pkt_sizes[i];

    TEST_STEP("Check counter values");
    for (i = 0; i < n_counters; i++)
    {
        unsigned int expected_bytes = pkt_sizes[i];
        unsigned int expected_hits = 1;

        rpc_rte_flow_query(iut_rpcs, iut_port->if_index, flows[i],
                           query_actions[i], &count_query, &error);
        test_check_flow_query_data(&count_query, TRUE, expected_hits, TRUE, expected_bytes);
    }


    TEST_SUCCESS;

cleanup:
    for (i = 0; i < received; i++)
    {
        rpc_rte_pktmbuf_free(iut_rpcs, mbufs[i]);
    }

    for (i = 0; i < n_counters; i++)
    {
        if (flows[i] != RPC_NULL)
            rpc_rte_flow_destroy(iut_rpcs, iut_port->if_index, flows[i], &error);

        rpc_rte_free_flow_rule(iut_rpcs, attr, rte_patterns[i], rte_actions[i]);
        attr = RPC_NULL;
    }

    TEST_END;
}

/** @} */
