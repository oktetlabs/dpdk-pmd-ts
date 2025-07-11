/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Traffic filtering
 */

/** @defgroup filters-flow_rule_encap_on_egress Check RTE flow API rules implying encapsulation actions on egress
 * @ingroup filters
 * @{
 *
 * @objective Check that flow API encap action on egress is carried out correctly
 *
 * @param flow_rule_pattern     Flow rule pattern
 * @param encap_header          Header of encapsulated packet
 * @param tunnel_type           Type of tunnel
 * @param count                 Count encapsulated packets if @c TRUE
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "filters/flow_rule_encap_on_egress"

#include "dpdk_pmd_test.h"

struct test_transform_data {
    const asn_value *encap_hdr;
    rcf_rpc_server *rpcs;
};

static test_transceiver_transform_ptrn transform_tmpl;
static asn_value *
transform_tmpl(const asn_value *tmpl_tx, void *tmpl_data)
{
    struct test_transform_data *data = tmpl_data;
    asn_value *result = NULL;

    result = test_concatenate_tmpl_ptrn_pdus(data->encap_hdr, tmpl_tx, "pdus");

    return result;
}

static test_transceiver_transform_ptrn transform_ptrn;
static asn_value *
transform_ptrn(const asn_value *ptrn_tx, void *ptrn_data)
{
    const asn_value *encap_hdr = ptrn_data;

    return test_concatenate_tmpl_ptrn_pdus(encap_hdr, ptrn_tx, "0.pdus");
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
    asn_value                              *encap_header;
    asn_value                              *pattern_encap_hdr;
    asn_value                              *tmpl_encap_hdr;
    asn_value                              *tmpl_nonencap;
    rpc_rte_flow_attr_p                     attr = RPC_NULL;
    rpc_rte_flow_item_p                     rte_pattern = RPC_NULL;
    rpc_rte_flow_action_p                   rte_actions = RPC_NULL;
    rpc_rte_flow_action_p                   rte_count_action = RPC_NULL;
    rpc_rte_flow_p                          flow = RPC_NULL;
    tarpc_rte_flow_error                    error;
    te_bool                                 count;

    uint32_t                                match_fields;
    uint32_t                                pkt_size;

    struct test_ethdev_config               ethdev_config;
    const struct sockaddr                  *iut_alien_mac = NULL;
    const struct sockaddr                  *tst_lladdr = NULL;
    const struct sockaddr                  *iut_addr = NULL;
    const struct sockaddr                  *tst_addr = NULL;
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
    TEST_GET_NDN_RTE_FLOW_PATTERN(encap_header);
    TEST_GET_TUNNEL_TYPE(tunnel_type);
    TEST_GET_LINK_ADDR(iut_alien_mac);
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

    TEST_STEP("Make flow rule attributes with \"egress\" and \"transfer\"");
    tapi_rte_flow_make_attr(iut_rpcs, 0, 0, FALSE, FALSE, TRUE, &attr);

    TEST_STEP("Make flow rule pattern by @p flow_rule_pattern");
    CHECK_RC(tapi_ndn_subst_env(flow_rule_pattern, &test_params, &env));
    CHECK_RC(tapi_ndn_subst_env(encap_header, &test_params, &env));
    test_mk_pattern_and_tmpl_by_flow_rule_pattern(iut_rpcs, flow_rule_pattern,
                                                  &rte_pattern, &tmpl_nonencap,
                                                  &match_fields);

    TEST_STEP("Make flow rule actions with encapsulation action");
    CHECK_NOT_NULL(flow_rule_actions = asn_init_value(ndn_rte_flow_actions));
    if (count)
    {
        TEST_STEP("Add flow rule action \"count\" for counter query");
        test_add_and_mk_rte_flow_action_count(0, 0, iut_rpcs,
                flow_rule_actions, &rte_count_action);
    }
    tapi_rte_flow_add_ndn_action_encap(flow_rule_actions, -1, tunnel_type,
                                       encap_header);

    TEST_STEP("Add flow rule fate action, \"REPRESENTED_PORT\"");
    tapi_rte_flow_add_ndn_action_port(NDN_FLOW_ACTION_TYPE_REPRESENTED_PORT,
                                      iut_port->if_index, flow_rule_actions,
                                      -1);

    rpc_rte_mk_flow_rule_components(iut_rpcs, flow_rule_actions,
                                    NULL, NULL, &rte_actions);

    TEST_STEP("Validate and create the flow rule");
    flow = tapi_rte_flow_validate_and_create_rule(iut_rpcs, iut_port->if_index,
                                                  attr, rte_pattern,
                                                  rte_actions);

    TEST_STEP("Fill required fields in non-encapsulated template");
    /*
     * The fields are set only if they are not specified. It is needed to create
     * a packet from the template since some fields are required.
     * Inner frame addresses are not set since flow rule pattern cannot contain
     * inner frame.
     */
    test_set_pkt_addresses(&ifrm_addrs, NULL, NULL, NULL, NULL);
    test_set_pkt_addresses(&addrs, (uint8_t *)tst_lladdr->sa_data,
                           (uint8_t *)iut_alien_mac->sa_data,
                           tst_addr, iut_addr);

    CHECK_RC(test_fill_in_tmpl_req_fields(tmpl_nonencap, &addrs, &ifrm_addrs));

    TEST_STEP("Make template and pattern with encapsulation header");
    CHECK_RC(test_mk_template_get_match_fields_from_flow_ndn(encap_header,
                                                             &tmpl_encap_hdr,
                                                             NULL));
    pattern_encap_hdr = tapi_tad_mk_pattern_from_template(tmpl_encap_hdr);
    if (pattern_encap_hdr == NULL)
        TEST_VERDICT("Failed to create pattern");

    TEST_STEP("Specify the transformations of non-encapsulated template and pattern");
    {
        struct test_transform_data data = {
            tmpl_encap_hdr, iut_rpcs,
        };

        exchange = test_transceiver_exchange_init(tmpl_nonencap,
                                                  transform_tmpl, &data,
                                                  transform_ptrn,
                                                  pattern_encap_hdr);
    }

    TEST_STEP("Commit transfer of a packet from one transceiver to another "
              "and match the received packet");
    test_transceiver_exchange_commit(exchange, trsc_dpdk, 1, 0,
                                     trsc_net, 1, 0);

    if (count)
    {
        tarpc_rte_flow_query_data count_query;

        pkt_size = test_get_template_packet_length(iut_rpcs, tmpl_nonencap,
                                                   ethdev_config.mp);

        memset(&count_query, 0, sizeof(count_query));
        /* FIXME: get sleep time from configurator */
        VSLEEP(1, "Wait for counters to update");
        rpc_rte_flow_query(iut_rpcs, iut_port->if_index, flow,
                           rte_count_action, &count_query, &error);
        test_check_flow_query_data(&count_query, TRUE, 1, TRUE, pkt_size);
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
