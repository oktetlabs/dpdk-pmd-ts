/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-rss Test RSS viability
 * @ingroup usecases
 * @{
 *
 * @objective Test RSS viability
 *
 * @param tmpl            The template of packet
 * @param nb_rx_queues    The number of RX queues
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Make sure that if RSS is enabled, sent packets are received
 * by the corresponding queue
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/rss"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                        *iut_rpcs  = NULL;
    rcf_rpc_server                        *tst_rpcs  = NULL;
    tapi_env_host                         *tst_host  = NULL;
    const struct if_nameindex             *iut_port  = NULL;
    const struct if_nameindex             *tst_if    = NULL;
    const struct sockaddr                 *tst_addr  = NULL;
    const struct sockaddr                 *iut_addr  = NULL;
    asn_value                             *tmpl      = NULL;
    asn_value                             *ptrn      = NULL;

    struct test_ethdev_config              ethdev_config;
    struct tarpc_rte_eth_conf              eth_conf;
    struct tarpc_rte_eth_rss_reta_entry64 *reta_conf;
    struct tarpc_rte_eth_rss_conf         *rss_conf;
    struct tarpc_rte_eth_rss_conf         *actual_rss_conf;
    uint64_t                               reta_size;
    unsigned int                           nb_rx_queues;
    int                                    expected_queue;
    int                                    reta_indx;
    int                                    reta_nb;

    tarpc_rss_hash_protos_t                hash_functions;
    uint32_t                               packet_hash;

    rpc_rte_mbuf_p                         mbufs[BURST_SIZE] = {};

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_UINT_PARAM(nb_rx_queues);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);

    TEST_STEP("Prepare ethernet device state for test");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);

    test_rpc_rte_eth_make_eth_conf(iut_rpcs, iut_port->if_index, &eth_conf);
    ethdev_config.eth_conf = &eth_conf;

    ethdev_config.nb_rx_queue = nb_rx_queues;
    ethdev_config.eth_conf->rxmode.mq_mode = TARPC_ETH_MQ_RX_RSS;

    TEST_STEP("Initialise the port in order to obtain RSS capabilities");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Check maximum number of Rx queues");
    if (nb_rx_queues > ethdev_config.dev_info.max_rx_queues)
        TEST_SKIP("So many Rx queues are not supported");

    TEST_STEP("Request appropriate RSS configuration that will be applied on "
              "device configure stage");
    rss_conf = &ethdev_config.eth_conf->rx_adv_conf.rss_conf;

    CHECK_RC(test_get_rss_hf_by_tmpl(tmpl, &hash_functions));
    hash_functions &= ethdev_config.dev_info.flow_type_rss_offloads;
    test_setup_rss_configuration(hash_functions, TRUE, rss_conf);

    TEST_STEP("Start the Ethernet device");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Prepare a packet by @p tmpl");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));

    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));

    TEST_STEP("Get RSS Redirection Table. If the corresponding RPC is not supported, "
              "use default Redirection Table");
    test_get_rss_reta(iut_rpcs, iut_port->if_index, &reta_size, &reta_conf);

    TEST_STEP("Get RSS hash configuration. If the corresponding RPC is not supported, "
              "use previously requested configuration");
    actual_rss_conf = test_try_get_rss_hash_conf(iut_rpcs, iut_port->if_index);
    if (actual_rss_conf != NULL)
        rss_conf = actual_rss_conf;

    TEST_STEP("Calculate packet hash, using Toeplitz function "
              "and current hash key");
    CHECK_RC(test_calc_hash_by_tmpl_and_hf(
                rss_conf->rss_hf, rss_conf->rss_key.rss_key_val,
                tmpl, &packet_hash, NULL));

    TEST_STEP("Determine the queue index by means of the hash");
    reta_nb = (packet_hash & TEST_HASH_RSS_MASK) / RPC_RTE_RETA_GROUP_SIZE;
    reta_indx = (packet_hash & TEST_HASH_RSS_MASK) % RPC_RTE_RETA_GROUP_SIZE;
    expected_queue = reta_conf[reta_nb].reta[reta_indx];

    TEST_STEP("Transmit the packet from @p tst_if");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, tmpl, NULL, &ptrn));

    TEST_STEP("Try to receive the packet on @p expected_queue "
              "Make sure that the packet received matches the packet sent");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index,
                                         expected_queue, mbufs,
                                         TE_ARRAY_LEN(mbufs), 1, ptrn, TRUE));

    rpc_rte_pktmbuf_free(iut_rpcs, mbufs[0]);

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
