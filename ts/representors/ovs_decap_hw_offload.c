/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK Port Representors Test Suite
 * Port representors use cases
 */

/** @defgroup representors-ovs_decap_hw_offload Simulate HW offload
 * @ingroup representors
 * @{
 *
 * @objective Make sure that offloading traffic to a virtual function
 *            works with RTE flow API
 *
 * @param encap_tunnel_type         Encapsulation tunnel type
 * @param jump_flow_rule_pattern    Flow Rule pattern with jump action
 * @param decap_flow_rule_pattern   Flow Rule pattern in the group to which
 *                                  traffic from the @p jump_flow_rule_pattern
 *                                  is directed. Contains decap action
 * @param nb_pkts                   Number of offloaded packets to send
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "representors/ovs_decap_hw_offload"

#include "dpdk_pmd_test.h"

#define TEST_GROUP_ID 1

struct test_flow_rule {
    rpc_rte_flow_attr_p attr;
    rpc_rte_flow_item_p pattern;
    rpc_rte_flow_action_p actions;
    rpc_rte_flow_p flow;
};

static test_transceiver_transform_ptrn transform_tmpl;
static asn_value *
transform_tmpl(const asn_value *tmpl_tx, void *unused)
{
    UNUSED(unused);

    return test_decap_tmpl_ptrn_pdus(tmpl_tx, "pdus");
}

static test_transceiver_transform_ptrn transform_ptrn;
static asn_value *
transform_ptrn(const asn_value *ptrn_tx, void *unused)
{
    UNUSED(unused);

    return test_decap_tmpl_ptrn_pdus(ptrn_tx, "0.pdus");
}

int
main(int argc, char *argv[])
{
    tapi_env_host                          *tst_host;
    const struct if_nameindex              *tst_if = NULL;
    rcf_rpc_server                         *iut_rpcs = NULL;
    const tapi_env_if                      *iut_port = NULL;
    struct test_ethdev_config               ethdev_config_pf;
    struct test_ethdev_config              *ethdev_config_vf;
    struct test_ethdev_config              *ethdev_config_rep;
    rpc_rte_mempool_p                       mp;

    tarpc_rte_eth_tunnel_type               encap_tunnel_type;
    asn_value                              *flow_actions;
    asn_value                              *tmpl;
    struct test_flow_rule                   jump_rule;
    struct test_flow_rule                   decap_rule;
    struct test_flow_rule                   *rules[] = {&jump_rule, &decap_rule};

    struct test_transceiver_exchange       *exchange = NULL;
    struct test_transceiver                *trsc_net = NULL;
    struct test_transceiver                *trsc_pf = NULL;
    struct test_transceiver                *trsc_vf = NULL;
    struct test_transceiver                *trsc_rep = NULL;
    unsigned int                            n_vfs;
    char                                  **vf_addrs;
    unsigned int                           *vf_ids;
    unsigned int                            i;

    asn_value                              *jump_flow_rule_pattern;
    asn_value                              *decap_flow_rule_pattern;
    unsigned int                            nb_pkts;

    uint8_t                                 dst_mac[ETHER_ADDR_LEN];
    uint8_t                                 src_mac[ETHER_ADDR_LEN];
    size_t                                  mac_len = ETHER_ADDR_LEN;
    const struct sockaddr                  *tst_lladdr = NULL;
    const struct sockaddr                  *iut_addr = NULL;
    const struct sockaddr                  *tst_addr = NULL;
    struct test_pkt_addresses               addrs;
    struct test_pkt_addresses               ifrm_addrs;


    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_ENV_IF(iut_port);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);

    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_ADDR_NO_PORT(iut_addr);
    TEST_GET_ADDR_NO_PORT(tst_addr);

    TEST_GET_NDN_RTE_FLOW_PATTERN(jump_flow_rule_pattern);
    TEST_GET_NDN_RTE_FLOW_PATTERN(decap_flow_rule_pattern);
    TEST_GET_UINT_PARAM(nb_pkts);
    TEST_GET_TUNNEL_TYPE(encap_tunnel_type);

    memset(&jump_rule, 0, sizeof(jump_rule));
    memset(&decap_rule, 0, sizeof(decap_rule));

    /* Get MAC addresses for outer rule from flow rule with jump */
    CHECK_RC(asn_read_value_field(jump_flow_rule_pattern, dst_mac, &mac_len,
                                  "0.#eth.dst-addr.#plain"));
    CHECK_RC(asn_read_value_field(jump_flow_rule_pattern, src_mac, &mac_len,
                                  "0.#eth.src-addr.#plain"));

    test_prepare_config_def_mk(&env, iut_rpcs, &iut_port->if_info,
                               &ethdev_config_pf);
    test_get_vf_pci_addrs_by_node(tapi_env_get_if_net_node(iut_port),
                                  &n_vfs, &vf_addrs, &vf_ids);


    TEST_STEP("Bind DPDK driver on a VF");

    CHECK_RC_VERDICT(tapi_cfg_pci_bind_ta_driver_on_device(iut_rpcs->ta,
                        NET_DRIVER_TYPE_DPDK, vf_addrs[0]),
                     "Failed to bind VF to DPDK driver");


    TEST_STEP("Create a big enough mempool for PF, VF and representor ports");

    mp = test_rte_pktmbuf_pool_create(iut_rpcs, TEST_PKTS_MEMPOOL_NAME,
                                     TEST_RTE_MEMPOOL_DEF_SIZE,
                                     TEST_RTE_MEMPOOL_DEF_CACHE,
                                     TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                     TEST_RTE_MEMPOOL_DEF_DATA_ROOM,
                                     rpc_rte_eth_dev_socket_id(iut_rpcs,
                                            iut_port->if_info.if_index));


    TEST_STEP("Hotplug the representor of the VF");

    test_hotplug_reps(iut_rpcs, &env, tapi_env_get_if_net_node(iut_port),
                      1, vf_ids, &ethdev_config_rep);


    TEST_STEP("Hotplug VF");

    test_hotplug_vfs_by_ids(iut_rpcs, &env,
                            tapi_env_get_if_net_node(iut_port),
                            1, vf_ids, &ethdev_config_vf);


    TEST_STEP("Make flow rule pattern and template");

    CHECK_RC(tapi_ndn_subst_env(jump_flow_rule_pattern, &test_params, &env));
    CHECK_RC(tapi_ndn_subst_env(decap_flow_rule_pattern, &test_params, &env));
    CHECK_RC(rpc_rte_mk_flow_rule_components(iut_rpcs, jump_flow_rule_pattern,
                                             NULL, &jump_rule.pattern, NULL));
    test_mk_pattern_and_tmpl_by_flow_rule_pattern(iut_rpcs,
                     decap_flow_rule_pattern, &decap_rule.pattern, &tmpl, NULL);

    test_set_pkt_addresses(&addrs,
                           src_mac, dst_mac,
                           tst_addr, iut_addr);
    test_set_pkt_addresses(&ifrm_addrs,
                           (uint8_t *)tst_lladdr->sa_data,
                           dst_mac,
                           tst_addr, iut_addr);
    CHECK_RC(test_fill_in_tmpl_req_fields(tmpl, &addrs, &ifrm_addrs));


    TEST_STEP("Start PF, VF and representor ports");

    ethdev_config_vf->mp = ethdev_config_pf.mp = ethdev_config_rep->mp = mp;
    CHECK_RC(test_prepare_ethdev(&ethdev_config_pf, TEST_ETHDEV_STARTED));
    CHECK_RC(test_prepare_ethdev(ethdev_config_rep, TEST_ETHDEV_STARTED));
    CHECK_RC(test_prepare_ethdev(ethdev_config_vf, TEST_ETHDEV_STARTED));


    TEST_STEP("Check full traffic path (IUT - representor - TST) without flow rule");

    trsc_net = test_transceiver_net_init(tst_host->ta, tst_if->if_name);
    trsc_pf = test_transceiver_dpdk_init(iut_rpcs, ethdev_config_pf.port_id,
                                           ethdev_config_pf.mp);
    trsc_rep = test_transceiver_dpdk_init(iut_rpcs,
                                          ethdev_config_rep->port_id,
                                          ethdev_config_rep->mp);
    trsc_vf = test_transceiver_dpdk_init(iut_rpcs, ethdev_config_vf->port_id,
                                         ethdev_config_vf->mp);

    test_rte_eth_promiscuous_enable(iut_rpcs, ethdev_config_pf.port_id,
                                    TEST_OP_REQUIRED);
    test_rte_eth_promiscuous_enable(iut_rpcs, ethdev_config_vf->port_id,
                                    TEST_OP_REQUIRED);

    test_transciever_simple_exchange_commit(tmpl, trsc_net, 1, 0,
                                            trsc_pf, 1, 0, NULL, NULL);
    test_transciever_simple_exchange_commit(tmpl, trsc_rep, 1, 0,
                                            trsc_vf, 1, 0, NULL, NULL);


    TEST_STEP("Make flow rule attributes with transfer and ingress or egress "
              "depending on traffic direction");

    tapi_rte_flow_make_attr(iut_rpcs, 0, 0, TRUE, FALSE, TRUE, &jump_rule.attr);
    tapi_rte_flow_make_attr(iut_rpcs, TEST_GROUP_ID, 0, TRUE, FALSE, TRUE, &decap_rule.attr);


    TEST_STEP("Make flow rule action VF (when receiving on IUT) "
              "or PHY (when receiving on TST)");

    CHECK_NOT_NULL(flow_actions = asn_init_value(ndn_rte_flow_actions));
    tapi_rte_flow_add_ndn_action_jump(flow_actions, 0, TEST_GROUP_ID);
    rpc_rte_mk_flow_rule_components(iut_rpcs, flow_actions, NULL, NULL,
                                    &jump_rule.actions);

    asn_free_value(flow_actions);
    CHECK_NOT_NULL(flow_actions = asn_init_value(ndn_rte_flow_actions));
    tapi_rte_flow_add_ndn_action_decap(flow_actions, 0, encap_tunnel_type);
    tapi_rte_flow_add_ndn_action_port(NDN_FLOW_ACTION_TYPE_PORT_REPRESENTOR,
                                      1, flow_actions, -1);
    rpc_rte_mk_flow_rule_components(iut_rpcs, flow_actions, NULL, NULL,
                                    &decap_rule.actions);


    TEST_STEP("Validate and create the flow rules");

    for (i = 0; i < TE_ARRAY_LEN(rules); i++)
    {
        rules[i]->flow = tapi_rte_flow_validate_and_create_rule(iut_rpcs,
                ethdev_config_pf.port_id, rules[i]->attr, rules[i]->pattern,
                rules[i]->actions);
    }

    test_rte_eth_promiscuous_disable(iut_rpcs, ethdev_config_pf.port_id,
                                     TEST_OP_REQUIRED);
    test_rte_eth_promiscuous_disable(iut_rpcs, ethdev_config_vf->port_id,
                                     TEST_OP_REQUIRED);

    for (i = 0; i < nb_pkts; i++)
    {
        TEST_STEP("Check the traffic from VF to TST");


        exchange = test_transceiver_exchange_init(tmpl, transform_tmpl, NULL,
                                                  transform_ptrn, NULL);
        TAPI_ON_JMP(goto exchange_fail);
        test_transceiver_exchange_commit(exchange, trsc_net, 1, 0, trsc_vf,
                                         1, 0);
        TAPI_JMP_POP;
        if (0)
        {
            rpc_rte_mbuf_p mbufs[BURST_SIZE] = {};
            unsigned int received;

exchange_fail:

            TEST_STEP("Check the traffic at least reached PF or representor");

            received = test_rx_burst_with_retries(iut_rpcs,
                              ethdev_config_pf.port_id,
                              0, mbufs, TE_ARRAY_LEN(mbufs), 1);

            CHECK_PACKETS_NUM(received, 1);
            TEST_VERDICT("Packet failed to reach the target destination");
        }
    }


    TEST_STEP("Destroy the flow rule and check the full traffic path again");

    for (i = 0; i < TE_ARRAY_LEN(rules); i++)
    {
        rpc_rte_flow_destroy(iut_rpcs, iut_port->if_info.if_index,
                             rules[i]->flow, NULL);
        rules[i]->flow = RPC_NULL;
    }

    test_rte_eth_promiscuous_enable(iut_rpcs, ethdev_config_pf.port_id,
                                    TEST_OP_REQUIRED);
    test_rte_eth_promiscuous_enable(iut_rpcs, ethdev_config_vf->port_id,
                                    TEST_OP_REQUIRED);

    test_transciever_simple_exchange_commit(tmpl, trsc_net, 1, 0,
                                            trsc_pf, 1, 0, NULL, NULL);
    test_transciever_simple_exchange_commit(tmpl, trsc_rep, 1, 0,
                                            trsc_vf, 1, 0, NULL, NULL);

    TEST_SUCCESS;

cleanup:

    for (i = 0; i < TE_ARRAY_LEN(rules); i++)
    {
        rpc_rte_free_flow_rule(iut_rpcs, rules[i]->attr,
                               rules[i]->pattern, rules[i]->actions);
        if (rules[i]->flow != RPC_NULL)
        {
            rpc_rte_flow_destroy(iut_rpcs, iut_port->if_info.if_index,
                                 rules[i]->flow, NULL);
        }
    }

    TEST_END;
}
/** @} */
