/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-dev_info_persistence Test dev_info
 *        persistence on state transition
 * @ingroup usecases
 * @{
 *
 * @objective The test gets dev_info in initialized state and
 *            then check that it remains the same in all other states
 *
 * @param ethdev_state     The state of Ethernet device
 *
 * @type use case
 *
 * @author Roman Zhukov <Roman.Zhukov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/dev_info_persistence"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                        *iut_rpcs = NULL;
    const struct if_nameindex             *iut_port = NULL;

    struct test_ethdev_config              test_ethdev_config;
    struct tarpc_rte_eth_dev_info          init_dev_info;
    struct tarpc_rte_eth_dev_info          dev_info;
    test_ethdev_state                      init_ethdev_state;
    test_ethdev_state                      st;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_ETHDEV_STATE(init_ethdev_state);


    TEST_STEP("Initialize EAL, preparing of @p init_ethdev_state Ethernet device state, "
              "and get information about Ethernet device");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &test_ethdev_config);

    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, init_ethdev_state));

    memset(&init_dev_info, 0, sizeof(init_dev_info));
    rpc_rte_eth_dev_info_get(iut_rpcs, iut_port->if_index, &init_dev_info);

    TEST_STEP("Check device information at all states after @p init_ethdev_state");
    for (st = init_ethdev_state; st < TEST_ETHDEV_CLOSED; st++)
    {
        CHECK_RC(test_prepare_ethdev(&test_ethdev_config, st));

        TEST_SUBSTEP("Get information about Ethernet device");
        memset(&dev_info, 0, sizeof(dev_info));
        rpc_rte_eth_dev_info_get(iut_rpcs, iut_port->if_index, &dev_info);

        TEST_SUBSTEP("Check that it remains the same");
        if (test_dev_info_cmp(&init_dev_info, &dev_info) != 0)
        {
            TEST_VERDICT("Different info on state: %s",
                         test_get_ethdev_state_name(st));
        }
    }

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
