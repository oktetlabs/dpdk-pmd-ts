/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup filters-flow_tunnel Flow tunnel offload
 * @ingroup filters
 * @{
 *
 * @objective Verify basic tunnel offload operability
 *
 * @param  tunnel_rule_ptrn_ndn  TUNNEL rule match pattern
 * @param  switch_rule_ptrn_ndn  SWITCH rule match pattern
 * @param  tunnel_rule_do_count  Add action COUNT to the TUNNEL rule
 * @param  switch_rule_do_count  Add action COUNT to the SWITCH rule
 *
 * @note  @b LIMITATIONS:
 *        - @p switch_rule_ptrn_ndn must include all of @p tunnel_rule_ptrn_ndn
 *        - Only VXLAN is supported for now: use appropriate patterns
 *        - The test does not send mismatch packets
 *        - Byte counts are not verified
 *
 * @author Ivan Malov <ivan.malov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "filters/flow_tunnel"

#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>

#include "tapi_rpc_rte_ethdev.h"
#include "tapi_rpc_rte_flow.h"
#include "tapi_rpc_rte_mbuf.h"
#include "tapi_rpc_unistd.h"
#include "rte_flow_ndn.h"

#include "dpdk_pmd_test.h"

static test_transceiver_transform_tmpl trfm_tmpl;
static asn_value *
trfm_tmpl(const asn_value *tmpl, void *data)
{
    UNUSED(data);
    return test_decap_tmpl_ptrn_pdus(tmpl, "pdus");
}

static test_transceiver_transform_ptrn trfm_ptrn;
static asn_value *
trfm_ptrn(const asn_value *ptrn, void *data)
{
    UNUSED(data);
    return test_decap_tmpl_ptrn_pdus(ptrn, "0.pdus");
}

int
main(int argc, char *argv[])
{
    bool        switch_rule_do_count;
    asn_value  *switch_rule_ptrn_ndn;
    bool        tunnel_rule_do_count;
    asn_value  *tunnel_rule_ptrn_ndn;

    const struct if_nameindex  *iut_port;
    rcf_rpc_server             *iut_rpcs;
    tapi_env_host              *tst_host;
    const struct if_nameindex  *tst_if;

    struct test_pkt_addresses  inner_addrs = {
                                   .src_mac = (uint8_t *)"\x00\x00\x5e\x00\x03\x01",
                                   .dst_mac = (uint8_t *)"\x00\x00\x5e\x00\x03\x02",
                                   .src_ip = test_rand_ip_addr(AF_INET6),
                                   .dst_ip = test_rand_ip_addr(AF_INET6),
                               };
    struct test_pkt_addresses  outer_addrs = {
                                   .src_mac = (uint8_t *)"\x00\x00\x5e\x00\x03\x03",
                                   .dst_mac = (uint8_t *)"\x00\x00\x5e\x00\x03\x04",
                                   .src_ip = test_rand_ip_addr(AF_INET6),
                                   .dst_ip = test_rand_ip_addr(AF_INET6),
                               };

    rpc_rte_flow_action_p                switch_rule_act_count = RPC_NULL;
    rpc_rte_flow_action_p                tunnel_rule_act_count = RPC_NULL;
    rpc_rte_flow_item_p                  switch_rule_itms_app = RPC_NULL;
    rpc_rte_flow_item_p                  switch_rule_itms_pmd = RPC_NULL;
    rpc_rte_flow_action_p                tunnel_rule_acts_app = RPC_NULL;
    rpc_rte_flow_action_p                tunnel_rule_acts_pmd = RPC_NULL;
    rpc_rte_flow_action_p                switch_rule_acts = RPC_NULL;
    rpc_rte_flow_attr_p                  switch_rule_attr = RPC_NULL;
    rpc_rte_flow_item_p                  switch_rule_ptrn = RPC_NULL;
    rpc_rte_flow_action_p                tunnel_rule_acts = RPC_NULL;
    rpc_rte_flow_attr_p                  tunnel_rule_attr = RPC_NULL;
    rpc_rte_flow_item_p                  tunnel_rule_ptrn = RPC_NULL;
    uint32_t                             switch_rule_group_id = 0;
    uint32_t                             tunnel_rule_group_id = 0;
    uint32_t                             nb_switch_rule_itms_pmd;
    uint32_t                             nb_tunnel_rule_acts_pmd;
    rpc_rte_flow_p                       switch_rule = RPC_NULL;
    rpc_rte_flow_p                       tunnel_rule = RPC_NULL;
    uint32_t                             switch_rule_prio = 0;
    uint32_t                             tunnel_rule_prio = 0;
    asn_value                           *ingress_port_itm_ndn;
    asn_value                           *switch_rule_acts_ndn;
    asn_value                           *tunnel_rule_acts_ndn;
    tarpc_rte_flow_query_data            rule_counter_query;
    struct test_transceiver             *trsc_iut = NULL;
    struct test_transceiver             *trsc_tst = NULL;
    rpc_rte_mbuf_p                       mbufs[BURST_SIZE] = {};
    uint32_t                             match_fields;
    struct tarpc_rte_flow_tunnel         tunnel = {};
    uint64_t                             metadata;
    struct test_transceiver_exchange    *x = NULL;
    struct tarpc_rte_flow_restore_info   info;
    asn_value                           *ptrn;
    asn_value                           *tmpl;
    unsigned int                         ret;
    struct test_ethdev_config            tec;

    TEST_START;

    TEST_GET_NDN_RTE_FLOW_PATTERN(switch_rule_ptrn_ndn);
    TEST_GET_NDN_RTE_FLOW_PATTERN(tunnel_rule_ptrn_ndn);
    TEST_GET_BOOL_PARAM(switch_rule_do_count);
    TEST_GET_BOOL_PARAM(tunnel_rule_do_count);

    TEST_GET_HOST(tst_host);
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    tunnel.type = TARPC_RTE_TUNNEL_TYPE_VXLAN;
    tunnel.tun_id = random();

    TEST_STEP("Prepare state TEST_ETHDEV_INITIALIZED");
    test_default_prepare_ethdev(&env, iut_rpcs, iut_port, &tec,
                                TEST_ETHDEV_INITIALIZED);

    RPC_AWAIT_ERROR(iut_rpcs);

    TEST_STEP("Negotiate the NIC's ability to deliver tunnel IDs to the PMD");
    metadata = (1ULL << TARPC_RTE_ETH_RX_METADATA_TUNNEL_ID_BIT);
    rc = rpc_rte_eth_rx_metadata_negotiate(iut_rpcs, tec.port_id, &metadata);
    if (rc == 0 &&
        (metadata & (1ULL << TARPC_RTE_ETH_RX_METADATA_TUNNEL_ID_BIT)) == 0)
        TEST_SKIP("Flow tunnel offload is unsupported");
    else if (rc != 0 && rc != -TE_RC(TE_RPC, TE_EOPNOTSUPP))
        CHECK_RC(rc);

    TEST_STEP("Prepare state TEST_ETHDEV_STARTED");
    test_default_prepare_ethdev(&env, iut_rpcs, iut_port, &tec,
                                TEST_ETHDEV_STARTED);

    TEST_STEP("Make a packet template from the SWITCH rule pattern NDN");
    CHECK_RC(tapi_ndn_subst_env(switch_rule_ptrn_ndn, &test_params, &env));
    test_mk_pattern_and_tmpl_by_flow_rule_pattern(iut_rpcs,
                                                  switch_rule_ptrn_ndn,
                                                  &switch_rule_itms_app, &tmpl,
                                                  &match_fields);
    test_fill_in_tmpl_req_fields(tmpl, &outer_addrs, &inner_addrs);

    TEST_STEP("Since DST addresses are alien, enable promiscuous mode on IUT");
    test_rte_eth_promiscuous_enable(iut_rpcs, tec.port_id, TEST_OP_REQUIRED);

    TEST_STEP("Send a packet from TST to IUT; watch it arrive unscathed");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                                tst_if->if_name, tmpl, NULL,
                                                &ptrn));
    test_rx_burst_match_pattern(iut_rpcs, tec.port_id, 0,
                                mbufs, TE_ARRAY_LEN(mbufs), 1, ptrn, TRUE);

    RPC_AWAIT_ERROR(iut_rpcs);

    TEST_STEP("Make sure that the packet is not recognised as a tunnel one");
    rc = rpc_rte_flow_get_restore_info(iut_rpcs, tec.port_id, mbufs[0],
                                       &info, NULL);
    if (rc == 0)
        TEST_VERDICT("Unexpectedly, the packet is recognised as a tunnel one");
    else if (rc == -TE_RC(TE_RPC, TE_ENOSYS))
        TEST_SKIP("Flow tunnel offload is unsupported");

    rpc_rte_pktmbuf_free(iut_rpcs, mbufs[0]);
    mbufs[0] = RPC_NULL;

    TEST_STEP("Prepare DPDK representation of the TUNNEL rule");
    tapi_rte_flow_make_attr(iut_rpcs, tunnel_rule_group_id, tunnel_rule_prio,
                            TRUE, FALSE, TRUE, &tunnel_rule_attr);
    CHECK_RC(tapi_ndn_subst_env(tunnel_rule_ptrn_ndn, &test_params, &env));
    rpc_rte_mk_flow_rule_components(iut_rpcs, tunnel_rule_ptrn_ndn,
                                    NULL, &tunnel_rule_ptrn, NULL);
    CHECK_NOT_NULL(ingress_port_itm_ndn = asn_init_value(ndn_rte_flow_items));
    tapi_rte_flow_add_ndn_item_port(NDN_FLOW_ITEM_TYPE_REPRESENTED_PORT,
                                    tec.port_id, ingress_port_itm_ndn, -1);
    rpc_rte_insert_flow_rule_items(iut_rpcs, &tunnel_rule_ptrn,
                                   ingress_port_itm_ndn, 0);
    CHECK_NOT_NULL(tunnel_rule_acts_ndn = asn_init_value(ndn_rte_flow_actions));
    if (tunnel_rule_do_count)
    {
        test_add_and_mk_rte_flow_action_count(0, -1, iut_rpcs,
                                              tunnel_rule_acts_ndn,
                                              &tunnel_rule_act_count);
    }
    tapi_rte_flow_add_ndn_action_jump(tunnel_rule_acts_ndn, -1,
                                      switch_rule_group_id);
    rpc_rte_mk_flow_rule_components(iut_rpcs, tunnel_rule_acts_ndn,
                                    NULL, NULL, &tunnel_rule_acts_app);
    rpc_rte_flow_tunnel_decap_set(iut_rpcs, tec.port_id, &tunnel,
                                  &tunnel_rule_acts_pmd,
                                  &nb_tunnel_rule_acts_pmd, NULL);
    rpc_rte_flow_prepend_opaque_actions(iut_rpcs, tunnel_rule_acts_app,
                                        tunnel_rule_acts_pmd,
                                        nb_tunnel_rule_acts_pmd,
                                        &tunnel_rule_acts);

    TEST_STEP("Validate and create the TUNNEL rule");
    tunnel_rule = tapi_rte_flow_validate_and_create_rule(iut_rpcs, tec.port_id,
                                                         tunnel_rule_attr,
                                                         tunnel_rule_ptrn,
                                                         tunnel_rule_acts);

    TEST_STEP("Send a packet from TST to IUT; watch it arrive");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                                tst_if->if_name, tmpl, NULL,
                                                &ptrn));
    test_rx_burst_match_pattern(iut_rpcs, tec.port_id, 0, mbufs,
                                TE_ARRAY_LEN(mbufs), 1, NULL, FALSE);

    TEST_STEP("Validate tunnel restore info operation for the packet");
    rpc_rte_flow_get_restore_info(iut_rpcs, tec.port_id, mbufs[0], &info, NULL);
    if ((info.flags & (1ULL << TARPC_RTE_FLOW_RESTORE_INFO_TUNNEL_BIT)) == 0)
        TEST_VERDICT("Tunnel restore info does not indicate flag TUNNEL");
    if (memcmp(&info.tunnel, &tunnel, sizeof(tunnel)) != 0)
        TEST_VERDICT("Tunnel restore info indicates incorrect tunnel data");
    if ((info.flags &
        (1ULL << TARPC_RTE_FLOW_RESTORE_INFO_GROUP_ID_BIT)) != 0 &&
        info.group_id != switch_rule_group_id)
        TEST_VERDICT("Tunnel restore info indicates incorrect group ID");

    TEST_STEP("Verify the packet data");
    if ((info.flags &
        (1ULL << TARPC_RTE_FLOW_RESTORE_INFO_ENCAPSULATED_BIT)) == 0)
    {
        ptrn = test_decap_tmpl_ptrn_pdus(ptrn, "0.pdus");
    }
    rpc_rte_mbuf_match_pattern(iut_rpcs, ptrn, mbufs, 1, NULL, &ret);
    if (ret != 1)
        TEST_VERDICT("Packet data mismatch");

    if (tunnel_rule_do_count)
    {
        TEST_STEP("Make sure that the TUNNEL rule hit counter reads 1");
        memset(&rule_counter_query, 0, sizeof(rule_counter_query));
        test_wait_stats_update();
        rpc_rte_flow_query(iut_rpcs, tec.port_id, tunnel_rule,
                           tunnel_rule_act_count, &rule_counter_query, NULL);
        test_check_flow_query_data(&rule_counter_query, TRUE, 1, FALSE, 0);
    }

    TEST_STEP("Prepare DPDK representation of the SWITCH rule");
    tapi_rte_flow_make_attr(iut_rpcs, switch_rule_group_id, switch_rule_prio,
                            TRUE, FALSE, TRUE, &switch_rule_attr);
    rpc_rte_flow_tunnel_match(iut_rpcs, tec.port_id, &tunnel,
                              &switch_rule_itms_pmd,
                              &nb_switch_rule_itms_pmd, NULL);
    rpc_rte_flow_prepend_opaque_items(iut_rpcs, switch_rule_itms_app,
                                      switch_rule_itms_pmd,
                                      nb_switch_rule_itms_pmd,
                                      &switch_rule_ptrn);
    rpc_rte_insert_flow_rule_items(iut_rpcs, &switch_rule_ptrn,
                                   ingress_port_itm_ndn, 0);
    CHECK_NOT_NULL(switch_rule_acts_ndn = asn_init_value(ndn_rte_flow_actions));
    if (switch_rule_do_count)
    {
        test_add_and_mk_rte_flow_action_count(0, -1, iut_rpcs,
                                              switch_rule_acts_ndn,
                                              &switch_rule_act_count);
    }
    tapi_rte_flow_add_ndn_action_port(NDN_FLOW_ACTION_TYPE_PORT_REPRESENTOR,
                                      tec.port_id, switch_rule_acts_ndn, -1);
    rpc_rte_mk_flow_rule_components(iut_rpcs, switch_rule_acts_ndn,
                                    NULL, NULL, &switch_rule_acts);

    TEST_STEP("Validate and create the SWITCH rule");
    switch_rule = tapi_rte_flow_validate_and_create_rule(iut_rpcs, tec.port_id,
                                                         switch_rule_attr,
                                                         switch_rule_ptrn,
                                                         switch_rule_acts);

    TEST_STEP("Send a packet from TST to IUT; watch it arrive decapsulated");
    trsc_tst = test_transceiver_net_init(tst_host->ta, tst_if->if_name);
    trsc_iut = test_transceiver_dpdk_init(iut_rpcs, tec.port_id, tec.mp);
    x = test_transceiver_exchange_init(tmpl, trfm_tmpl, NULL, trfm_ptrn, NULL);
    test_transceiver_exchange_commit(x, trsc_tst, 1, 0, trsc_iut, 1, 0);

    if (tunnel_rule_do_count || switch_rule_do_count)
        test_wait_stats_update();

    if (tunnel_rule_do_count)
    {
        TEST_STEP("Make sure that the TUNNEL rule hit counter reads 2");
        memset(&rule_counter_query, 0, sizeof(rule_counter_query));
        rpc_rte_flow_query(iut_rpcs, tec.port_id, tunnel_rule,
                           tunnel_rule_act_count, &rule_counter_query, NULL);
        test_check_flow_query_data(&rule_counter_query, TRUE, 2, FALSE, 0);
    }

    if (switch_rule_do_count)
    {
        TEST_STEP("Make sure that the SWITCH rule hit counter reads 1");
        memset(&rule_counter_query, 0, sizeof(rule_counter_query));
        rpc_rte_flow_query(iut_rpcs, tec.port_id, switch_rule,
                           switch_rule_act_count, &rule_counter_query, NULL);
        test_check_flow_query_data(&rule_counter_query, TRUE, 1, FALSE, 0);
    }

    TEST_SUCCESS;

cleanup:

    test_transceiver_free(trsc_tst);
    test_transceiver_free(trsc_iut);
    if (switch_rule != RPC_NULL)
        rpc_rte_flow_destroy(iut_rpcs, tec.port_id, switch_rule, NULL);
    rpc_rte_flow_release_united_items(iut_rpcs, switch_rule_ptrn);
    if (switch_rule_itms_pmd != RPC_NULL)
    {
        rpc_rte_flow_tunnel_item_release(iut_rpcs, tec.port_id,
                                         switch_rule_itms_pmd,
                                         nb_switch_rule_itms_pmd, NULL);
    }
    rpc_rte_free_flow_rule(iut_rpcs, switch_rule_attr, switch_rule_itms_app,
                           switch_rule_acts);
    rpc_rte_free_flow_rule(iut_rpcs, RPC_NULL, RPC_NULL, switch_rule_act_count);
    if (tunnel_rule != RPC_NULL)
        rpc_rte_flow_destroy(iut_rpcs, tec.port_id, tunnel_rule, NULL);
    rpc_rte_flow_release_united_actions(iut_rpcs, tunnel_rule_acts);
    if (tunnel_rule_acts_pmd != RPC_NULL)
    {
        rpc_rte_flow_tunnel_action_decap_release(iut_rpcs, tec.port_id,
                                                 tunnel_rule_acts_pmd,
                                                 nb_tunnel_rule_acts_pmd, NULL);
    }
    rpc_rte_free_flow_rule(iut_rpcs, tunnel_rule_attr, tunnel_rule_ptrn,
                           tunnel_rule_acts_app);
    rpc_rte_free_flow_rule(iut_rpcs, RPC_NULL, RPC_NULL, tunnel_rule_act_count);
    if (mbufs[0] != RPC_NULL)
        rpc_rte_pktmbuf_free(iut_rpcs, mbufs[0]);

    TEST_END;
}
/** @} */
