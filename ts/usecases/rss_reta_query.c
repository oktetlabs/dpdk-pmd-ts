/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-rss_reta_query Query Redirection Table of RSS
 * @ingroup usecases
 * @{
 *
 * @objective Query Redirection Table of RSS
 *
 * @param nb_rx_queues     The number of RX queues
 * @param ethdev_state     The state of Ethernet device
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Make sure that it is possible to query Redirection Table of RSS
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/rss_reta_query"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                        *iut_rpcs = NULL;
    const struct if_nameindex             *iut_port = NULL;

    struct test_ethdev_config              test_ethdev_config;
    struct tarpc_rte_eth_conf              eth_conf;
    uint64_t                               reta_size;
    struct tarpc_rte_eth_rss_reta_entry64 *reta_conf = NULL;

    test_ethdev_state                      ethdev_state;
    int                                    nb_rx_queues;
    unsigned int                           i;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_INT_PARAM(nb_rx_queues);
    TEST_GET_ETHDEV_STATE(ethdev_state);

    TEST_STEP("Prepare Ethernet device configuration");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &test_ethdev_config);

    if (ethdev_state > TEST_ETHDEV_INITIALIZED)
    {
        TEST_STEP("Check if required number of Rx queues is supported");
        CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_INITIALIZED));
        if (nb_rx_queues > test_ethdev_config.dev_info.max_rx_queues)
            TEST_SKIP("So many Rx queues are not supported");
    }

    TEST_STEP("Prepare @p ethdev_state Ethernet device state for test "
               "using @p nb_rx_queues queues for set up a receive queue");
    test_ethdev_config.eth_conf = test_rpc_rte_eth_make_eth_conf(
                                      iut_rpcs, iut_port->if_index, &eth_conf);
    test_ethdev_config.nb_rx_queue = nb_rx_queues;
    test_ethdev_config.eth_conf->rxmode.mq_mode = TARPC_ETH_MQ_RX_RSS;

    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, ethdev_state));

    if (ethdev_state != TEST_ETHDEV_INITIALIZED)
    {
        TEST_STEP("Refresh device info to pick up RSS reta size changes");
        rpc_rte_eth_dev_info_get(iut_rpcs, iut_port->if_index,
                                 &test_ethdev_config.dev_info);
    }

    TEST_STEP("Query Redirection Table of RSS");
    reta_size = test_ethdev_config.dev_info.reta_size;
    reta_conf = tapi_calloc(TE_DIV_ROUND_UP(reta_size, RPC_RTE_RETA_GROUP_SIZE),
                            sizeof(*reta_conf));

    for (i = 0; i < TE_DIV_ROUND_UP(reta_size, RPC_RTE_RETA_GROUP_SIZE); i++)
        reta_conf[i].mask = ~0;

    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_rss_reta_query(iut_rpcs, iut_port->if_index,
                                        reta_conf, reta_size);
    switch (-rc)
    {
        case 0:
            break;
        case TE_RC(TE_RPC, TE_EOPNOTSUPP):
            TEST_SKIP("RSS redirection table query not supported");
            break;
        default:
            switch (ethdev_state)
            {
                case TEST_ETHDEV_INITIALIZED:
                case TEST_ETHDEV_CLOSED:
                    WARN_VERDICT("RSS redirection table query failed: %s",
                                 errno_rpc2str(-rc));
                    TEST_SUCCESS;
                    break;
                default:
                    TEST_VERDICT("RSS redirection table query failed: %s",
                                 errno_rpc2str(-rc));
            }
    }

    TEST_STEP("Check that the RETA is table with all zeros if @p ethdev_state "
              "is @c INITIALZED or "
              "table with a periodic sequence of numbers from @c 0 "
              "to @p nb_rx_queues - 1");
    if (ethdev_state > TEST_ETHDEV_INITIALIZED)
        for (i = 0; i < reta_size; i++)
            if (reta_conf[i / RPC_RTE_RETA_GROUP_SIZE].reta[
                    i % RPC_RTE_RETA_GROUP_SIZE] != i % nb_rx_queues)
                TEST_VERDICT("RETA has been filled incorrectly.");

    TEST_SUCCESS;

cleanup:
    free(reta_conf);

    TEST_END;
}
/** @} */
