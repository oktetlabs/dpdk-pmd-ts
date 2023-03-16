/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup filters-flow_rule_mark_and_flag Check RTE flow API rules implying MARK and FLAG actions
 * @ingroup filters
 * @{
 *
 * @objective Make sure that RTE flow API MARK and FLAG actions are carried
 *            out correctly
 *
 * @param flow_rule_pattern     Flow rule pattern
 * @param flow_rule_actions     Flow rule actions
 * @param mark_value            MARK id value
 *
 * @author Roman Zhukov <Roman.Zhukov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "filters/flow_rule_mark_and_flag"

#include "dpdk_pmd_test.h"

/**
 * The default queue number to which mismatched packets are directed
 * in case of non-isolated RTE flow.
 */
#define TEST_DEF_QUEUE_NB 0

int
main(int argc, char *argv[])
{
    rcf_rpc_server                         *iut_rpcs = NULL;
    tapi_env_host                          *tst_host;
    const struct if_nameindex              *iut_port = NULL;
    const struct if_nameindex              *tst_if = NULL;

    asn_value                              *flow_rule_pattern;
    asn_value                              *flow_rule_pattern_copy;
    asn_value                              *flow_rule_actions;
    unsigned int                            actions_len;
    asn_value                              *action;
    asn_value                              *tmpl_match;
    asn_value                             **tmpls_mismatch;
    unsigned int                            tmpls_mismatch_nb;
    asn_value                              *patterns;
    rpc_rte_flow_attr_p                     attr = RPC_NULL;
    rpc_rte_flow_item_p                     pattern = RPC_NULL;
    rpc_rte_flow_action_p                   actions = RPC_NULL;
    rpc_rte_flow_p                          flow = RPC_NULL;
    tarpc_rte_flow_error                    error;

    rpc_rte_mbuf_p                          mbufs[BURST_SIZE] = {};
    uint32_t                                match_fields;
    unsigned int                            i;
    size_t                                  size;
    uint8_t                                 type;
    te_bool                                 is_mark_action = FALSE;
    unsigned int                            pkts_nb;
    unsigned int                            received = 0;
    uint64_t                                ol_flags;
    uint32_t                                fdir_id;
    unsigned int                            mark_value;

    struct test_ethdev_config               ethdev_config;
    const struct sockaddr                  *iut_alien_mac = NULL;
    const struct sockaddr                  *tst_alien_mac = NULL;
    const struct sockaddr                  *tst_lladdr = NULL;
    const struct sockaddr                  *iut_addr = NULL;
    const struct sockaddr                  *tst_addr = NULL;
    const struct sockaddr                  *alien_addr = NULL;
    struct test_pkt_addresses               addrs;
    struct test_pkt_addresses               ifrm_addrs;

    uint64_t metadata_requested;
    uint64_t metadata;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);
    TEST_GET_NDN_RTE_FLOW_PATTERN(flow_rule_pattern);
    TEST_GET_NDN_RTE_FLOW_ACTIONS(flow_rule_actions);
    TEST_GET_LINK_ADDR(iut_alien_mac);
    TEST_GET_LINK_ADDR(tst_alien_mac);
    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_ADDR_NO_PORT(iut_addr);
    TEST_GET_ADDR_NO_PORT(tst_addr);
    TEST_GET_ADDR_NO_PORT(alien_addr);
    TEST_GET_UINT_PARAM(mark_value);

    TEST_STEP("Prepare state TEST_ETHDEV_INITIALIZED");
    test_default_prepare_ethdev(&env, iut_rpcs, iut_port, &ethdev_config,
                                TEST_ETHDEV_INITIALIZED);

    CHECK_RC(tapi_ndn_subst_env(flow_rule_pattern, &test_params, &env));
    flow_rule_pattern_copy = asn_copy_value(flow_rule_pattern);
    CHECK_RC(tapi_ndn_subst_env(flow_rule_actions, &test_params, &env));

    /*- Make flow rule attributes with only one "ingress" attribute */
    CHECK_RC(test_mk_rte_flow_attr_ingress(iut_rpcs, &attr));

    /*- Make flow rule pattern by @p flow_rule_pattern */
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_mk_flow_rule_components(iut_rpcs, flow_rule_pattern, NULL,
                                         &pattern, NULL);
    if (rc == -TE_RC(TE_RPCS, TE_EPROTONOSUPPORT))
        TEST_SKIP("The protocol used in the flow rule is not supported");
    if (rc != 0)
        TEST_VERDICT("Failed to make a flow rule pattern");

    /*- Check that there is only one action in the @p flow_rule_actions,
     *  and if this is MARK action, then set the MARK id value
     */
    actions_len = (unsigned int)asn_get_length(flow_rule_actions, "");
    if (actions_len != 1)
        TEST_VERDICT("Only one action should be in the flow rule");

    CHECK_RC(asn_get_indexed(flow_rule_actions, &action, 0, ""));
    size = sizeof(type);
    CHECK_RC(asn_read_value_field(action, &type, &size, "type"));
    if (type == NDN_FLOW_ACTION_TYPE_MARK)
    {
        CHECK_RC(asn_write_int32(action, mark_value, "conf.#id"));
        is_mark_action = TRUE;
    }

    /*- Add QUEUE flow rule action to direct matched traffic to default queue */
    test_add_ndn_action_queue(flow_rule_actions, 1, TEST_DEF_QUEUE_NB);

    /*- Make flow rule actions by @p flow_rule_actions */
    rpc_rte_mk_flow_rule_components(iut_rpcs, flow_rule_actions, NULL,
                                    NULL, &actions);

    RPC_AWAIT_ERROR(iut_rpcs);

    TEST_STEP("Negotiate the NIC's ability to deliver MARK/FLAG to the PMD");
    if (type == NDN_FLOW_ACTION_TYPE_MARK)
        metadata_requested = (1ULL << TARPC_RTE_ETH_RX_METADATA_USER_MARK_BIT);
    else if (type == NDN_FLOW_ACTION_TYPE_FLAG)
        metadata_requested = (1ULL << TARPC_RTE_ETH_RX_METADATA_USER_FLAG_BIT);
    metadata = metadata_requested;
    rc = rpc_rte_eth_rx_metadata_negotiate(iut_rpcs, ethdev_config.port_id,
                                           &metadata);
    if (rc == 0 && (metadata & metadata_requested) == 0)
        TEST_SKIP("Delivery of MARK/FLAG from NIC to PMD is unsupported");
    else if (rc != 0 && rc != -TE_RC(TE_RPC, TE_EOPNOTSUPP))
        CHECK_RC(rc);

    TEST_STEP("Prepare state TEST_ETHDEV_STARTED");
    test_default_prepare_ethdev(&env, iut_rpcs, iut_port, &ethdev_config,
                                TEST_ETHDEV_STARTED);

    /*- Validate and create the flow rule */
    flow = tapi_rte_flow_validate_and_create_rule(iut_rpcs, iut_port->if_index,
                                                  attr, pattern, actions);

    /*- Prepare template that matches the filter */
    CHECK_RC(test_mk_template_get_match_fields_from_flow_ndn(
                flow_rule_pattern_copy, &tmpl_match, &match_fields));

    /* There is no support of encapsulated traffic */
    test_set_pkt_addresses(&ifrm_addrs, NULL, NULL, NULL, NULL);
    test_set_pkt_addresses(&addrs, (uint8_t *)tst_lladdr->sa_data,
                           (uint8_t *)iut_alien_mac->sa_data,
                           tst_addr, iut_addr);
    CHECK_RC(test_fill_in_tmpl_req_fields(tmpl_match, &addrs, &ifrm_addrs));

    /*- Enable promiscuous mode to recieve packets with alien mac addresses */
    test_rte_eth_promiscuous_enable(iut_rpcs, iut_port->if_index,
                                    TEST_OP_REQUIRED);

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    /*- Transmit one packet with prepared match template */
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                tst_if->if_name, tmpl_match, NULL, &patterns));
    pkts_nb = 1;

    /*- Transmit packets with dissimilar templates */
    test_set_pkt_addresses(&addrs, (uint8_t *)tst_alien_mac->sa_data,
                           (uint8_t *)iut_alien_mac->sa_data,
                           alien_addr, alien_addr);
    CHECK_RC(test_mk_tmpls_with_change_one_field(tmpl_match, match_fields,
                                                 &addrs, &ifrm_addrs,
                                                 &tmpls_mismatch, &tmpls_mismatch_nb));
    for (i = 0; i < tmpls_mismatch_nb; i++)
    {
        asn_value *ptrn;

        CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                            tst_if->if_name, tmpls_mismatch[i], NULL, &ptrn));
        CHECK_RC(tapi_tad_concat_patterns(patterns, ptrn));
        pkts_nb++;
    }

    /*- Receive all packets */
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index,
                                         TEST_DEF_QUEUE_NB,
                                         mbufs, TE_ARRAY_LEN(mbufs),
                                         pkts_nb, patterns, TRUE));
    received = pkts_nb;

    /*- Check that the first packet has RTE_MBUF_F_RX_FDIR mbuf offload flag in case
     *  of FLAG action and has RTE_MBUF_F_RX_FDIR and RTE_MBUF_F_RX_FDIR_ID flags and right
     *  MARK value in case of MARK action
     */
    ol_flags = rpc_rte_pktmbuf_get_flags(iut_rpcs, mbufs[0]);
    if (~ol_flags & (1UL << TARPC_RTE_MBUF_F_RX_FDIR))
        TEST_VERDICT("RTE_MBUF_F_RX_FDIR mbuf offload flag is not set");
    if (is_mark_action)
    {
        if (~ol_flags & (1UL << TARPC_RTE_MBUF_F_RX_FDIR_ID))
            TEST_VERDICT("RTE_MBUF_F_RX_FDIR_ID mbuf offload flag is not set");

        fdir_id = rpc_rte_pktmbuf_get_fdir_id(iut_rpcs, mbufs[0]);
        if (fdir_id != mark_value)
            TEST_VERDICT("MARK value is wrong");
    }

    /*- Check that other packets don't have RTE_MBUF_F_RX_FDIR and RTE_MBUF_F_RX_FDIR_ID mbuf
     *  offload flags
     */
    for (i = 1; i < pkts_nb; i++)
    {
        ol_flags = rpc_rte_pktmbuf_get_flags(iut_rpcs, mbufs[i]);
        if (ol_flags & (1UL << TARPC_RTE_MBUF_F_RX_FDIR))
            TEST_VERDICT("RTE_MBUF_F_RX_FDIR mbuf offload flag is set for packet that "
                         "should not match the flow rule");

        if (ol_flags & (1UL << TARPC_RTE_MBUF_F_RX_FDIR_ID))
            TEST_VERDICT("RTE_MBUF_F_RX_FDIR_ID mbuf offload flag is set for packet that "
                         "should not match the flow rule");
    }

    TEST_SUCCESS;

cleanup:
    for (i = 0; i < received; i++)
        rpc_rte_pktmbuf_free(iut_rpcs, mbufs[i]);

    if (flow != RPC_NULL)
        rpc_rte_flow_destroy(iut_rpcs, iut_port->if_index, flow, &error);

    rpc_rte_free_flow_rule(iut_rpcs, attr, pattern, actions);

    TEST_END;
}
/** @} */
