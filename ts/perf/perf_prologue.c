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
#include "tapi_env.h"
#include "tapi_reqs.h"
#include "tapi_test.h"
#include "rpc_dpdk_offloads.h"

/* Control environment binding manually */
#undef TEST_START_SPECIFIC
#define TEST_START_SPECIFIC

static te_errno
bind_dpdk_driver_on_tst_agent(cfg_net_t *net, cfg_net_node_t *node,
                              const char *oid_str, cfg_oid *oid, void *cookie)
{
    char *tst_dpdk_driver = NULL;
    char *pci_instance = NULL;
    char *pci_oid = NULL;
    const char *agent;
    const char *iface;
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
    iface = CFG_OID_GET_INST_NAME(oid, 2);

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

    rc = tapi_cfg_pci_oid_by_net_if(agent, iface, &pci_oid);
    if (rc != 0)
    {
        ERROR("Failed to get PCI device function by interface '%s': %r",
              iface, rc);
        goto out;
    }

    rc = tapi_cfg_pci_instance_by_addr(agent,
                                       cfg_oid_str_get_inst_name(pci_oid, -1),
                                       &pci_instance);
    if (rc != 0)
        goto out;

    rc = cfg_get_instance_fmt(NULL, &tst_dpdk_driver,
                              "/local:%s/dpdk_driver:", agent);
    if (rc != 0)
    {
        ERROR("Failed to get DPDK driver of TST agent");
        goto out;
    }

    rc = tapi_cfg_pci_bind_driver(pci_oid, tst_dpdk_driver);
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
    free(tst_dpdk_driver);
    free(pci_instance);
    free(pci_oid);

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

    unsigned int env_id = 0;
    const char *env_str;
    rcf_rpc_server *iut_jobs_ctrl = NULL;
    tapi_env_host *iut_host = NULL;
    tapi_env_host *tst_host = NULL;
    unsigned int iut_port_id = 0;
    const struct if_nameindex *iut_port = NULL;
    struct tarpc_rte_eth_dev_info dev_info;
    const char *iut_job_control_rpcs = "iut_jobs_ctrl";
    struct tarpc_ether_addr iut_mac;
    char *iut_mac_str = NULL;
    tapi_cpu_index_t *indices = NULL;
    size_t nb_cpus;
    unsigned int i;

    TEST_START;

    do {
        te_string env_name = TE_STRING_INIT;

        te_string_append(&env_name, "env%u", env_id);
        env_str = test_get_param(argc, argv, te_string_value(&env_name));
        te_string_free(&env_name);

        if (env_str == NULL)
            TEST_FAIL("No env which could be bound");

        memset(&env, 0, sizeof(env));
        rc = tapi_env_get(env_str, &env);
        if (TE_RC_GET_ERROR(rc) == TE_EENV)
        {
            /* Rely on the fact that env0 has TWO_PARALLEL_LINKS requirement */
            if (env_id == 0)
            {
                /* Skip tests which require two parallel links */
                CHECK_RC(tapi_reqs_modify("!TWO_PARALLEL_LINKS"));
            }
        }

        env_id++;
    } while (rc != 0);

    RING("Bound environment:\n%s", env_str);

    CFG_WAIT_CHANGES;

    TEST_GET_PCO(iut_jobs_ctrl);
    TEST_GET_HOST(iut_host);
    TEST_GET_HOST(tst_host);

    /*
     * Make stats of SFC (Xilinx) NICs more consistent by removing update period
     */
    CHECK_RC(cfg_add_instance_str("/local:/dpdk:/dev_args:pci_fn:10ee::", NULL,
                                  CFG_VAL(STRING, extra_sfc_devargs)));
    CHECK_RC(cfg_add_instance_str("/local:/dpdk:/dev_args:pci_fn:1924::", NULL,
                                  CFG_VAL(STRING, extra_sfc_devargs)));

    CHECK_RC(tapi_rte_eal_init(&env, iut_jobs_ctrl, 0, NULL));

    do {
        te_string if_name = TE_STRING_INIT;

        te_string_append(&if_name, "iut_port%u", iut_port_id++);
        iut_port = tapi_env_get_if(&env, te_string_value(&if_name));
        if (iut_port == NULL)
        {
            te_string_free(&if_name);
            break;
        }

        memset(&dev_info, 0, sizeof(dev_info));
        rpc_rte_eth_dev_info_get(iut_jobs_ctrl, iut_port->if_index, &dev_info);

        CHECK_RC(test_add_pci_fn_prop(iut_jobs_ctrl, iut_port,
                                      "max_tx_queues", dev_info.max_tx_queues));
        CHECK_RC(test_add_pci_fn_prop(iut_jobs_ctrl, iut_port,
                                      "max_rx_queues", dev_info.max_rx_queues));

        CHECK_RC(test_add_pci_fn_prop(iut_jobs_ctrl, iut_port,
                                      "min_mtu", dev_info.min_mtu));
        CHECK_RC(test_add_pci_fn_prop(iut_jobs_ctrl, iut_port,
                                      "max_mtu", dev_info.max_mtu));

        for (i = 0; i < rpc_dpdk_tx_offloads_num; i++)
        {
            if (dev_info.tx_offload_capa & (1ULL << rpc_dpdk_tx_offloads[i].bit))
                CHECK_RC(test_add_tx_offload_supported(iut_jobs_ctrl, iut_port,
                                                       rpc_dpdk_tx_offloads[i].bit));
        }

        for (i = 0; i < rpc_dpdk_rx_offloads_num; i++)
        {
            if (dev_info.rx_offload_capa & (1ULL << rpc_dpdk_rx_offloads[i].bit))
                CHECK_RC(test_add_rx_offload_supported(iut_jobs_ctrl, iut_port,
                                                       rpc_dpdk_rx_offloads[i].bit));
        }

        rpc_rte_eth_macaddr_get(iut_jobs_ctrl, iut_port->if_index, &iut_mac);
        te_asprintf(&iut_mac_str, TE_PRINTF_MAC_FMT,
                    TE_PRINTF_MAC_VAL(iut_mac.addr_bytes));
        CHECK_NOT_NULL(iut_mac_str);
        CHECK_RC(cfg_add_instance_fmt(NULL, CFG_VAL(STRING, iut_mac_str),
                                      "/local:/dpdk:/mac:%s",
                                      te_string_value(&if_name)));
        te_string_free(&if_name);
    } while (TRUE);

    /* Deinitialize EAL on iut_jobs_ctrl */
    CHECK_RC(tapi_rte_eal_fini(&env, iut_jobs_ctrl));

    /* Set RPC provider to default value on IUT (do not use DPDK RPC server) */
    CHECK_RC(cfg_set_instance_fmt(CFG_VAL(STRING, "ta_rpcs"),
                                  "/agent:%s/rpcprovider:", iut_host->ta));

    /* Dismantle default CPU pre-allocation made by 'ts/prologue' */
    CHECK_RC(tapi_cfg_get_all_threads(iut_host->ta, &nb_cpus, &indices));
    for (i = 0; i < nb_cpus; ++i)
        CHECK_RC(tapi_cfg_cpu_release_by_id(iut_host->ta, &indices[i]));
    free(indices);

    CHECK_RC(tapi_cfg_get_all_threads(tst_host->ta, &nb_cpus, &indices));
    for (i = 0; i < nb_cpus; ++i)
        CHECK_RC(tapi_cfg_cpu_release_by_id(tst_host->ta, &indices[i]));
    free(indices);

    /* Create RPC server jobs control to avoid addition in each test */
    CHECK_RC(rcf_rpc_server_create(iut_host->ta, iut_job_control_rpcs, NULL));

    CHECK_RC(tapi_cfg_net_foreach_node(bind_dpdk_driver_on_tst_agent, NULL));

    CFG_WAIT_CHANGES;

    CHECK_RC(rc = cfg_synchronize("/:", TRUE));
    CHECK_RC(rc = cfg_tree_print(NULL, TE_LL_RING, "/:"));

    TEST_SUCCESS;

cleanup:

    TEST_END;
}

#endif /* !DOXYGEN_TEST_SPEC */
