/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-dev_conf_rss_adv Check custom RSS settings at start of day
 * @ingroup usecases
 * @{
 *
 * @objective Check that RSS settings may be customised on configuration step
 *
 * @type use case
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 *
 * Check that one can customise RSS HF and key on port configuration stage by
 * forwarding advertised RSS capabilities to the configuration API. Make sure
 * that wrong HF combination or RSS key size cannot be accepted by the driver.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "usecases/dev_conf_rss_adv"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                *iut_rpcs  = NULL;
    const struct if_nameindex     *iut_port  = NULL;

    struct tarpc_rte_eth_dev_info *di;
    struct test_ethdev_config      ec;
    struct tarpc_rte_eth_conf      eth_conf;
    struct tarpc_rte_eth_rss_conf *rss_conf;
    uint16_t                       nb_rxq = 1;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);

    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ec);

    TEST_STEP("Initialise the port in order to obtain advertised capabilities");
    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_INITIALIZED));
    di = &ec.dev_info;

    TEST_STEP("Prepare default configuration draft");
    test_rpc_rte_eth_make_eth_conf(iut_rpcs, iut_port->if_index, &eth_conf);
    ec.eth_conf = &eth_conf;
    rss_conf = &eth_conf.rx_adv_conf.rss_conf;

    TEST_STEP("Select multi-queue mode and prepare a dummy buffer for RSS key");
    eth_conf.rxmode.mq_mode = TARPC_ETH_MQ_RX_RSS;
    rss_conf->rss_key.rss_key_len = di->hash_key_size + 1;
    rss_conf->rss_key.rss_key_val = TE_ALLOC(di->hash_key_size + 1);
    CHECK_NOT_NULL(rss_conf->rss_key.rss_key_val);

    TEST_STEP("Make sure that correct configuration is not rejected");
    rss_conf->rss_hf = di->flow_type_rss_offloads;
    rss_conf->rss_key_len = di->hash_key_size;
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_configure(iut_rpcs, iut_port->if_index,
                                   ++nb_rxq, 1, &eth_conf);
    if (rc == -TE_RC(TE_RPC, TE_EINVAL) || rc == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
    {
        TEST_VERDICT("Correct RSS configuration has been rejected on "
                     "configuration stage");
    }
    else if (rc != 0)
    {
        TEST_STOP;
    }

    TEST_STEP("Make sure that it is possible to start the device");
    ec.cur_state = TEST_ETHDEV_CONFIGURED;
    ec.nb_rx_queue = nb_rxq;
    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_STARTED));
    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_CONFIGURED));

    TEST_STEP("Attempt to use unsupported RSS HF");
    rss_conf->rss_hf = ~di->flow_type_rss_offloads & TEST_ETH_RSS_PROTO_MASK;
    rss_conf->rss_key_len = di->hash_key_size;
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_configure(iut_rpcs, iut_port->if_index,
                                   ++nb_rxq, 1, &eth_conf);

    /* Unsupported RSS HF must be rejected on device configure */
    if (rc == 0)
        TEST_VERDICT("Bogus RSS HF has been accepted");
    else if (rc != -TE_RC(TE_RPC, TE_EINVAL))
        TEST_STOP;

    TEST_STEP("Attempt to use RSS key of unsupported size");
    rss_conf->rss_hf = di->flow_type_rss_offloads;
    rss_conf->rss_key_len = di->hash_key_size + 1;
    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_configure(iut_rpcs, iut_port->if_index,
                                   ++nb_rxq, 1, &eth_conf);
    if (rc == 0)
        WARN("Bogus RSS key size has been accepted on configuration stage");
    else if (rc == -TE_RC(TE_RPC, TE_EINVAL) || rc == -TE_RC(TE_RPC, TE_EPERM))
        TEST_SUCCESS;
    else if (rc == -TE_RC(TE_TAPI, TE_ECORRUPTED))
        TEST_VERDICT("Driver critical failure on rte_eth_dev_configure()");
    else
        TEST_STOP;

    TEST_STEP("Make sure that if an invalid configuration is accepted, device start "
              "will fail");
    ec.cur_state = TEST_ETHDEV_CONFIGURED;
    ec.nb_rx_queue = nb_rxq;
    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_RXTX_SETUP_DONE));

    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_start(iut_rpcs, iut_port->if_index);

    if (rc == 0)
        TEST_VERDICT("Bogus RSS key size has been accepted");
    else if (rc != -TE_RC(TE_RPC, TE_EINVAL) && rc != -TE_RC(TE_RPC, TE_EPERM))
        TEST_STOP;

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
