/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-get_mtu   Get MTU test
 * @ingroup usecases
 * @{
 *
 * @objective Get MTU test
 *
 * @param ethdev_state     The state of Ethernet device
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Check the successful getting of MTU on different device states
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/get_mtu"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                *iut_rpcs = NULL;
    const struct if_nameindex     *iut_port = NULL;

    uint16_t                       mtu;
    test_ethdev_state              ethdev_state;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_ETHDEV_STATE(ethdev_state);

    TEST_STEP("Initialize EAL, preparing of @p ethdev_state Ethernet device state");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         NULL, ethdev_state));

    TEST_STEP("Get current MTU on port @p iut_port");
    rpc_rte_eth_dev_get_mtu(iut_rpcs, iut_port->if_index, &mtu);

    TEST_STEP("Logging of the obtained MTU");
    RING("MTU is %d", mtu);

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
