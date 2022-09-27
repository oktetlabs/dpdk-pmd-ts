/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-rss_hash_conf_get Get RSS hash computation test
 * @ingroup usecases
 * @{
 *
 * @objective Get current configuration of RSS hash computation
 *
 * @param nb_rx_queues     The number of RX queues
 * @param ethdev_state     The state of Ethernet device
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Make sure that it is possible to get configuration of RSS hash computation
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/rss_hash_conf_get"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                        *iut_rpcs = NULL;
    const struct if_nameindex             *iut_port = NULL;

    struct test_ethdev_config              test_ethdev_config;
    struct tarpc_rte_eth_conf              eth_conf;
    struct tarpc_rte_eth_rss_conf          rss_conf;
    int                                    nb_rx_queues;
    test_ethdev_state                      ethdev_state;

    memset(&rss_conf, 0, sizeof(rss_conf));

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_INT_PARAM(nb_rx_queues);
    TEST_GET_ETHDEV_STATE(ethdev_state);


    TEST_STEP("Initialize test representation of the ethdev configuration");
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

    TEST_STEP("Get configuration of RSS hash computation");
    rss_conf.rss_key_len = MAX(test_ethdev_config.dev_info.hash_key_size,
                               RPC_RSS_HASH_KEY_LEN_DEF);
    rss_conf.rss_key.rss_key_val = tapi_malloc(rss_conf.rss_key_len);
    rss_conf.rss_key.rss_key_len = rss_conf.rss_key_len;

    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_rss_hash_conf_get(iut_rpcs, iut_port->if_index,
                                           &rss_conf);
    switch (-rc)
    {
        case TE_RC(TE_RPC, TE_EOPNOTSUPP):
            TEST_SKIP("RSS hash config get not supported");
            /* FALLTHROUGH */
        case 0:
            TEST_SUCCESS;
            break;
        default:
            switch (ethdev_state)
            {
                case TEST_ETHDEV_INITIALIZED:
                case TEST_ETHDEV_CLOSED:
                    WARN_VERDICT("RSS hash config get failed: %s",
                                 errno_rpc2str(-rc));
                    TEST_SUCCESS;
                    break;
                default:
                    TEST_VERDICT("RSS hash config get failed: %s",
                                 errno_rpc2str(-rc));
            }
    }

cleanup:
    free(rss_conf.rss_key.rss_key_val);

    TEST_END;
}
/** @} */
