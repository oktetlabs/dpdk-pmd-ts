/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-tunnel_udp_port_config Check tunnel UDP port configuration
 * @ingroup usecases
 * @{
 *
 * @objective Check that tunnel UDP port could be added and deleted correctly
 *
 * @param ethdev_state Ethernet device state
 * @param tunnel_type  Tunnel type
 * @param nb_entries   The number of UDP port entries
 *
 * @type use case
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 *
 * The test confirms that tunnel UDP port configuration interface is reliable
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "usecases/tunnel_udp_port_config"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                  *iut_rpcs = NULL;
    rcf_rpc_server                  *tst_rpcs = NULL;
    tapi_env_host                   *tst_host = NULL;
    const struct if_nameindex       *iut_port = NULL;
    const struct if_nameindex       *tst_if   = NULL;
    test_ethdev_state                ethdev_state;
    enum tarpc_rte_eth_tunnel_type   tunnel_type;
    unsigned int                     nb_entries;
    struct test_ethdev_config        ethdev_config;
    struct tarpc_rte_eth_udp_tunnel  udp_tunnel;
    unsigned int                     i;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_ETHDEV_STATE(ethdev_state);
    TEST_GET_TUNNEL_TYPE(tunnel_type);
    TEST_GET_UINT_PARAM(nb_entries);

    TEST_STEP("Prepare @p ethdev_state state");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &ethdev_config, ethdev_state));

    TEST_STEP("Check that 'add' operation is available");
    udp_tunnel.udp_port = UINT8_MAX - 1;
    udp_tunnel.prot_type = tunnel_type;
    rc = test_tunnel_udp_port_add(&ethdev_config, &udp_tunnel);
    if (rc == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
        TEST_SKIP("The operation to add a tunnel UDP port is unsupported");

    if (rc == -TE_RC(TE_RPC, TE_EINVAL))
        TEST_SKIP("The tunnel type is unsupported");

    TEST_STEP("Check that 'delete' operation is available");
    rc = test_tunnel_udp_port_del(&ethdev_config, &udp_tunnel);
    if (rc == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
        TEST_VERDICT("The operation to delete a tunnel UDP port is unsupported");

    TEST_STEP("Add a handful of entries");
    udp_tunnel.prot_type = tunnel_type;
    for (i = 0; i < nb_entries; ++i)
    {
        udp_tunnel.udp_port = UINT8_MAX - i - 1;
        CHECK_RC(test_tunnel_udp_port_add(&ethdev_config, &udp_tunnel));
    }

    TEST_STEP("Make sure that existing entry duplicate is rejected");
    rc = test_tunnel_udp_port_add(&ethdev_config, &udp_tunnel);
    if (rc == 0)
        TEST_VERDICT("Duplicate entry has been mistakenly accepted");

    TEST_STEP("Delete existing entries");
    for (i = 0; i < nb_entries; ++i)
    {
        udp_tunnel.udp_port = UINT8_MAX - i - 1;
        CHECK_RC(test_tunnel_udp_port_del(&ethdev_config, &udp_tunnel));
    }

    TEST_STEP("Make sure that an attempt to delete a nonexistent entry fails");
    rc = test_tunnel_udp_port_del(&ethdev_config, &udp_tunnel);
    if (rc == 0)
        TEST_VERDICT("An attempt to delete a nonexistent entry has succeeded");

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
