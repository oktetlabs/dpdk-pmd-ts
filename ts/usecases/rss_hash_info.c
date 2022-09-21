/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-rss_hash_info Get RSS hash info test
 * @ingroup usecases
 * @{
 *
 * @objective Test checks that ethdev writes right rss hash info
 *            to the packet
 *
 * @param nb_rx_queues     The number of RX queues
 *
 * @type use case
 *
 * @author Roman Zhukov <Roman.Zhukov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/rss_hash_info"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                *iut_rpcs = NULL;
    rcf_rpc_server                *tst_rpcs = NULL;
    tapi_env_host                 *tst_host;
    const struct if_nameindex     *iut_port = NULL;
    const struct if_nameindex     *tst_if = NULL;

    asn_value                     *tmpl;
    rpc_rte_mbuf_p                 mbufs[BURST_SIZE] = {};
    asn_value                     *ptrn;
    uint16_t                       received = 0;
    struct test_ethdev_config      test_ethdev_config;
    struct tarpc_rte_eth_conf      eth_conf;
    tarpc_rss_hash_protos_t        hash_functions;
    struct tarpc_rte_eth_rss_conf *rss_conf;
    uint64_t                       offload;
    char                          *offload_name;

    uint32_t                       rss_hash_regular;
    uint32_t                       rss_hash_symmetric;
    uint32_t                       rss_hash;

    unsigned int                   matched_num;
    uint64_t                       ol_flags;
    int                            nb_rx_queues;
    int                            i;
    te_bool                        enable_offload;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_INT_PARAM(nb_rx_queues);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_BOOL_PARAM(enable_offload);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_STEP("Prepare @p ethdev_state Ethernet device state for test "
              "using @p nb_rx_queues queues for set up a receive queue");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &test_ethdev_config);

    test_ethdev_config.eth_conf = test_rpc_rte_eth_make_eth_conf(
                                      iut_rpcs, iut_port->if_index, &eth_conf);

    test_ethdev_config.nb_rx_queue = nb_rx_queues;

    test_ethdev_config.eth_conf->rxmode.mq_mode = TARPC_ETH_MQ_RX_RSS;

    TEST_STEP("Prepare TEST_ETHDEV_INITIALIZED state");
    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Check maximum number of Rx queues");
    if (nb_rx_queues > test_ethdev_config.dev_info.max_rx_queues)
        TEST_SKIP("So many Rx queues are not supported");

    TEST_STEP("Decide on explicit RSS configuration to be applied on start");
    CHECK_RC(test_get_rss_hf_by_tmpl(tmpl, &hash_functions));
    hash_functions &= test_ethdev_config.dev_info.flow_type_rss_offloads;
    rss_conf = &test_ethdev_config.eth_conf->rx_adv_conf.rss_conf;
    test_setup_rss_configuration(hash_functions, TRUE, rss_conf);

    offload = (1ULL << TARPC_RTE_ETH_RX_OFFLOAD_RSS_HASH_BIT);
    offload_name = rpc_rte_eth_dev_rx_offload_name(iut_rpcs, offload);

    if (enable_offload)
    {
        if (offload_name != NULL && strcmp(offload_name, "RSS_HASH") == 0)
        {
            if ((test_ethdev_config.dev_info.rx_offload_capa & offload) == 0)
                TEST_SKIP("Rx RSS hash offload is not supported");
            test_ethdev_config.eth_conf->rxmode.offloads |= offload;
        }
        else
        {
            TEST_SKIP("Rx RSS hash offload unknown");
        }
    }

    TEST_STEP("Prepare TEST_ETHDEV_STARTED state");
    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_STARTED));

    tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                         &test_params, TEST_IUT_PORT_MAC_NAME);

    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Transmit and sniff one packet from @p tst_if to the @p iut_port");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, tmpl, NULL, &ptrn));

    TEST_STEP("Receive packets on port @p iut_port");
    received = 0;
    for (i = 0; i < nb_rx_queues && received < TE_ARRAY_LEN(mbufs); i++)
        received += rpc_rte_eth_rx_burst(iut_rpcs, iut_port->if_index, i,
                                         mbufs + received,
                                         TE_ARRAY_LEN(mbufs) - received);

    CHECK_PACKETS_NUM(received, 1);

    TEST_STEP("Check that the recieved packet matches the sent");
    rpc_rte_mbuf_match_pattern(iut_rpcs, ptrn, &mbufs[0], 1, NULL,
                               &matched_num);
    CHECK_MATCHED_PACKETS_NUM(matched_num, 1);

    TEST_STEP("Given the traffic template, anticipate RSS hash value");
    CHECK_RC(test_calc_hash_by_tmpl_and_hf(rss_conf->rss_hf,
                                           rss_conf->rss_key.rss_key_val,
                                           tmpl, &rss_hash_regular,
                                           &rss_hash_symmetric));

    TEST_STEP("Check that the packet has RSS offload flag set");
    ol_flags = rpc_rte_pktmbuf_get_flags(iut_rpcs, mbufs[0]);
    if (enable_offload && (~ol_flags & (1UL << TARPC_RTE_MBUF_F_RX_RSS_HASH)) != 0)
        TEST_VERDICT("RSS offload flag is not set when offload is enabled");
    if (!enable_offload && (~ol_flags & (1UL << TARPC_RTE_MBUF_F_RX_RSS_HASH)) == 0)
        RING_VERDICT("RSS offload flag is set when offload is disabled");

    if ((ol_flags & (1UL << TARPC_RTE_MBUF_F_RX_RSS_HASH)) != 0)
    {
        TEST_STEP("Validate RSS hash value available from the mbuf");
        rss_hash = rpc_rte_pktmbuf_get_rss_hash(iut_rpcs, mbufs[0]);
        if (rss_hash == rss_hash_regular)
            RING("RSS hash value matches regular variant");
        else if (rss_hash == rss_hash_symmetric)
            RING_VERDICT("RSS hash value matches symmetric variant");
        else
            TEST_VERDICT("Invalid RSS hash value");
    }
    TEST_SUCCESS;

cleanup:
    for (i = 0; i < received; i++)
        rpc_rte_pktmbuf_free(iut_rpcs, mbufs[i]);

    TEST_END;
}
/** @} */
