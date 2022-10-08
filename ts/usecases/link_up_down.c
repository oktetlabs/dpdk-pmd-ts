/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-link_up_down The parntner reaction to link status changes of iut_port
 * @ingroup usecases
 * @{
 *
 * @objective The parntner reaction to link status changes on @p iut_port
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Check how the parntner reacts to link status changes of @p iut_port
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/link_up_down"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_phy.h"


int
main(int argc, char *argv[])
{
    rcf_rpc_server             *iut_rpcs = NULL;
    tapi_env_host              *tst_host = NULL;
    const struct if_nameindex  *iut_port = NULL;
    const struct if_nameindex  *tst_if   = NULL;
    struct test_ethdev_config   ethdev_config;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_STEP("Initialize EAL and start the @p iut_port");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Wait for interface to become UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Make sure statuses on @p tst_if and @p iut_port realy are \"up\"");
    CHECK_RC(test_check_iut_tst_link_status(tst_host->ta, tst_if->if_name,
                                            iut_rpcs, iut_port->if_index,
                                            TRUE));

    TEST_STEP("Link down the @p iut_port");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_set_link_down(iut_rpcs, iut_port->if_index);

    if (-rc == TE_RC(TE_RPC, TE_EOPNOTSUPP))
    {
        TEST_SKIP("Set link down operation is not supported");
    }

    CHECK_RC(rc);

    /* Make sure statuses on @p tst_if and @p iut_port realy are "down" */
    CHECK_RC(test_check_iut_tst_link_status(tst_host->ta, tst_if->if_name,
                                            iut_rpcs, iut_port->if_index,
                                            FALSE));

    TEST_STEP("Link up the @p iut_port");
    rpc_rte_eth_dev_set_link_up(iut_rpcs, iut_port->if_index);

    TEST_STEP("Wait for interface to become UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP(" Make sure statuses on @p tst_if and @p iut_port realy are \"up\"");
    CHECK_RC(test_check_iut_tst_link_status(tst_host->ta, tst_if->if_name,
                                            iut_rpcs, iut_port->if_index,
                                            TRUE));

    TEST_SUCCESS;

cleanup:

    TEST_END;
}
/** @} */
