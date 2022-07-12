/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief DPDK Port Reresentors prologue
 *
 * DPDK Port Representors prologue
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 */

/** Logging subsystem entity name */
#define TE_TEST_NAME    "rep_prologue"

#include "dpdk_pmd_test.h"

#include "tapi_test.h"
#include "tapi_cfg_pci.h"

/**
 * @retval EXIT_SUCCESS     success
 * @retval EXIT_FAILURE     failure
 */
int
main(int argc, char **argv)
{
    const tapi_env_if *iut_port = NULL;
    rcf_rpc_server *iut_rpcs = NULL;
    unsigned int n_pfs = 0;
    char **pf_oids = NULL;
    unsigned int n_vfs = 0;
    cfg_oid **vfs = NULL;
    unsigned int i;
    unsigned int n_rep = 0;
    int ret;
    struct tarpc_rte_eth_representor_info rep_info = {};
    struct test_ethdev_config ethdev_config;

    TEST_START;
    TEST_GET_ENV_IF(iut_port);
    TEST_GET_PCO(iut_rpcs);

    if (test_is_vdev(iut_port->if_info.if_name))
        TEST_SKIP("Representors package does not support virtual devices");

    CHECK_RC(tapi_cfg_net_node_get_pci_oids(tapi_env_get_if_net_node(iut_port),
                                            &n_pfs, &pf_oids));

    if (n_pfs != 1)
        TEST_SKIP("Only single PCI device in a network node is supported");

    CHECK_RC(tapi_cfg_pci_get_max_vfs_of_pf(pf_oids[0], &n_vfs));
    if (n_vfs == 0)
        TEST_SKIP("Virtual functions on IUT node are not found");

    CHECK_RC_VERDICT(tapi_cfg_pci_enable_vfs_of_pf(pf_oids[0], n_vfs),
                     "Failed to enable VFs");

    CHECK_RC(tapi_cfg_pci_get_vfs_of_pf(pf_oids[0], FALSE, &n_vfs, &vfs, NULL));

    for (i = 0; i < n_vfs; i++)
        CHECK_RC(tapi_cfg_pci_grab(vfs[i]));

    TEST_STEP("Check representor info");
    test_prepare_config_def_mk(&env, iut_rpcs, &iut_port->if_info,
                               &ethdev_config);

    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    ret = rpc_rte_eth_representor_info_get(iut_rpcs, ethdev_config.port_id,
                                           NULL);
    if (ret < 0)
    {
        if (TE_RC_GET_ERROR(-ret) == TE_EOPNOTSUPP)
            TEST_SKIP("Querying representor information is not supported");
        TEST_FAIL("Failed to get the number of representors: %r", -ret);
    }

    rep_info.ranges.ranges_len = ret;
    rep_info.ranges.ranges_val =
        tapi_calloc(1, ret * sizeof(*rep_info.ranges.ranges_val));

    ret = rpc_rte_eth_representor_info_get(iut_rpcs, ethdev_config.port_id,
                                           &rep_info);

    for (i = 0; i < rep_info.nb_ranges; i++)
    {
        struct tarpc_rte_eth_representor_range *range;

        range = &rep_info.ranges.ranges_val[i];
        if (range->type != TARPC_RTE_ETH_REPRESENTOR_VF ||
            rep_info.controller != range->controller ||
            rep_info.pf != range->pf)
            continue;

        n_rep += range->id_end - range->id_base + 1;
    }

    if (n_rep != n_vfs)
    {
        ERROR_VERDICT("VF/rep mismatch: %u VFs available but %u representors found",
                      n_vfs, n_rep);
    }


    TEST_SUCCESS;

cleanup:
    free(rep_info.ranges.ranges_val);

    for (i = 0; i < n_pfs; i++)
        free(pf_oids[i]);
    free(pf_oids);

    if (vfs != NULL)
    {
        for (i = 0; i < n_vfs; i++)
            cfg_free_oid(vfs[i]);
        free(vfs);
    }

    tapi_rte_eal_fini(&env, iut_rpcs);

    TEST_END;
}
