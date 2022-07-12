/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK Port Representors Test Suite
 * Port representors use cases
 */

/** @defgroup representors-flow_rule_port_id Test RTE flow API PORT ID action
 * @ingroup representors
 * @{
 *
 * @objective Make sure that RTE flow API PORT ID action
 *            is carried out correctly
 *
 * @param tmpl              Packet template
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "representors/flow_rule_port_id"

#include "dpdk_pmd_test.h"

#define TEST_N_FLOWS 2

int
main(int argc, char *argv[])
{
    tapi_env_host                          *tst_host;
    const struct if_nameindex              *tst_if = NULL;
    rcf_rpc_server                         *iut_rpcs = NULL;
    const tapi_env_if                      *iut_port = NULL;
    struct test_ethdev_config               ethdev_config;
    struct test_ethdev_config              *ethdev_config_vf;
    struct test_ethdev_config              *ethdev_config_rep;
    rpc_rte_mempool_p                       mp;

    asn_value                              *flow_actions[TEST_N_FLOWS];
    asn_value                              *flow_items[TEST_N_FLOWS];
    rpc_rte_flow_attr_p                     attr = RPC_NULL;
    rpc_rte_flow_item_p                     patterns[TEST_N_FLOWS] = {RPC_NULL};
    rpc_rte_flow_action_p                   actions[TEST_N_FLOWS] = {RPC_NULL};
    rpc_rte_flow_p                          flow[TEST_N_FLOWS] = {RPC_NULL};
    asn_value                              *tmpl;

    struct test_transceiver                *trsc_net = NULL;
    struct test_transceiver                *trsc_dpdk = NULL;
    struct test_transceiver                *trsc_vf = NULL;
    struct test_transceiver                *trsc_vf_net = NULL;
    struct tarpc_ether_addr                 vf_mac;
    struct tarpc_ether_addr                 iut_mac;
    const struct sockaddr                  *tst_lladdr = NULL;
    unsigned int                            n_vfs;
    char                                  **vf_addrs;
    unsigned int                           *vf_ids;
    char                                   *vf_if;
    unsigned int                            i;
    char                                   *pci_oid;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_ENV_IF(iut_port);
    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);

    test_prepare_config_def_mk(&env, iut_rpcs, &iut_port->if_info,
                               &ethdev_config);

    TEST_STEP("Bind kernel driver on a VF and get its interface");
    test_get_vf_pci_addrs_by_node(tapi_env_get_if_net_node(iut_port),
                                  &n_vfs, &vf_addrs, &vf_ids);

    CHECK_RC_VERDICT(tapi_cfg_pci_bind_ta_driver_on_device(iut_rpcs->ta,
                        NET_DRIVER_TYPE_NET, vf_addrs[0]),
                     "Failed to bind VF to net driver");
    CHECK_RC(tapi_cfg_pci_oid_by_addr(iut_rpcs->ta, vf_addrs[0], &pci_oid));
    CHECK_RC(tapi_cfg_pci_get_net_if(pci_oid, &vf_if));

    TEST_STEP("Create a big enough mempool for PF, VF and representor ports");
    mp = rpc_rte_pktmbuf_pool_create(iut_rpcs, TEST_PKTS_MEMPOOL_NAME,
                                     TEST_RTE_MEMPOOL_DEF_SIZE,
                                     TEST_RTE_MEMPOOL_DEF_CACHE,
                                     TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                     TEST_RTE_MEMPOOL_DEF_DATA_ROOM,
                                     rpc_rte_eth_dev_socket_id(iut_rpcs,
                                            iut_port->if_info.if_index));

    TEST_STEP("Hotplug the representor of the VF");
    test_hotplug_reps(iut_rpcs, &env, tapi_env_get_if_net_node(iut_port),
                      1, vf_ids, &ethdev_config_rep);

    TEST_STEP("Start PF, VF and representor ports");
    ethdev_config.mp = ethdev_config_rep->mp = mp;
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));
    CHECK_RC(test_prepare_ethdev(ethdev_config_rep, TEST_ETHDEV_STARTED));

    TEST_STEP("Check that the traffic passes from TST to IUT PF");
    trsc_net = test_transceiver_net_init(tst_host->ta, tst_if->if_name);
    trsc_dpdk = test_transceiver_dpdk_init(iut_rpcs, ethdev_config.port_id,
                                           ethdev_config.mp);
    trsc_vf_net = test_transceiver_net_init(iut_rpcs->ta, vf_if);

    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    rpc_rte_eth_macaddr_get(iut_rpcs, ethdev_config.port_id, &iut_mac);
    test_pdus_set_eth_src_dst(tmpl, "pdus",
                              (const uint8_t *)tst_lladdr->sa_data,
                              iut_mac.addr_bytes);
    test_transciever_simple_exchange_commit(tmpl, trsc_net, 1, 0,
                                            trsc_dpdk, 1, 0, NULL, NULL);

    TEST_STEP("Make flow rule patterns matching representor and PF PORT IDs");
    CHECK_NOT_NULL(flow_items[0] = asn_init_value(ndn_rte_flow_items));
    CHECK_NOT_NULL(flow_items[1] = asn_init_value(ndn_rte_flow_items));
    tapi_rte_flow_add_ndn_item_port(NDN_FLOW_ITEM_TYPE_PORT_REPRESENTOR,
                                    ethdev_config.port_id,
                                    flow_items[0], -1);
    tapi_rte_flow_add_ndn_item_port(NDN_FLOW_ITEM_TYPE_PORT_REPRESENTOR,
                                    ethdev_config_rep->port_id,
                                    flow_items[1], -1);
    rpc_rte_insert_flow_rule_items(iut_rpcs, &patterns[0], flow_items[0], -1);
    rpc_rte_insert_flow_rule_items(iut_rpcs, &patterns[1], flow_items[1], -1);

    TEST_STEP("Make flow rule attributes with ingress and transfer");
    tapi_rte_flow_make_attr(iut_rpcs, 0, 0, TRUE, FALSE, TRUE, &attr);

    TEST_STEP("Make flow rule actions with PORT ID of the representor and PF");
    CHECK_NOT_NULL(flow_actions[0] = asn_init_value(ndn_rte_flow_actions));
    CHECK_NOT_NULL(flow_actions[1] = asn_init_value(ndn_rte_flow_actions));
    tapi_rte_flow_add_ndn_action_port(NDN_FLOW_ACTION_TYPE_PORT_REPRESENTOR,
                                      ethdev_config_rep->port_id,
                                      flow_actions[0], -1);
    tapi_rte_flow_add_ndn_action_port(NDN_FLOW_ACTION_TYPE_PORT_REPRESENTOR,
                                      ethdev_config.port_id,
                                      flow_actions[1], -1);
    rpc_rte_mk_flow_rule_components(iut_rpcs, flow_actions[0], NULL, NULL,
                                    &actions[0]);
    rpc_rte_mk_flow_rule_components(iut_rpcs, flow_actions[1],
                                    NULL, NULL, &actions[1]);

    TEST_STEP("Validate and create the flow rules to pass traffic "
              "from the IUT VF to TST and vice versa");
    flow[0] = tapi_rte_flow_validate_and_create_rule(iut_rpcs,
                 ethdev_config.port_id, attr, patterns[0], actions[0]);
    flow[1] = tapi_rte_flow_validate_and_create_rule(iut_rpcs,
                 ethdev_config.port_id, attr, patterns[1], actions[1]);

    TEST_STEP("Check traffic between TST and the IUT VF (kernel driver)");
    test_transciever_simple_exchange_commit(tmpl, trsc_net, 1, 0,
                                            trsc_vf_net, 1, 0, NULL, NULL);
    test_transciever_simple_exchange_commit(tmpl, trsc_vf_net, 1, 0,
                                            trsc_net, 1, 0, NULL, NULL);

    TEST_STEP("Bind DPDK driver on the VF");
    CHECK_RC(tapi_cfg_pci_bind_ta_driver_on_device(iut_rpcs->ta,
                    NET_DRIVER_TYPE_DPDK, vf_addrs[0]));
    TEST_STEP("Hotplug the VF");
    test_hotplug_vfs_by_ids(iut_rpcs, &env, tapi_env_get_if_net_node(iut_port),
                            1, vf_ids, &ethdev_config_vf);

    TEST_STEP("Start the VF port");
    ethdev_config_vf->mp = mp;
    CHECK_RC(test_prepare_ethdev(ethdev_config_vf, TEST_ETHDEV_STARTED));

    TEST_STEP("Set the VF MAC address in the packet template");
    rpc_rte_eth_macaddr_get(iut_rpcs, ethdev_config_vf->port_id, &vf_mac);
    test_pdus_set_eth_src_dst(tmpl, "pdus", NULL, vf_mac.addr_bytes);

    TEST_STEP("Check traffic between TST and the IUT VF (DPDK driver)");
    trsc_vf = test_transceiver_dpdk_init(iut_rpcs, ethdev_config_vf->port_id,
                                         ethdev_config_vf->mp);

    test_transciever_simple_exchange_commit(tmpl, trsc_net, 1, 0,
                                            trsc_vf, 1, 0, NULL, NULL);
    test_transciever_simple_exchange_commit(tmpl, trsc_vf, 1, 0,
                                            trsc_net, 1, 0, NULL, NULL);
    test_transciever_simple_exchange_commit(tmpl, trsc_net, 1, 0,
                                            trsc_dpdk, 0, 0, NULL, NULL);

    TEST_SUCCESS;

cleanup:
    rpc_rte_free_flow_rule(iut_rpcs, attr, RPC_NULL, RPC_NULL);

    for (i = 0; i < TEST_N_FLOWS; i++)
    {
        if (flow[i] != RPC_NULL)
        {
            rpc_rte_flow_destroy(iut_rpcs, iut_port->if_info.if_index,
                                 flow[i], NULL);
        }
        rpc_rte_free_flow_rule(iut_rpcs, RPC_NULL, patterns[i], actions[i]);
    }

    TEST_END;
}
/** @} */
