/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup filters-flow_rule_in2q Test ingress to specific queue flow API rules
 * @ingroup filters
 * @{
 *
 * @objective Verify Flow API by adding a filter and inspecting
 *            the inbound traffic
 *
 * @param flow_rule_in2q    Flow rule
 * @param isolated          Isolated mode toggle
 * @param promisc           Test with promiscuous mode enabled if @c TRUE
 *
 * @author Roman Zhukov <Roman.Zhukov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "filters/flow_rule_in2q"

#include "dpdk_pmd_test.h"

/**
 * The number of Rx queues
 */
#define TEST_NB_RX_QUEUES 2

/**
 * The default queue number
 */
#define TEST_DEF_QUEUE_NB 0

/**
 * The filtering queue number
 */
#define TEST_FILTER_QUEUE_NB 1

int
main(int argc, char *argv[])
{
    rcf_rpc_server                         *iut_rpcs = NULL;
    rcf_rpc_server                         *tst_rpcs = NULL;
    tapi_env_host                          *tst_host;
    const struct if_nameindex              *iut_port = NULL;
    const struct if_nameindex              *tst_if = NULL;

    te_bool                                 isolated;
    te_bool                                 promisc;
    te_bool                                 is_ip6_inner_frame;
    asn_value                              *flow_rule_pattern;
    asn_value                              *flow_rule_pattern_copy;
    asn_value                              *tmpl_match;
    asn_value                             **tmpls_mismatch;
    unsigned int                            tmpls_mismatch_nb;
    rpc_rte_flow_attr_p                     attr = RPC_NULL;
    rpc_rte_flow_item_p                     pattern = RPC_NULL;
    rpc_rte_flow_action_p                   actions = RPC_NULL;
    rpc_rte_flow_p                          flow = RPC_NULL;
    tarpc_rte_flow_error                    error;

    rpc_rte_mbuf_p                          mbufs[BURST_SIZE] = {};
    uint32_t                                match_fields;
    uint32_t                                match_fields_mask = ~0;
    unsigned int                            i;
    int                                     ifrm_af;
    te_bool                                 duplicate_packets = FALSE;

    struct test_ethdev_config               ethdev_config;
    struct tarpc_rte_eth_conf               eth_conf;
    struct tarpc_ether_addr                 iut_lladdr;
    const struct sockaddr                  *iut_alien_mac = NULL;
    const struct sockaddr                  *tst_alien_mac = NULL;
    const struct sockaddr                  *tst_lladdr = NULL;
    const struct sockaddr                  *iut_addr = NULL;
    const struct sockaddr                  *tst_addr = NULL;
    struct sockaddr                        *ifrm_src_addr = NULL;
    struct sockaddr                        *ifrm_dst_addr = NULL;
    const struct sockaddr                  *mcast_addr = NULL;
    const uint8_t                          *not_match_dst_addr = NULL;
    const uint8_t                          *not_match_ifrm_dst_addr = NULL;
    struct test_pkt_addresses               addrs;
    struct test_pkt_addresses               ifrm_addrs;


    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);
    TEST_GET_BOOL_PARAM(isolated);
    TEST_GET_BOOL_PARAM(promisc);
    TEST_GET_BOOL_PARAM(is_ip6_inner_frame);
    TEST_GET_NDN_RTE_FLOW_PATTERN(flow_rule_pattern);
    TEST_GET_LINK_ADDR(iut_alien_mac);
    TEST_GET_LINK_ADDR(tst_alien_mac);
    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_ADDR_NO_PORT(iut_addr);
    TEST_GET_ADDR_NO_PORT(tst_addr);
    TEST_GET_LINK_ADDR(mcast_addr);

#define TEST_SEND_AND_MATCH_ONE_PACKET_TST2IUT(_tmpl, _queue, _packet_expected)\
    do {                                                                       \
        asn_value      *__ptrn = NULL;                                         \
        unsigned int    received = 0;                                          \
                                                                               \
        CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,           \
                                tst_if->if_name, _tmpl, NULL, &__ptrn));       \
                                                                               \
        rc = test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, _queue, \
                                             mbufs, TE_ARRAY_LEN(mbufs),       \
                                             _packet_expected, __ptrn, TRUE);  \
        if (rc == TE_EFAIL) {                                                  \
            CHECK_RC(test_rx_burst_match_pattern_custom_verdicts(iut_rpcs,     \
                            iut_port->if_index, TEST_DEF_QUEUE_NB,             \
                            mbufs, TE_ARRAY_LEN(mbufs),                        \
                            _packet_expected, __ptrn, TRUE,                    \
                            "There are no packets on default queue",           \
                            "Unexpected packets on default queue"));           \
                                                                               \
            TEST_VERDICT("Packet received on default queue instead of expected"); \
        }                                                                      \
        CHECK_RC(rc);                                                          \
        received = _packet_expected;                                           \
        /*                                                                     \
         * When it is expected that packets are received to the non-default    \
         * queue, the isolate mode is disabled and there should be duplicate   \
         * packets, check whether the duplicated packets have received to      \
         * the default queue too                                               \
         */                                                                    \
        if (duplicate_packets && !isolated && _queue != 0)                     \
        {                                                                      \
            CHECK_RC(test_rx_burst_match_pattern_custom_verdicts(              \
                        iut_rpcs, iut_port->if_index, TEST_DEF_QUEUE_NB,       \
                        &mbufs[received],                                      \
                        TE_ARRAY_LEN(mbufs) - received,                        \
                        _packet_expected, __ptrn, TRUE,                        \
                        "Duplicate packet is not received", NULL));            \
            received += _packet_expected;                                      \
        }                                                                      \
                                                                               \
        if (received > 0)                                                      \
        {                                                                      \
            rpc_rte_pktmbuf_free_array(iut_rpcs, mbufs, received);             \
            test_nullify_rte_pktmbuf_array(mbufs, received);                   \
        }                                                                      \
    } while (0)

    TEST_STEP("Setup 2 Rx queues in non-RSS mode");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);

    ethdev_config.eth_conf = test_rpc_rte_eth_make_eth_conf(
                                      iut_rpcs, iut_port->if_index, &eth_conf);

    ethdev_config.nb_rx_queue = TEST_NB_RX_QUEUES;

    TEST_STEP("Prepare @c TEST_ETHDEV_INITIALIZED state");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Check maximum number of Rx queues");
    if (TEST_NB_RX_QUEUES > ethdev_config.dev_info.max_rx_queues)
        TEST_SKIP("So many Rx queues are not supported");

    TEST_STEP("Enter isolated mode if need be");
    if (isolated)
        tapi_rte_flow_isolate(iut_rpcs, iut_port->if_index, 1);

    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    rpc_rte_eth_macaddr_get(iut_rpcs, iut_port->if_index, &iut_lladdr);

    if (!promisc && test_pdus_dst_mac_is_set(flow_rule_pattern))
    {
        match_fields_mask &= ~TEST_TMPL_DST_MAC;
        test_pdus_set_eth_src_dst(flow_rule_pattern, "", NULL,
                                  iut_lladdr.addr_bytes);
    }

    CHECK_RC(tapi_ndn_subst_env(flow_rule_pattern, &test_params, &env));
    flow_rule_pattern_copy = asn_copy_value(flow_rule_pattern);

    TEST_STEP("Make flow rule attributes with only one \"ingress\" attribute");
    CHECK_RC(test_mk_rte_flow_attr_ingress(iut_rpcs, &attr));

    TEST_STEP("Make flow rule pattern by @p flow_rule_pattern");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_mk_flow_rule_components(iut_rpcs, flow_rule_pattern, NULL,
                                         &pattern, NULL);
    if (rc == -TE_RC(TE_RPCS, TE_EPROTONOSUPPORT))
        TEST_SKIP("The protocol used in the flow rule is not supported");
    if (rc != 0)
        TEST_VERDICT("Failed to make a flow rule pattern");

    TEST_STEP("Make flow rule actions with only one \"queue\" action");
    CHECK_RC(test_mk_rte_flow_action_queue(iut_rpcs, TEST_FILTER_QUEUE_NB,
                                           &actions));

    TEST_STEP("Validate and create the flow rule");
    flow = tapi_rte_flow_validate_and_create_rule(iut_rpcs, iut_port->if_index,
                                                  attr, pattern, actions);

    TEST_STEP("Prepare template that matches the filter");
    CHECK_RC(test_mk_template_get_match_fields_from_flow_ndn(
                flow_rule_pattern_copy, &tmpl_match, &match_fields));

    if (is_ip6_inner_frame)
        ifrm_af = AF_INET6;
    else
        ifrm_af = AF_INET;

    ifrm_src_addr = test_rand_ip_addr(ifrm_af);
    CHECK_NOT_NULL(ifrm_src_addr);

    ifrm_dst_addr = test_rand_ip_addr(ifrm_af);
    CHECK_NOT_NULL(ifrm_dst_addr);

    test_set_pkt_addresses(&addrs, (uint8_t *)tst_lladdr->sa_data,
                           promisc ? (const uint8_t *)iut_alien_mac->sa_data :
                           iut_lladdr.addr_bytes, tst_addr, iut_addr);
    test_set_pkt_addresses(&ifrm_addrs, (uint8_t *)tst_alien_mac->sa_data,
                           iut_lladdr.addr_bytes, ifrm_src_addr, ifrm_dst_addr);
    CHECK_RC(test_fill_in_tmpl_req_fields(tmpl_match, &addrs, &ifrm_addrs));

    TEST_STEP("Add tunnel UDP ports to recognize received packets "
              "if VXLAN/GENEVE protocols are used");
    CHECK_RC(test_add_tunnel_udp_port_from_tmpl(&ethdev_config, tmpl_match,
                                                TRUE));

    TEST_STEP("When filtering a multicast packet and a unicast packet with "
              "enabled promiscuous mode, it is expected that there will be "
              "a duplication of packets");
    duplicate_packets = promisc && ((match_fields & TEST_TMPL_UN_MCAST_DST) ||
                                    (match_fields & TEST_TMPL_UN_UCAST_DST));

    TEST_STEP("Enable promiscuous mode if need be");
    if (promisc)
        test_rte_eth_promiscuous_enable(iut_rpcs, iut_port->if_index,
                                        TEST_OP_REQUIRED);

    TEST_STEP("Transmit and receive one packet with prepared match template. "
              "Check that the packet was received on correct queue.");
    TEST_SEND_AND_MATCH_ONE_PACKET_TST2IUT(tmpl_match, TEST_FILTER_QUEUE_NB, 1);

    if (match_fields & TEST_TMPL_UN_UCAST_DST)
        not_match_dst_addr = (uint8_t *)mcast_addr->sa_data;
    else
        not_match_dst_addr = (uint8_t *)iut_alien_mac->sa_data;

    if (match_fields & TEST_TMPL_IFRM_UN_UCAST_DST)
        not_match_ifrm_dst_addr = (uint8_t *)mcast_addr->sa_data;
    else
        not_match_ifrm_dst_addr = iut_lladdr.addr_bytes;

    TEST_STEP("Transmit and receive packets with dissimilar templates. "
              "Check that packets were received on default queue.");
    test_set_pkt_addresses(&addrs, (uint8_t *)tst_alien_mac->sa_data,
                           not_match_dst_addr, iut_addr, tst_addr);
    test_set_pkt_addresses(&ifrm_addrs, NULL, not_match_ifrm_dst_addr,
                           NULL, NULL);
    CHECK_RC(test_mk_tmpls_with_change_one_field(tmpl_match,
                                                 match_fields & match_fields_mask,
                                                 &addrs, &ifrm_addrs,
                                                 &tmpls_mismatch,
                                                 &tmpls_mismatch_nb));

    for (i = 0; i < tmpls_mismatch_nb; i++)
    {
        TEST_SEND_AND_MATCH_ONE_PACKET_TST2IUT(tmpls_mismatch[i],
                                               TEST_DEF_QUEUE_NB, !isolated);

        if (isolated)
            TEST_SEND_AND_MATCH_ONE_PACKET_TST2IUT(tmpls_mismatch[i],
                                                   TEST_FILTER_QUEUE_NB, 0);
    }

    TEST_SUCCESS;

cleanup:
    if (flow != RPC_NULL)
        rpc_rte_flow_destroy(iut_rpcs, iut_port->if_index, flow, &error);

    rpc_rte_free_flow_rule(iut_rpcs, attr, pattern, actions);

    rpc_rte_pktmbuf_free_array(iut_rpcs, mbufs, TE_ARRAY_LEN(mbufs));

    TEST_END;
}
/** @} */
