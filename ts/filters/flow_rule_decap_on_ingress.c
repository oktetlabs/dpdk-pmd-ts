/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Traffic filtering
 */

/** @defgroup filters-flow_rule_decap_on_ingress Check RTE flow API rules implying decapsulation actions on ingress
 * @ingroup filters
 * @{
 *
 * @objective Check that flow API decap action on ingress is carried out
 *            correctly
 *
 * @param flow_rule_pattern     Flow rule pattern
 * @param tunnel_type           Type of tunnel
 * @param count                 Count decapsulated packets if @c TRUE
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "filters/flow_rule_decap_on_ingress"

#include "dpdk_pmd_test.h"

#define TEST_DEF_QUEUE_NB 0

static test_transceiver_transform_tmpl transform_tmpl;
static asn_value *
transform_tmpl(const asn_value *tmpl_tx, void *tmpl_data)
{
    UNUSED(tmpl_data);

    return test_decap_tmpl_ptrn_pdus(tmpl_tx, "pdus");
}

static test_transceiver_transform_ptrn transform_ptrn;
static asn_value *
transform_ptrn(const asn_value *ptrn_tx, void *ptrn_data)
{
    UNUSED(ptrn_data);

    return test_decap_tmpl_ptrn_pdus(ptrn_tx, "0.pdus");
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server                         *iut_rpcs = NULL;
    tapi_env_host                          *tst_host;
    const struct if_nameindex              *iut_port = NULL;
    const struct if_nameindex              *tst_if = NULL;

    tarpc_rte_eth_tunnel_type               tunnel_type;
    asn_value                              *flow_rule_pattern;
    asn_value                              *flow_rule_actions;
    asn_value                              *tmpl_encap;
    asn_value                              *tmpl_decap;
    rpc_rte_flow_attr_p                     attr = RPC_NULL;
    rpc_rte_flow_item_p                     rte_pattern = RPC_NULL;
    rpc_rte_flow_action_p                   rte_actions = RPC_NULL;
    rpc_rte_flow_action_p                   rte_count_action = RPC_NULL;
    rpc_rte_flow_p                          flow = RPC_NULL;
    tarpc_rte_flow_error                    error;
    te_bool                                 count;

    struct test_ethdev_config               ethdev_config;
    struct tarpc_ether_addr                 iut_lladdr;
    const struct sockaddr                  *iut_alien_mac = NULL;
    const struct sockaddr                  *tst_lladdr = NULL;
    const struct sockaddr                  *iut_addr = NULL;
    const struct sockaddr                  *tst_addr = NULL;
    const struct sockaddr                  *tst_alien_mac = NULL;
    struct sockaddr                        *ifrm_src_addr = NULL;
    struct sockaddr                        *ifrm_dst_addr = NULL;
    struct test_pkt_addresses               addrs;
    struct test_pkt_addresses               ifrm_addrs;
    uint32_t                                decap_pkt_size;

    struct test_transceiver                *trsc_net = NULL;
    struct test_transceiver                *trsc_dpdk = NULL;
    struct test_transceiver_exchange       *exchange = NULL;


    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);
    TEST_GET_NDN_RTE_FLOW_PATTERN(flow_rule_pattern);
    TEST_GET_TUNNEL_TYPE(tunnel_type);
    TEST_GET_LINK_ADDR(iut_alien_mac);
    TEST_GET_LINK_ADDR(tst_alien_mac);
    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_ADDR_NO_PORT(iut_addr);
    TEST_GET_ADDR_NO_PORT(tst_addr);
    TEST_GET_BOOL_PARAM(count);

    TEST_STEP("Initialize, configure, setup Rx/Tx queues, start the Ethernet device and wait for link up");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Initiate DPDK and net transceivers");
    trsc_net = test_transceiver_net_init(tst_host->ta, tst_if->if_name);
    trsc_dpdk = test_transceiver_dpdk_init(iut_rpcs, iut_port->if_index,
                                           ethdev_config.mp);

    TEST_STEP("Make flow rule attributes with \"ingress\" and \"transfer\"");
    tapi_rte_flow_make_attr(iut_rpcs, 0, 0, FALSE, FALSE, TRUE, &attr);

    TEST_STEP("Make encapsulated flow rule pattern by @p flow_rule_pattern");
    CHECK_RC(tapi_ndn_subst_env(flow_rule_pattern, &test_params, &env));
    test_mk_pattern_and_tmpl_by_flow_rule_pattern(iut_rpcs, flow_rule_pattern,
                                                  &rte_pattern, &tmpl_encap,
                                                  NULL);

    TEST_STEP("Make flow rule actions with decap, queue and count (optional)");
    CHECK_NOT_NULL(flow_rule_actions = asn_init_value(ndn_rte_flow_actions));

    tapi_rte_flow_add_ndn_action_decap(flow_rule_actions, 0, tunnel_type);

    if (count)
    {
        TEST_STEP("Add flow rule action \"count\" for counter query");
        test_add_and_mk_rte_flow_action_count(0, -1, iut_rpcs,
                flow_rule_actions, &rte_count_action);
    }

    TEST_STEP("Add flow rule fate action, \"PORT_REPRESENTOR\"");
    tapi_rte_flow_add_ndn_action_port(NDN_FLOW_ACTION_TYPE_PORT_REPRESENTOR,
                                      iut_port->if_index, flow_rule_actions,
                                      -1);

    rpc_rte_mk_flow_rule_components(iut_rpcs, flow_rule_actions,
                                    NULL, NULL, &rte_actions);

    TEST_STEP("Validate and create the flow rule");
    flow = tapi_rte_flow_validate_and_create_rule(iut_rpcs, iut_port->if_index,
                                                  attr, rte_pattern,
                                                  rte_actions);

    TEST_STEP("Fill required fields in encapsulated template");
    /* Only ipv4 */
    rpc_rte_eth_macaddr_get(iut_rpcs, iut_port->if_index, &iut_lladdr);
    CHECK_NOT_NULL(ifrm_src_addr = test_rand_ip_addr(AF_INET));
    CHECK_NOT_NULL(ifrm_dst_addr = test_rand_ip_addr(AF_INET));
    /*
     * The fields are set only if they are not specified. It is needed to create
     * a packet from the template since some fields are required.
     */
    test_set_pkt_addresses(&ifrm_addrs, (uint8_t *)tst_alien_mac->sa_data,
                           iut_lladdr.addr_bytes, ifrm_src_addr, ifrm_dst_addr);
    test_set_pkt_addresses(&addrs, (uint8_t *)tst_lladdr->sa_data,
                           (uint8_t *)iut_alien_mac->sa_data,
                           tst_addr, iut_addr);

    CHECK_RC(test_fill_in_tmpl_req_fields(tmpl_encap, &addrs, &ifrm_addrs));
    tmpl_decap = test_decap_tmpl_ptrn_pdus(tmpl_encap, "pdus");
    decap_pkt_size = test_get_template_packet_length(iut_rpcs, tmpl_decap,
                                                     ethdev_config.mp);

    TEST_STEP("Enable promiscuous mode to recieve packets with alien "
              "MAC addresses");
    test_rte_eth_promiscuous_enable(iut_rpcs, iut_port->if_index,
                                    TEST_OP_REQUIRED);

    TEST_STEP("Specify the transformations of encapsulated template and pattern");
    exchange = test_transceiver_exchange_init(tmpl_encap, transform_tmpl, NULL,
                                              transform_ptrn, NULL);

    TEST_STEP("Commit transfer of a packet from one transceiver to another "
              "and match the received packet");
    test_transceiver_exchange_commit(exchange, trsc_net, 1, 0, trsc_dpdk, 1,
                                     TEST_DEF_QUEUE_NB);

    if (count)
    {
        tarpc_rte_flow_query_data count_query;

        memset(&count_query, 0, sizeof(count_query));
        /* FIXME: get sleep time from configurator */
        SLEEP(1);
        rpc_rte_flow_query(iut_rpcs, iut_port->if_index, flow,
                           rte_count_action, &count_query, &error);
        test_check_flow_query_data(&count_query, TRUE, 1, TRUE, decap_pkt_size);
    }

    TEST_SUCCESS;

cleanup:
    if (flow != RPC_NULL)
        rpc_rte_flow_destroy(iut_rpcs, iut_port->if_index, flow, &error);

    rpc_rte_free_flow_rule(iut_rpcs, attr, rte_pattern, rte_actions);
    test_transceiver_free(trsc_net);
    test_transceiver_free(trsc_dpdk);
    test_transceiver_exchange_free(exchange);


    TEST_END;
}
/** @} */
