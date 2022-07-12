/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-test_detach Test detach
 * @ingroup usecases
 * @{
 *
 * @objective Test detach
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Check that detaching of Ethernet device is successful
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/test_detach"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server             *iut_rpcs = NULL;
    const struct if_nameindex  *iut_port = NULL;
    struct test_ethdev_config   tec;
    struct test_ethdev_config  *tecp = &tec;
    int                         ret;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);

    TEST_STEP("Initialization EAL, preparing Ethernet device state for test");
    test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                &tec, TEST_ETHDEV_STARTED);

    /* Make sure the port is reported as attached */
    ret = rpc_rte_eth_dev_is_valid_port(iut_rpcs, tecp->port_id);
    if (ret != 1)
        TEST_VERDICT("The port is not reported as attached upon start");

    TEST_STEP("Stop the port @p iut_port");
    rpc_rte_eth_dev_stop(iut_rpcs, tecp->port_id);

    TEST_STEP("Close the port @p iut_port");
    rpc_rte_eth_dev_close(iut_rpcs, tecp->port_id);

    TEST_STEP("Detach the @p iut_pci_fn_test from the port @p test_port");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eal_hotplug_remove(iut_rpcs, tecp->bus_name, tecp->dev_name);
    if (rc != 0)
        TEST_VERDICT("Ethernet device detach failed: %s", te_rc_err2str(-rc));

    TEST_STEP("Make sure the port is reported as detached");
    ret = rpc_rte_eth_dev_is_valid_port(iut_rpcs, tecp->port_id);
    if (ret != 0)
        TEST_VERDICT("The port is reported as attached upon detach");

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
