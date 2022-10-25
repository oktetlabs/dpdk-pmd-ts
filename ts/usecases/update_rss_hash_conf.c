/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-update_rss_hash_conf Update the RSS hash configuration
 * @ingroup usecases
 * @{
 *
 * @objective Update the RSS hash configuration
 *
 * @param tmpl              The template of packet
 * @param nb_rx_queues      The number of RX queues
 * @param rss_hash_protos   Bitmask of RSS hash protocols
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Make sure that the RSS configuration updating is successful
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/update_rss_hash_conf"

#include "log_bufs.h"

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

    struct test_ethdev_config              test_ethdev_config;
    struct tarpc_rte_eth_conf              eth_conf;
    uint64_t                               reta_size;
    struct tarpc_rte_eth_rss_reta_entry64 *reta_conf;
    struct tarpc_rte_eth_rss_conf          rss_conf;
    struct tarpc_rte_eth_rss_conf          rss_conf_tmp;
    uint16_t                               received;
    rpc_rte_mbuf_p                         mbufs[BURST_SIZE] = {};

    uint32_t                               packet_hash;
    uint32_t                               hash_symmetric;
    unsigned int                           nb_rx_queues;
    uint64_t                               rss_hash_protos;
    div_t                                  reta_group;

    int                                    expected_queue;
    unsigned int                           matched_num;

    te_bool use_parameter_rss_hash_protos;
    te_bool use_rx_adv_conf;

    uint8_t *rss_hash_key;
    uint8_t *rss_hash_key_tmp;
    size_t rss_key_sz;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_ADDR(iut_rpcs, tst_addr);
    TEST_GET_ADDR(tst_rpcs, iut_addr);
    TEST_GET_UINT_PARAM(nb_rx_queues);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_RSS_HASH_PROTOS(rss_hash_protos);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_STEP("Initialise the Ethernet device to get device information");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &test_ethdev_config);
    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Check if required number of Rx queues is supported");
    if (nb_rx_queues > test_ethdev_config.dev_info.max_rx_queues)
    {
        TEST_SKIP("So many Rx queues are not supported");
    }

    TEST_STEP("Setup RX configuration to work in RSS mode");
    test_ethdev_config.eth_conf = test_rpc_rte_eth_make_eth_conf(
                                      iut_rpcs, iut_port->if_index, &eth_conf);
    test_ethdev_config.nb_rx_queue = nb_rx_queues;
    test_ethdev_config.eth_conf->rxmode.mq_mode = TARPC_ETH_MQ_RX_RSS;

    TEST_STEP("Enable RSS_HASH offload if it is supported");
    if ((test_ethdev_config.dev_info.rx_offload_capa &
         (1ULL << TARPC_RTE_ETH_RX_OFFLOAD_RSS_HASH_BIT)) != 0)
    {
        test_ethdev_config.eth_conf->rxmode.offloads |=
            (1ULL << TARPC_RTE_ETH_RX_OFFLOAD_RSS_HASH_BIT);
    }

    TEST_STEP("Commence configuration with generating a random hash key");

    rss_key_sz = MAX(test_ethdev_config.dev_info.hash_key_size,
                     RPC_RSS_HASH_KEY_LEN_DEF);

    rss_hash_key_tmp = TE_ALLOC(rss_key_sz);
    CHECK_NOT_NULL(rss_hash_key_tmp);

    rss_hash_key = TE_ALLOC(rss_key_sz);
    CHECK_NOT_NULL(rss_hash_key);

    te_fill_buf(rss_hash_key, rss_key_sz);

    rss_conf.rss_key.rss_key_val = rss_hash_key;
    rss_conf.rss_key.rss_key_len = rss_key_sz;
    rss_conf.rss_key_len = rss_key_sz;

    TEST_STEP("Specify all of the advertised flags in the hash proto mask");

    rss_conf.rss_hf = test_ethdev_config.dev_info.flow_type_rss_offloads;

    use_parameter_rss_hash_protos = FALSE;
    use_rx_adv_conf = FALSE;

retry_dev_configure_and_hash_conf_update:

    TEST_STEP("Start the Ethernet device");
    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_STARTED));

retry_hash_conf_update:

    TEST_STEP("Try to update hash configuration via the dedicated API");

    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_rss_hash_update(iut_rpcs, iut_port->if_index,
                                         &rss_conf);

    TEST_STEP("On error, lead the ethdev to state @c TEST_ETHDEV_INITIALIZED");

    TEST_STEP("If needed, fill out @c rx_adv_conf and redo steps 7 and 8");

    if (rc != 0)
    {
        CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_STOPPED));

        /*
         * The next step may be to reconfigure the ethdev without releasing
         * the queues that were set up above. Doing so requires that
         * the formal state of the ethdev be adjusted like this.
         */
        test_ethdev_config.cur_state = TEST_ETHDEV_INITIALIZED;

        if (!use_rx_adv_conf && !use_parameter_rss_hash_protos)
        {
            memcpy(&test_ethdev_config.eth_conf->rx_adv_conf.rss_conf,
                   &rss_conf, sizeof(rss_conf));
            use_rx_adv_conf = TRUE;

            goto retry_dev_configure_and_hash_conf_update;
        }

        TEST_VERDICT("Hash protocols configuration failed: %s",
                     errno_rpc2str(-rc));
    }

    TEST_STEP("If needed, apply parameter @p rss_hash_protos and redo step 8");

    if (!use_parameter_rss_hash_protos)
    {
        if (use_rx_adv_conf)
            WARN_ARTIFACT("Hash proto mask in rx_adv_conf must not be zero");

        use_parameter_rss_hash_protos = TRUE;
        rss_conf.rss_hf = rss_hash_protos;

        goto retry_hash_conf_update;
    }

    TEST_STEP("Query the RSS hash configuration and check that "
              "the new configuration was successfully updated "
              "Skip this step, if RSS hash configuration query is not supported");
    rss_conf_tmp.rss_key.rss_key_val = rss_hash_key_tmp;
    rss_conf_tmp.rss_key.rss_key_len = rss_conf.rss_key.rss_key_len;
    rss_conf_tmp.rss_key_len = rss_conf.rss_key.rss_key_len;
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_rss_hash_conf_get(iut_rpcs, iut_port->if_index,
                                           &rss_conf_tmp);
    if (rc == 0)
    {
        if (rss_conf.rss_key_len != rss_conf_tmp.rss_key_len)
        {
            TEST_VERDICT("Read RSS hash key length does not match written, "
                         "should be %hhu, but it is %hhu",
                         rss_conf.rss_key_len, rss_conf_tmp.rss_key_len);
        }

        if (memcmp(rss_conf.rss_key.rss_key_val, rss_conf_tmp.rss_key.rss_key_val,
                   rss_conf.rss_key_len) != 0)
        {
            TEST_VERDICT("Read RSS hash key does not match written one");
        }

        if (rss_conf.rss_hf != rss_conf_tmp.rss_hf &&
            (rss_conf_tmp.rss_hf & rss_conf.rss_hf) == rss_conf.rss_hf)
        {
            uint64_t extra_protos = rss_conf_tmp.rss_hf & ~rss_conf.rss_hf;
            te_log_buf *tlbp = te_log_buf_alloc();

            WARN_ARTIFACT("The ethdev enabled hash for extra protocols: %s",
                          tarpc_rte_eth_dev_rss_types2str(tlbp, extra_protos));

            te_log_buf_free(tlbp);
        }
        else if (rss_conf.rss_hf != rss_conf_tmp.rss_hf)
        {
            TEST_VERDICT("Updating of RSS hash functions failed, should "
                         "be 0x%x, but it is 0x%x",
                         rss_conf.rss_hf, rss_conf_tmp.rss_hf);
        }
    }
    else if (-rc == TE_RC(TE_RPC, TE_EOPNOTSUPP))
    {
        WARN_VERDICT("RSS hash configuration get operation is not supported");
    }
    else
    {
        TEST_VERDICT("RSS hash configuration get operation failed");
    }

    TEST_STEP("Prepare a packet by @p tmpl to check the RSS hash coniguration "
              "by the packet sending");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));

    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));

    TEST_STEP("Query the RETA. Use default one if query operation is not supported");
    test_get_rss_reta(iut_rpcs, iut_port->if_index, &reta_size, &reta_conf);

    TEST_STEP("Calculate the packet hash, using the Toeplitz function "
              "and the new hash key");
    CHECK_RC(test_calc_hash_by_tmpl_and_hf(
                rss_conf_tmp.rss_hf, rss_conf.rss_key.rss_key_val,
                rss_conf.rss_key_len,
                tmpl, &packet_hash, &hash_symmetric));

    TEST_STEP("Determine the queue index by means of the hash");
    reta_group = div(packet_hash % reta_size, RPC_RTE_RETA_GROUP_SIZE);
    expected_queue = reta_conf[reta_group.quot].reta[reta_group.rem];

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Transmit the packet from @p tst_if");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, tmpl, NULL, &ptrn));

    TEST_STEP("Try to receive the packet on @p expected_queue");
    received = test_rx_burst_with_retries(iut_rpcs, iut_port->if_index,
                                          expected_queue, mbufs,
                                          TE_ARRAY_LEN(mbufs), 1);

    TEST_STEP("If the packet has not hit the target queue, consider symmetric RSS hash.");
    if (received == 0)
    {
        reta_group = div(hash_symmetric % reta_size, RPC_RTE_RETA_GROUP_SIZE);
        expected_queue = reta_conf[reta_group.quot].reta[reta_group.rem];

        received = test_rx_burst_with_retries(iut_rpcs, iut_port->if_index,
                                              expected_queue, mbufs,
                                              TE_ARRAY_LEN(mbufs), 1);
    }

    if (received == 1)
    {
        TEST_STEP("Make sure that the packet received matches the packet sent");
        rpc_rte_mbuf_match_pattern(iut_rpcs, ptrn, mbufs, received, NULL,
                                   &matched_num);
        CHECK_MATCHED_PACKETS_NUM(matched_num, 1);

        TEST_STEP("Check RSS hash value if it is available");
        test_check_mbuf_rss_hash_value(iut_rpcs, mbufs[0],
                                       packet_hash, hash_symmetric);

        rpc_rte_pktmbuf_free(iut_rpcs, mbufs[0]);

        TEST_SUCCESS;
    }
    else
    {
        TEST_STEP("Check other Rx queues to collect diagnostics");
        test_check_rss_queues(iut_rpcs, iut_port->if_index, nb_rx_queues,
                              reta_size, reta_conf, ptrn,
                              packet_hash, expected_queue);

        /* Fail with verdict after diagnostics */
        CHECK_PACKETS_NUM(received, 1);
    }

cleanup:
    TEST_END;
}
/** @} */
