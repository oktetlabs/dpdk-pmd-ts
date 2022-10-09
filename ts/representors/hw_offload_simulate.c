/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK Port Representors Test Suite
 * Port representors use cases
 */

/** @defgroup representors-hw_offload_simulate Simulate HW offload
 * @ingroup representors
 * @{
 *
 * @objective Make sure that offloading traffic to a virtual function
 *            works with RTE flow API
 *
 * @param flow_rule_pattern     Flow Rule pattern
 * @param count                 Count offloaded packets if @c TRUE
 * @param use_dpdk_driver       Use DPDK driver on VF if @c TRUE,
 *                              use kernel driver otherwise
 * @param nb_pkts               Number of offloaded packets to send
 * @param iut_client            @c TRUE - IUT is client, @c FALSE - IUT is server
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "representors/hw_offload_simulate"

#include "dpdk_pmd_test.h"

#define TEST_N_FLOWS 2

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

    asn_value                              *flow_actions;
    asn_value                              *tmpl;
    rpc_rte_flow_attr_p                     attr = RPC_NULL;
    rpc_rte_flow_p                          flow = RPC_NULL;
    rpc_rte_flow_item_p                     rte_pattern = RPC_NULL;
    asn_value                              *flow_items;
    rpc_rte_flow_action_p                   rte_actions = RPC_NULL;
    rpc_rte_flow_action_p                   rte_count_action = RPC_NULL;
    tarpc_rte_flow_error                    error;

    struct test_transceiver                *trsc_net = NULL;
    struct test_transceiver                *trsc_pf = NULL;
    struct test_transceiver                *trsc_vf = NULL;
    struct test_transceiver                *trsc_rep = NULL;
    unsigned int                            n_vfs;
    char                                  **vf_addrs;
    unsigned int                           *vf_ids;
    char                                   *vf_if;
    unsigned int                            i;
    char                                   *pci_oid;

    asn_value                              *flow_rule_pattern;
    unsigned int                            nb_pkts;
    te_bool                                 count;
    te_bool                                 use_dpdk_driver;
    te_bool                                 iut_client;

    const struct sockaddr                  *tst_lladdr = NULL;
    struct tarpc_ether_addr                 vf_lladdr = {
        .addr_bytes = {0x00, 0x01, 0x52, 0x63, 0x54, 0x45},
    };
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

    TEST_GET_NDN_RTE_FLOW_PATTERN(flow_rule_pattern);
    TEST_GET_BOOL_PARAM(use_dpdk_driver);
    TEST_GET_BOOL_PARAM(count);
    TEST_GET_BOOL_PARAM(iut_client);
    TEST_GET_UINT_PARAM(nb_pkts);

    test_prepare_config_def_mk(&env, iut_rpcs, &iut_port->if_info,
                               &ethdev_config_pf);
    test_get_vf_pci_addrs_by_node(tapi_env_get_if_net_node(iut_port),
                                  &n_vfs, &vf_addrs, &vf_ids);

    TEST_STEP("Hotplug the representor of the VF");

    test_hotplug_reps(iut_rpcs, &env, tapi_env_get_if_net_node(iut_port),
                      1, vf_ids, &ethdev_config_rep);


    TEST_STEP("Bind kernel or DPDK driver on a VF");


    CHECK_RC_VERDICT(tapi_cfg_pci_bind_ta_driver_on_device(iut_rpcs->ta,
                        use_dpdk_driver ? NET_DRIVER_TYPE_DPDK :
                        NET_DRIVER_TYPE_NET, vf_addrs[0]),
                     "Failed to bind VF to %s driver",
                     use_dpdk_driver ? "DPDK" : "net");

    if (use_dpdk_driver)
    {
        TEST_STEP("Hotplug VF");

        test_hotplug_vfs_by_ids(iut_rpcs, &env,
                                tapi_env_get_if_net_node(iut_port),
                                1, vf_ids, &ethdev_config_vf);
        rpc_rte_eth_macaddr_get(iut_rpcs, ethdev_config_vf->port_id,
                                &vf_lladdr);
    }
    else
    {
        struct sockaddr mac = { .sa_family = AF_LOCAL, };
        char *rsrc_val;


        TEST_STEP("Add VF's kernel interface and bring it up");

        memcpy(mac.sa_data, vf_lladdr.addr_bytes, ETHER_ADDR_LEN);

        CHECK_RC(tapi_cfg_pci_oid_by_addr(iut_rpcs->ta, vf_addrs[0], &pci_oid));
        CHECK_RC(tapi_cfg_pci_get_net_if(pci_oid, &vf_if));
        CHECK_NOT_NULL(rsrc_val = te_string_fmt("/agent:%s/interface:%s",
                                                iut_rpcs->ta, vf_if));
        CHECK_RC(cfg_add_instance_fmt(NULL, CFG_VAL(STRING, rsrc_val),
                                      "/agent:%s/rsrc:%s",
                                      iut_rpcs->ta, vf_if));
        CHECK_RC(cfg_set_instance_fmt(CFG_VAL(ADDRESS, &mac),
                                      "/agent:%s/interface:%s/link_addr:",
                                      iut_rpcs->ta, vf_if));
        CHECK_RC(cfg_set_instance_fmt(CFG_VAL(INTEGER, 1),
                                      "/agent:%s/interface:%s/status:",
                                      iut_rpcs->ta, vf_if));
    }


    TEST_STEP("Create a big enough mempool for PF, VF and representor ports");

    mp = test_rte_pktmbuf_pool_create(iut_rpcs, TEST_PKTS_MEMPOOL_NAME,
                                     TEST_RTE_MEMPOOL_DEF_SIZE,
                                     TEST_RTE_MEMPOOL_DEF_CACHE,
                                     TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                     TEST_RTE_MEMPOOL_DEF_DATA_ROOM,
                                     rpc_rte_eth_dev_socket_id(iut_rpcs,
                                            iut_port->if_info.if_index));


    TEST_STEP("Make flow rule pattern and template");

    CHECK_RC(tapi_ndn_subst_env(flow_rule_pattern, &test_params, &env));
    test_mk_pattern_and_tmpl_by_flow_rule_pattern(iut_rpcs, flow_rule_pattern,
                                                  &rte_pattern, &tmpl, NULL);

    CHECK_NOT_NULL(flow_items = asn_init_value(ndn_rte_flow_items));

    if (iut_client)
    {
        tapi_rte_flow_add_ndn_item_port(NDN_FLOW_ITEM_TYPE_PORT_REPRESENTOR,
                                        ethdev_config_rep->port_id,
                                        flow_items, -1);
    }
    else
    {
        tapi_rte_flow_add_ndn_item_port(NDN_FLOW_ITEM_TYPE_REPRESENTED_PORT,
                                        ethdev_config_pf.port_id,
                                        flow_items, -1);
    }
    rpc_rte_insert_flow_rule_items(iut_rpcs, &rte_pattern, flow_items, 0);

    test_set_pkt_addresses(&addrs,
                           iut_client ? vf_lladdr.addr_bytes :
                           (uint8_t *)tst_lladdr->sa_data,
                           iut_client ?  (uint8_t *)tst_lladdr->sa_data :
                           vf_lladdr.addr_bytes,
                           iut_client ? iut_addr : tst_addr,
                           iut_client ? tst_addr : iut_addr);
    test_set_pkt_addresses(&ifrm_addrs, NULL, NULL, NULL, NULL);
    CHECK_RC(test_fill_in_tmpl_req_fields(tmpl, &addrs, &ifrm_addrs));


    TEST_STEP("Start PF, VF and representor ports");

    ethdev_config_pf.mp = ethdev_config_rep->mp = mp;
    CHECK_RC(test_prepare_ethdev(&ethdev_config_pf, TEST_ETHDEV_STARTED));
    CHECK_RC(test_prepare_ethdev(ethdev_config_rep, TEST_ETHDEV_STARTED));
    if (use_dpdk_driver)
    {
        ethdev_config_vf->mp = mp;
        CHECK_RC(test_prepare_ethdev(ethdev_config_vf, TEST_ETHDEV_STARTED));
    }


    TEST_STEP("Check full traffic path (IUT - representor - TST) without flow rule");

    trsc_net = test_transceiver_net_init(tst_host->ta, tst_if->if_name);
    trsc_pf = test_transceiver_dpdk_init(iut_rpcs, ethdev_config_pf.port_id,
                                           ethdev_config_pf.mp);
    trsc_rep = test_transceiver_dpdk_init(iut_rpcs,
                                          ethdev_config_rep->port_id,
                                          ethdev_config_rep->mp);
    trsc_vf = use_dpdk_driver ?
        test_transceiver_dpdk_init(iut_rpcs, ethdev_config_vf->port_id,
                                   ethdev_config_vf->mp) :
        test_transceiver_net_init(iut_rpcs->ta, vf_if);

    test_rte_eth_promiscuous_enable(iut_rpcs, ethdev_config_pf.port_id,
                                    TEST_OP_REQUIRED);

    test_transciever_simple_exchange_commit(tmpl,
                                    iut_client ? trsc_vf : trsc_net, 1, 0,
                                    iut_client ? trsc_rep : trsc_pf, 1, 0,
                                    NULL, NULL);
    test_transciever_simple_exchange_commit(tmpl,
                                    iut_client ? trsc_pf : trsc_rep, 1, 0,
                                    iut_client ? trsc_net : trsc_vf, 1, 0,
                                    NULL, NULL);


    TEST_STEP("Make flow rule attributes with transfer and ingress or egress "
              "depending on traffic direction");

    tapi_rte_flow_make_attr(iut_rpcs, 0, 0, FALSE, FALSE, TRUE, &attr);


    TEST_STEP("Direct traffic to the VF (when receiving on IUT) "
              "or to the physical port (when receiving on TST)");

    CHECK_NOT_NULL(flow_actions = asn_init_value(ndn_rte_flow_actions));
    tapi_rte_flow_add_ndn_action_port(NDN_FLOW_ACTION_TYPE_REPRESENTED_PORT,
                                      iut_client ?
                                      ethdev_config_pf.port_id :
                                      ethdev_config_rep->port_id,
                                      flow_actions, -1);
    if (count)
    {
        TEST_STEP("Add flow rule action \"count\" for counter query");

        test_add_and_mk_rte_flow_action_count(0, 0, iut_rpcs,
                flow_actions, &rte_count_action);
    }
    rpc_rte_mk_flow_rule_components(iut_rpcs, flow_actions, NULL, NULL,
                                    &rte_actions);


    TEST_STEP("Validate and create the flow rule");

    flow = tapi_rte_flow_validate_and_create_rule(iut_rpcs,
                ethdev_config_pf.port_id, attr, rte_pattern, rte_actions);
    test_rte_eth_promiscuous_disable(iut_rpcs, ethdev_config_pf.port_id,
                                     TEST_OP_REQUIRED);

    for (i = 0; i < nb_pkts; i++)
    {
        TEST_STEP("Check the traffic from VF to TST");

        TAPI_ON_JMP(goto exchange_fail);
        test_transciever_simple_exchange_commit(tmpl,
                                        iut_client ? trsc_vf : trsc_net, 1, 0,
                                        iut_client ? trsc_net : trsc_vf, 1, 0,
                                        NULL, NULL);
        TAPI_JMP_POP;
        if (0)
        {
            rpc_rte_mbuf_p mbufs[BURST_SIZE] = {};
            unsigned int received;

exchange_fail:

            TEST_STEP("Check the traffic at least reached PF or representor");

            received = test_rx_burst_with_retries(iut_rpcs,
                              iut_client ? ethdev_config_rep->port_id :
                              ethdev_config_pf.port_id,
                              0, mbufs, TE_ARRAY_LEN(mbufs), 1);

            CHECK_PACKETS_NUM(received, 1);
            TEST_VERDICT("Packet failed to reach the target destination");
        }
        else if (count)
        {
            tarpc_rte_flow_query_data count_query;

            memset(&count_query, 0, sizeof(count_query));
            count_query.tarpc_rte_flow_query_data_u.count.reset = 1;
            /* FIXME: get sleep time from configurator */
            VSLEEP(1, "Wait for counters to update");
            rpc_rte_flow_query(iut_rpcs, iut_port->if_info.if_index, flow,
                               rte_count_action, &count_query, &error);
            test_check_flow_query_data(&count_query, TRUE, 1, FALSE, 0);
        }
    }


    TEST_STEP("Destroy the flow rule and check the full traffic path again");

    rpc_rte_flow_destroy(iut_rpcs, iut_port->if_info.if_index, flow, NULL);
    flow = RPC_NULL;

    test_rte_eth_promiscuous_enable(iut_rpcs, ethdev_config_pf.port_id,
                                    TEST_OP_REQUIRED);

    test_transciever_simple_exchange_commit(tmpl,
                                    iut_client ? trsc_vf : trsc_net, 1, 0,
                                    iut_client ? trsc_rep : trsc_pf, 1, 0,
                                    NULL, NULL);
    test_transciever_simple_exchange_commit(tmpl,
                                    iut_client ? trsc_pf : trsc_rep, 1, 0,
                                    iut_client ? trsc_net : trsc_vf, 1, 0,
                                    NULL, NULL);

    TEST_SUCCESS;

cleanup:
    rpc_rte_free_flow_rule(iut_rpcs, attr, rte_pattern, rte_actions);

    if (flow != RPC_NULL)
    {
        rpc_rte_flow_destroy(iut_rpcs, iut_port->if_info.if_index,
                             flow, NULL);
    }
    test_transceiver_free(trsc_net);
    test_transceiver_free(trsc_pf);
    test_transceiver_free(trsc_vf);
    test_transceiver_free(trsc_rep);

    TEST_END;
}
/** @} */
