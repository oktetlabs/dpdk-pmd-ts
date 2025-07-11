/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Traffic filtering
 */

/** @defgroup filters-flow_rule_vlan_push Check RTE flow API rules with VLAN tag push
 * @ingroup filters
 * @{
 *
 * @objective Check that flow API VLAN tag push action is carried out correctly
 *
 * @param flow_rule_pattern     Flow rule pattern
 * @param ingress               Test flow rule ingress if @c TRUE, egress if
 *                              @c FALSE
 * @param ethertype_first       Ethertype of the first VLAN tag to be pushed
 * @param ethertype_second      Ethertype of the second VLAN tag to be pushed,
 *                              @c 0 means do not push the second VLAN tag
 * @param vlan_id_first         VLAN ID of the first VLAN tag to be pushed,
 *                              negative value means not specified
 * @param vlan_id_second        VLAN ID of the second VLAN tag to be pushed,
 *                              negative value means not specified
 * @param count                 Count packets if @c TRUE
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "filters/flow_rule_vlan_push"

#include "dpdk_pmd_test.h"
#include "te_mi_log.h"

#define TEST_DEF_QUEUE_NB 0
#define TEST_VLAN_HDR_SIZE 4

struct test_vlan_push {
    size_t n_tags;
    uint16_t ethertype[2];
    int vlan_id[2];
};

static void
inject_vlan_vid(asn_value *pdus, struct test_vlan_push *push)
{
    uint16_t vid[2] = {
        push->vlan_id[0] < 0 ? UINT16_MAX : (uint16_t)push->vlan_id[0],
        push->vlan_id[1] < 0 ? UINT16_MAX : (uint16_t)push->vlan_id[1],
    };
    uint16_t prio[2] = {UINT16_MAX, UINT16_MAX};
    uint16_t cfi[2] = {UINT16_MAX, UINT16_MAX};

    CHECK_RC(tapi_ndn_pdus_inject_vlan_tags(pdus, vid, prio, cfi,
                                            push->n_tags));
}

static asn_value *
transform_tmpl(const asn_value *tmpl_tx, void *tmpl_data)
{
    struct test_vlan_push *vlan_push = tmpl_data;
    asn_value *result;
    asn_value *pdus;

    CHECK_NOT_NULL(result = asn_copy_value(tmpl_tx));

    CHECK_NOT_NULL(pdus = asn_find_descendant(result, NULL, "pdus"));
    inject_vlan_vid(pdus, vlan_push);

    return result;
}

static asn_value *
transform_ptrn(const asn_value *ptrn_tx, void *ptrn_data)
{
    struct test_vlan_push *vlan_push = ptrn_data;
    asn_value *result;
    asn_value *pdus;

    CHECK_NOT_NULL(result = asn_copy_value(ptrn_tx));
    CHECK_NOT_NULL(pdus = asn_find_descendant(result, NULL, "0.pdus"));
    inject_vlan_vid(pdus, vlan_push);

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
    asn_value                              *flow_rule_actions;
    asn_value                              *tmpl_orig;
    rpc_rte_flow_attr_p                     attr = RPC_NULL;
    rpc_rte_flow_item_p                     rte_pattern = RPC_NULL;
    rpc_rte_flow_action_p                   rte_actions = RPC_NULL;
    rpc_rte_flow_action_p                   rte_count_action = RPC_NULL;
    rpc_rte_flow_p                          flow = RPC_NULL;
    tarpc_rte_flow_error                    error;
    te_bool                                 count;

    te_bool                                 ingress;
    struct test_vlan_push                   test_vlan_push;
    uint32_t                                result_pkt_size;

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

    struct test_transceiver                *trsc_net = NULL;
    struct test_transceiver                *trsc_dpdk = NULL;
    struct test_transceiver_exchange       *exchange = NULL;

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
    TEST_GET_BOOL_PARAM(ingress);
    TEST_GET_BOOL_PARAM(count);
    test_vlan_push.ethertype[0] = TEST_UINT_PARAM(ethertype_first);
    test_vlan_push.ethertype[1] = TEST_UINT_PARAM(ethertype_second);
    test_vlan_push.vlan_id[0] = TEST_INT_PARAM(vlan_id_first);
    test_vlan_push.vlan_id[1] = TEST_INT_PARAM(vlan_id_second);
    test_vlan_push.n_tags = (test_vlan_push.ethertype[1] != 0) ? 2 : 1;

    TEST_STEP("Initialize, configure, setup Rx/Tx queues, start the Ethernet device and wait for link up");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &ethdev_config, TEST_ETHDEV_STARTED));

    trsc_net = test_transceiver_net_init(tst_host->ta, tst_if->if_name);
    trsc_dpdk = test_transceiver_dpdk_init(iut_rpcs, iut_port->if_index,
                                           ethdev_config.mp);

    TEST_STEP("Make flow rule attributes");
    tapi_rte_flow_make_attr(iut_rpcs, 0, 0, FALSE, FALSE, TRUE, &attr);

    TEST_STEP("Make flow rule pattern by @p flow_rule_pattern");
    CHECK_RC(tapi_ndn_subst_env(flow_rule_pattern, &test_params, &env));
    test_mk_pattern_and_tmpl_by_flow_rule_pattern(iut_rpcs, flow_rule_pattern,
                                                  &rte_pattern, &tmpl_orig,
                                                  NULL);

    TEST_STEP("Make flow rule actions with VLAN push and set VLAN ID actions");
    CHECK_NOT_NULL(flow_rule_actions = asn_init_value(ndn_rte_flow_actions));

    tapi_rte_flow_add_ndn_action_of_push_vlan(flow_rule_actions, -1,
                                              test_vlan_push.ethertype[0]);
    if (test_vlan_push.vlan_id[0] >= 0)
    {
        tapi_rte_flow_add_ndn_action_of_set_vlan_vid(flow_rule_actions, -1,
                (uint16_t)test_vlan_push.vlan_id[0]);
    }
    if (test_vlan_push.ethertype[1] != 0)
    {
        tapi_rte_flow_add_ndn_action_of_push_vlan(flow_rule_actions, -1,
                                                  test_vlan_push.ethertype[1]);
        if (test_vlan_push.vlan_id[1] >= 0)
        {
            tapi_rte_flow_add_ndn_action_of_set_vlan_vid(flow_rule_actions, -1,
                    (uint16_t)test_vlan_push.vlan_id[1]);
        }
    }

    if (count)
    {
        TEST_STEP("Add flow rule action \"count\" for counter query");
        test_add_and_mk_rte_flow_action_count(0, -1, iut_rpcs,
                flow_rule_actions, &rte_count_action);
    }

    if (ingress)
    {
        tapi_rte_flow_add_ndn_action_port(NDN_FLOW_ACTION_TYPE_PORT_REPRESENTOR,
                                          iut_port->if_index, flow_rule_actions,
                                          -1);
    }
    else
    {
        tapi_rte_flow_add_ndn_action_port(NDN_FLOW_ACTION_TYPE_REPRESENTED_PORT,
                                          iut_port->if_index, flow_rule_actions,
                                          -1);
    }

    rpc_rte_mk_flow_rule_components(iut_rpcs, flow_rule_actions,
                                    NULL, NULL, &rte_actions);

    TEST_STEP("Validate and create the flow rule");
    flow = tapi_rte_flow_validate_and_create_rule(iut_rpcs, iut_port->if_index,
                                                  attr, rte_pattern,
                                                  rte_actions);

    TEST_STEP("Fill required fields in the template");
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

    CHECK_RC(test_fill_in_tmpl_req_fields(tmpl_orig, &addrs, &ifrm_addrs));
    result_pkt_size = TEST_VLAN_HDR_SIZE +
        test_get_template_packet_length(iut_rpcs, tmpl_orig, ethdev_config.mp);
    if (test_vlan_push.ethertype[1] != 0)
        result_pkt_size += TEST_VLAN_HDR_SIZE;

    if (ingress)
    {
        test_rte_eth_promiscuous_enable(iut_rpcs, iut_port->if_index,
                                        TEST_OP_REQUIRED);
    }

    TEST_STEP("Initiate a packet transfer with VLAN push transformations");
    exchange = test_transceiver_exchange_init(tmpl_orig, transform_tmpl,
                                              &test_vlan_push, transform_ptrn,
                                              &test_vlan_push);

    TEST_STEP("Send a packet, receive it the other side and match packet to transformed pattern");
    test_transceiver_exchange_commit(exchange, ingress ? trsc_net : trsc_dpdk,
                                     1, 0, ingress ? trsc_dpdk : trsc_net, 1,
                                     TEST_DEF_QUEUE_NB);

    if (count)
    {
        tarpc_rte_flow_query_data count_query;

        memset(&count_query, 0, sizeof(count_query));
        /* FIXME: get sleep time from configurator */
        VSLEEP(1, "Wait for counters to update");
        rpc_rte_flow_query(iut_rpcs, iut_port->if_index, flow,
                           rte_count_action, &count_query, &error);
        test_check_flow_query_data(&count_query, TRUE, 1, TRUE, result_pkt_size);
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
