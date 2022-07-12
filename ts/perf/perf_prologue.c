/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief Performance Test Suite prologue
 *
 * DPDK PMD Performance Test Suite
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 */

#ifndef DOXYGEN_TEST_SPEC

/** Logging subsystem entity name */
#define TE_TEST_NAME    "perf_prologue"

#include "dpdk_pmd_test.h"

#include "tapi_cfg_cpu.h"
#include "tapi_cfg_sys.h"
#include "tapi_test.h"
#include "rpc_dpdk_offloads.h"

static te_errno
bind_dpdk_driver_on_tst_agent(cfg_net_t *net, cfg_net_node_t *node,
                              const char *oid_str, cfg_oid *oid, void *cookie)
{
    cfg_handle *tst_pci_instances = NULL;
    unsigned int n_tst_pci_instances = 0;
    char *tst_dpdk_driver = NULL;
    char *pci_instance = NULL;
    char *pci_device = NULL;
    const char *agent;
    te_errno rc = 0;

    UNUSED(oid_str);
    UNUSED(cookie);
    UNUSED(net);

    if (strcmp(cfg_oid_inst_subid(oid, 1), "agent") != 0 ||
        strcmp(cfg_oid_inst_subid(oid, 2), "interface") != 0)
    {
        goto out;
    }

    agent = CFG_OID_GET_INST_NAME(oid, 1);

    switch (node->type)
    {
        case NET_NODE_TYPE_AGENT:
            break;
        case NET_NODE_TYPE_NUT:
            goto out;
        default:
            ERROR("Invalid net node type for agent %s", agent);
            rc = TE_RC(TE_CONF_API, TE_EINVAL);
            goto out;
    }

    rc = cfg_find_pattern_fmt(&n_tst_pci_instances, &tst_pci_instances,
                "/agent:%s/hardware:/pci:/vendor:*/device:*/instance:*",
                agent);
    if (rc != 0)
    {
        ERROR("Failed to find pci instances on TST agent");
        goto out;
    }

    if (n_tst_pci_instances != 1)
    {
        ERROR("Failed to bind DPDK driver on agent with %u PCI instances",
              n_tst_pci_instances);
        rc = TE_RC(TE_CONF_API, TE_EFAIL);
        goto out;
    }

    rc = cfg_get_instance_fmt(NULL, &tst_dpdk_driver,
                              "/local:%s/dpdk_driver:", agent);
    if (rc != 0)
    {
        ERROR("Failed to get DPDK driver of TST agent");
        goto out;
    }

    rc = cfg_get_oid_str(tst_pci_instances[0], &pci_instance);
    if (rc != 0)
    {
        ERROR("Failed to get PCI instance OID string");
        goto out;
    }

    rc = cfg_get_instance_str(NULL, &pci_device, pci_instance);
    if (rc != 0)
    {
        ERROR("Failed to get PCI device of TST agent");
        goto out;
    }

    rc = cfg_set_instance_fmt(CFG_VAL(STRING, tst_dpdk_driver), "%s/driver:",
                              pci_device);
    if (rc != 0)
    {
        ERROR("Failed to bind DPDK driver on TST agent");
        goto out;
    }

    rc = cfg_set_instance(node->handle, CFG_VAL(STRING, pci_instance));
    if (rc != 0)
    {
        ERROR("Failed to set PCI instance on TST agent network node");
        goto out;
    }

out:
    free(tst_pci_instances);
    free(tst_dpdk_driver);
    free(pci_instance);
    free(pci_device);

    return rc;
}

/**
 * @retval EXIT_SUCCESS     success
 * @retval EXIT_FAILURE     failure
 */
int
main(int argc, char **argv)
{
    const char *extra_sfc_devargs = "stats_update_period_ms=0";
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_port = NULL;
    struct tarpc_rte_eth_dev_info dev_info;
    const char *iut_job_control_rpcs = "iut_jobs_ctrl";
    struct tarpc_ether_addr iut_mac;
    char *iut_mac_str = NULL;
    tapi_cpu_index_t *indices = NULL;
    size_t nb_cpus;
    unsigned int i;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_port);

    /*
     * Make stats of SFC (Xilinx) NICs more consistent by removing update period
     */
    CHECK_RC(cfg_add_instance_str("/local:/dpdk:/dev_args:pci_fn:10ee::", NULL,
                                  CFG_VAL(STRING, extra_sfc_devargs)));
    CHECK_RC(cfg_add_instance_str("/local:/dpdk:/dev_args:pci_fn:1924::", NULL,
                                  CFG_VAL(STRING, extra_sfc_devargs)));

    memset(&dev_info, 0, sizeof(dev_info));
    rpc_rte_eth_dev_info_get(iut_rpcs, iut_port->if_index, &dev_info);

    CHECK_RC(test_add_pci_fn_prop(iut_rpcs, iut_port,
                                  "max_tx_queues", dev_info.max_tx_queues));
    CHECK_RC(test_add_pci_fn_prop(iut_rpcs, iut_port,
                                  "max_rx_queues", dev_info.max_rx_queues));

    CHECK_RC(test_add_pci_fn_prop(iut_rpcs, iut_port,
                                  "min_mtu", dev_info.min_mtu));
    CHECK_RC(test_add_pci_fn_prop(iut_rpcs, iut_port,
                                  "max_mtu", dev_info.max_mtu));

    for (i = 0; i < rpc_dpdk_tx_offloads_num; i++)
    {
        if (dev_info.tx_offload_capa & (1ULL << rpc_dpdk_tx_offloads[i].bit))
        {
            CHECK_RC(cfg_add_instance_fmt(NULL, CFG_VAL(NONE, NULL),
                        "/local:/dpdk:/offloads:/dev:/tx:/supported:%s",
                        rpc_dpdk_tx_offloads[i].name));
        }
    }

    for (i = 0; i < rpc_dpdk_rx_offloads_num; i++)
    {
        if (dev_info.rx_offload_capa & (1ULL << rpc_dpdk_rx_offloads[i].bit))
        {
            CHECK_RC(cfg_add_instance_fmt(NULL, CFG_VAL(NONE, NULL),
                        "/local:/dpdk:/offloads:/dev:/rx:/supported:%s",
                        rpc_dpdk_rx_offloads[i].name));
        }
    }

    rpc_rte_eth_macaddr_get(iut_rpcs, iut_port->if_index, &iut_mac);
    te_asprintf(&iut_mac_str, TE_PRINTF_MAC_FMT,
                TE_PRINTF_MAC_VAL(iut_mac.addr_bytes));
    CHECK_NOT_NULL(iut_mac_str);
    CHECK_RC(cfg_add_instance_fmt(NULL, CFG_VAL(STRING, iut_mac_str),
                                  "/local:/dpdk:/mac:%s", TEST_ENV_IUT_PORT));

    /* Deinitialize EAL on iut_rpcs */
    CHECK_RC(tapi_rte_eal_fini(&env, iut_rpcs));

    /* Set RPC provider to default value on IUT (do not use DPDK RPC server) */
    CHECK_RC(cfg_set_instance_fmt(CFG_VAL(STRING, "ta_rpcs"),
                                  "/agent:%s/rpcprovider:", iut_rpcs->ta));

    /* Dismantle default CPU pre-allocation made by 'ts/prologue' */
    CHECK_RC(tapi_cfg_get_all_threads(iut_rpcs->ta, &nb_cpus, &indices));
    for (i = 0; i < nb_cpus; ++i)
        CHECK_RC(tapi_cfg_cpu_release_by_id(iut_rpcs->ta, &indices[i]));
    free(indices);

    CHECK_RC(tapi_cfg_get_all_threads(tst_rpcs->ta, &nb_cpus, &indices));
    for (i = 0; i < nb_cpus; ++i)
        CHECK_RC(tapi_cfg_cpu_release_by_id(tst_rpcs->ta, &indices[i]));
    free(indices);

    /* Create RPC server jobs control to avoid addition in each test */
    CHECK_RC(rcf_rpc_server_create(iut_rpcs->ta, iut_job_control_rpcs, NULL));

    CHECK_RC(tapi_cfg_net_foreach_node(bind_dpdk_driver_on_tst_agent, NULL));

    CFG_WAIT_CHANGES;

    CHECK_RC(rc = cfg_synchronize("/:", TRUE));
    CHECK_RC(rc = cfg_tree_print(NULL, TE_LL_RING, "/:"));

    TEST_SUCCESS;

cleanup:

    TEST_END;
}

#endif /* !DOXYGEN_TEST_SPEC */
