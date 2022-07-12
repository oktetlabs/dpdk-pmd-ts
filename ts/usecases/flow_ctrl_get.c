/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-flow_ctrl_get Get current status of the Ethernet link flow
 *                               control test
 * @ingroup usecases
 * @{
 *
 * @objective Get current status of the Ethernet link flow control for
 *            Ethernet device
 *
 * @param ethdev_state     The state of Ethernet device
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Make sure that it is possible to get current status of the Ethernet
 * link flow control
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/flow_ctrl_get"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                        *iut_rpcs = NULL;
    const struct if_nameindex             *iut_port = NULL;

    struct tarpc_rte_eth_fc_conf           fc_conf;
    test_ethdev_state                      ethdev_state;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_ETHDEV_STATE(ethdev_state);

    TEST_STEP("Initialize EAL, preparing of @p ethdev_state Ethernet device state");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         NULL, ethdev_state));

    TEST_STEP("Get current status of the Ethernet link flow control "
              "on port @p iut_port");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_flow_ctrl_get(iut_rpcs, iut_port->if_index, &fc_conf);

    if (-rc == TE_RC(TE_RPC, TE_EOPNOTSUPP))
    {
        TEST_SKIP("Flow control get operation is not supported");
    }

    CHECK_RC(rc);

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
