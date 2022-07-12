/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup filters-flow_rule_vlan Test RTE flow API VLAN ID matching
 * @ingroup filters
 * @{
 *
 * @objective Make sure that RTE flow API VLAN ID matching
 *            is carried out correctly
 *
 * @param flow_rule_pattern     Flow rule pattern that must have VLAN ID match
 * @param promisc               Test with promiscuous mode enabled if @c TRUE
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "filters/flow_rule_vlan"

#include "dpdk_pmd_test.h"

/**
 * The maximum value of TCI's PCP and DEI part
 */
#define TEST_NON_VID_TCI_PART_MAX 15

/**
 * The default queue number
 */
#define TEST_DEFAULT_QUEUE 0

#define TEST_FIELD_NAME_MAX_LEN 128

static asn_value *
get_eth_tagged(const asn_value *tmpl)
{
    asn_value *pdus;
    asn_value *choice;
    asn_value *eth;
    asn_value *tagged;
    te_errno rc;

    pdus = asn_find_descendant(tmpl, &rc, "pdus");
    if (pdus == NULL)
        choice = asn_find_descendant(tmpl, &rc, "");
    else
        choice = asn_find_descendant(pdus, &rc, "");

    eth = asn_find_child_choice_value(choice, TE_PROTO_ETH);
    CHECK_NOT_NULL(eth);

    tagged = asn_find_descendant(eth, &rc, "tagged.#tagged");
    CHECK_NOT_NULL(tagged);

    return tagged;
}

static void
write_non_vid(asn_value *tagged, uint8_t value, const char *field)
{
    char buf[TEST_FIELD_NAME_MAX_LEN];

    uint32_t cfi;
    uint32_t priority;

    value %= (TEST_NON_VID_TCI_PART_MAX + 1); /* Truncate to fit */
    cfi = value & 1;                          /* The least significant bit */
    priority = value >> 1;                    /* Remaining bits except CFI */

    snprintf(buf, sizeof(buf), "cfi.%s", field);
    CHECK_RC(asn_write_uint32(tagged, cfi, buf));

    snprintf(buf, sizeof(buf), "priority.%s", field);
    CHECK_RC(asn_write_uint32(tagged, priority, buf));
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server                         *iut_rpcs = NULL;
    tapi_env_host                          *tst_host;
    const struct if_nameindex              *iut_port = NULL;
    const struct if_nameindex              *tst_if = NULL;

    asn_value                              *flow_rule_pattern;
    asn_value                              *flow_rule_pattern_copy;
    asn_value                              *flow_rule_pattern_non_vid;
    asn_value                              *tmpl_match;
    asn_value                              *tmpl_mismatch;
    asn_value                              *tmpl_match_non_vid;
    asn_value                              *tmpl_mismatch_non_vid;

    rpc_rte_flow_attr_p                     attr = RPC_NULL;
    rpc_rte_flow_item_p                     pattern = RPC_NULL;
    rpc_rte_flow_item_p                     pattern_non_vid = RPC_NULL;
    rpc_rte_flow_action_p                   actions = RPC_NULL;
    rpc_rte_flow_p                          flow = RPC_NULL;
    tarpc_rte_flow_error                    error;

    rpc_rte_mbuf_p                          mbufs[BURST_SIZE] = {};
    uint32_t                                match_fields;
    te_bool                                 second_flow;
    uint32_t                                vid_value;
    uint8_t                                 non_vid_value;
    te_bool                                 promisc;

    struct test_ethdev_config               ethdev_config;
    const struct sockaddr                  *iut_alien_mac = NULL;
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
    TEST_GET_NDN_RTE_FLOW_PATTERN(flow_rule_pattern);
    TEST_GET_LINK_ADDR(iut_alien_mac);
    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_ADDR_NO_PORT(iut_addr);
    TEST_GET_ADDR_NO_PORT(tst_addr);
    TEST_GET_ADDR_NO_PORT(alien_addr);
    TEST_GET_BOOL_PARAM(promisc);


    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Substisute environment bindings in @p flow_rule_pattern");
    CHECK_RC(tapi_ndn_subst_env(flow_rule_pattern, &test_params, &env));

    TEST_STEP("Set NIC MAC address in pattern if promiscuous is disabled");
    rpc_rte_eth_macaddr_get(iut_rpcs, iut_port->if_index, &iut_mac);
    if (!promisc && test_pdus_dst_mac_is_set(flow_rule_pattern))
    {
        test_pdus_set_eth_src_dst(flow_rule_pattern, "", NULL,
                                  iut_mac.addr_bytes);
    }

    TEST_STEP("Make flow rule attributes with only one \"ingress\" attribute");
    CHECK_RC(test_mk_rte_flow_attr_ingress(iut_rpcs, &attr));

    TEST_STEP("Make the TCI mask of @p flow_rule_pattern do not cover "
              "non VLAN ID bits");
    write_non_vid(get_eth_tagged(flow_rule_pattern), 0, "#range.mask");

    TEST_STEP("Get the VLAN ID value from @p flow_rule_pattern");
    CHECK_RC(asn_read_uint32(get_eth_tagged(flow_rule_pattern), &vid_value,
                            "vlan-id.#plain"));
    TEST_STEP("Make two flow rule patterns from @p flow_rule_pattern: one that has "
              "non VLAN ID bits set, another that has these bits cleared. "
              "Both patterns have the same VLAN ID");
    flow_rule_pattern_non_vid = asn_copy_value(flow_rule_pattern);
    write_non_vid(get_eth_tagged(flow_rule_pattern), 0, "#range.first");
    flow_rule_pattern_copy = asn_copy_value(flow_rule_pattern);
    rpc_rte_mk_flow_rule_components(iut_rpcs, flow_rule_pattern, NULL,
                                    &pattern, NULL);

    non_vid_value = rand_range(1, TEST_NON_VID_TCI_PART_MAX);
    write_non_vid(get_eth_tagged(flow_rule_pattern_non_vid), non_vid_value,
                  "#range.first");
    rpc_rte_mk_flow_rule_components(iut_rpcs, flow_rule_pattern_non_vid, NULL,
                                    &pattern_non_vid, NULL);

    TEST_STEP("Make flow rule actions with only one \"drop\" action");
    CHECK_RC(test_mk_rte_flow_action_drop(iut_rpcs, &actions));

    TEST_STEP("Validate and create flow rule with non VLAN ID bits cleared");
    flow = tapi_rte_flow_validate_and_create_rule(iut_rpcs, iut_port->if_index,
                                                  attr, pattern, actions);

    TEST_STEP("Prepare template with non VLAN ID bits cleared that matches the filter");
    CHECK_RC(test_mk_template_get_match_fields_from_flow_ndn(
                flow_rule_pattern_copy, &tmpl_match, &match_fields));

    /* There is no support of encapsulated traffic */
    test_set_pkt_addresses(&ifrm_addrs, NULL, NULL, NULL, NULL);
    test_set_pkt_addresses(&addrs, (uint8_t *)tst_lladdr->sa_data,
                           promisc ? (const uint8_t *)iut_alien_mac->sa_data :
                           iut_mac.addr_bytes, tst_addr, iut_addr);
    CHECK_RC(test_fill_in_tmpl_req_fields(tmpl_match, &addrs, &ifrm_addrs));

    TEST_STEP("Prepare template with different VLAN ID which does not match the filter");
    tmpl_mismatch = asn_copy_value(tmpl_match);
    CHECK_RC(asn_write_uint32(get_eth_tagged(tmpl_mismatch), vid_value + 1,
                             "vlan-id.#plain"));

    TEST_STEP("Prepare template with non VLAN ID bits set which should still match "
              "the filter");
    tmpl_match_non_vid = asn_copy_value(tmpl_match);
    write_non_vid(get_eth_tagged(tmpl_match_non_vid), non_vid_value, "#plain");

    TEST_STEP("Prepare template with different VLAN ID and non VLAN ID bits set "
              "which does not match the filter");
    tmpl_mismatch_non_vid = asn_copy_value(tmpl_match_non_vid);
    CHECK_RC(asn_write_uint32(get_eth_tagged(tmpl_mismatch_non_vid),
                              vid_value + 1, "vlan-id.#plain"));

    TEST_STEP("Enable promiscuous mode if need be");
    if (promisc)
        test_rte_eth_promiscuous_enable(iut_rpcs, iut_port->if_index,
                                        TEST_OP_REQUIRED);

    second_flow = FALSE;
    while(1)
    {
        TEST_SUBSTEP("Transmit packets with match template. "
                     "Check that the packets were dropped.");
        test_send_and_match_one_packet_custom_verdicts(iut_rpcs,
                iut_port->if_index, tst_host->ta, tst_if->if_name,
                mbufs, tmpl_match, TEST_DEFAULT_QUEUE, 0, NULL,
                "Matching packet received unexpectedly");
        test_send_and_match_one_packet(iut_rpcs, iut_port->if_index,
                                       tst_host->ta, tst_if->if_name,
                                       mbufs, tmpl_match_non_vid,
                                       TEST_DEFAULT_QUEUE, 0);

        TEST_SUBSTEP("Transmit and receive packets with dissimilar templates. "
                     "Check that packets were received.");
        test_send_and_match_one_packet(iut_rpcs, iut_port->if_index,
                                       tst_host->ta, tst_if->if_name,
                                       mbufs, tmpl_mismatch,
                                       TEST_DEFAULT_QUEUE, 1);
        test_send_and_match_one_packet(iut_rpcs, iut_port->if_index,
                                       tst_host->ta, tst_if->if_name,
                                       mbufs, tmpl_mismatch_non_vid,
                                       TEST_DEFAULT_QUEUE, 1);

        if (second_flow)
            break;

        TEST_SUBSTEP("Destroy the flow rule");
        rpc_rte_flow_destroy(iut_rpcs, iut_port->if_index, flow, &error);

        TEST_SUBSTEP("Validate and create flow rule with non VLAN ID bits set ");
        flow = tapi_rte_flow_validate_and_create_rule(iut_rpcs,
                    iut_port->if_index, attr, pattern_non_vid, actions);
        second_flow = TRUE;

        TEST_SUBSTEP("Check that the same packets are received or dropped as before");
    }

    TEST_SUCCESS;

cleanup:
    if (flow != RPC_NULL)
        rpc_rte_flow_destroy(iut_rpcs, iut_port->if_index, flow, &error);

    rpc_rte_free_flow_rule(iut_rpcs, attr, pattern, actions);
    rpc_rte_free_flow_rule(iut_rpcs, RPC_NULL, pattern_non_vid, RPC_NULL);

    TEST_END;
}
/** @} */
