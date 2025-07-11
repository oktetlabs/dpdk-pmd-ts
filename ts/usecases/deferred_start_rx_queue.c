/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-deferred_start_rx_queue Deferred start RX queue test
 * @ingroup usecases
 * @{
 *
 * @objective Deferred start of random RX queue and checking that
 *            it works properly
 *
 * @param tmpl         Traffic template
 * @param n_rxq        The number of RX queues
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Make sure that if we use deferred start on randomly chosen queue,
 * then this queue actually will not receive packets until we do it manually
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/deferred_start_rx_queue"

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
    asn_value                             *ptrn      = NULL;
    asn_value                             *tmpl      = NULL;

    const struct tarpc_rte_eth_rss_conf   *rss_conf;
    struct tarpc_rte_eth_rss_reta_entry64 *reta_conf;
    tarpc_rss_hash_protos_t                hash_functions;
    te_toeplitz_hash_cache                *hash_cache;
    uint32_t                               packet_hash;
    unsigned int                          *reta_indxs_q;
    unsigned int                           nb_reta_indxs_q;
    uint64_t                               reta_size;

    struct test_ethdev_config              ethdev_config;
    struct tarpc_rte_eth_rxq_info          rx_qinfo;
    struct tarpc_rte_eth_rxconf            rx_conf;
    unsigned int                           n_rxq;
    unsigned int                           deferred_queue;

    uint8_t                               *src_addr = NULL;
    unsigned int                           addr_size;

    rpc_rte_mbuf_p                         mbufs[BURST_SIZE] = {};
    unsigned int                           nb_matched;
    unsigned int                           received;
    unsigned int                           i;
    te_bool                                rx_queue_info_get_supported;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_UINT_PARAM(n_rxq);

    TEST_STEP("Prepare @p n_rxq RX queues and set deffered flag randomly");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);

    TEST_STEP("Initialise the port in order to obtain RSS capabilities");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Check maximum number of Rx queues");
    if (n_rxq > ethdev_config.dev_info.max_rx_queues)
        TEST_SKIP("So many Rx queues are not supported");

    ethdev_config.nb_rx_queue = n_rxq;

    TEST_STEP("Prepare desired RSS hash configuration");

    CHECK_RC(test_get_rss_hf_by_tmpl(tmpl, &hash_functions));

    hash_functions &= ethdev_config.dev_info.flow_type_rss_offloads;

    test_rx_mq_rss_prepare(&ethdev_config, hash_functions);

    TEST_STEP("Configure the Ethernet device");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_CONFIGURED));

    deferred_queue = rand() % n_rxq;
    memcpy(&rx_conf, &ethdev_config.dev_info.default_rxconf, sizeof(rx_conf));
    rx_conf.rx_deferred_start = 1;

    ethdev_config.rx_confs = tapi_calloc(n_rxq,
                                         sizeof(*ethdev_config.rx_confs));
    ethdev_config.rx_confs[deferred_queue] = &rx_conf;

    TEST_STEP("Setup Rx/Tx queues, start the Ethernet device and wait for link up");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Retrieve information about chosen RX queue and make sure that "
              "the rx_deferred_start flag is set "
              "Ignore this check if rx_queue_info_get function is not supported");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_rx_queue_info_get(iut_rpcs, iut_port->if_index,
                                       deferred_queue, &rx_qinfo);
    switch (-rc)
    {
        case 0:
            rx_queue_info_get_supported = TRUE;
            if (rx_qinfo.conf.rx_deferred_start != 1)
            {
                TEST_VERDICT("Deferred start has not been applied for RX queue");
            }
            break;
        case TE_RC(TE_RPC, TE_EOPNOTSUPP):
            rx_queue_info_get_supported = FALSE;
            break;
        default:
            TEST_VERDICT("Get RX queue info operation failed: %s",
                         errno_rpc2str(-rc));
            break;
    }

    TEST_STEP("Prepare @p tmpl for test");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));

    TEST_STEP("Establish effective RSS hash configuration");

    rss_conf = test_rx_mq_rss_establish(&ethdev_config, FALSE);

    TEST_STEP("Get RSS Redirection Table. If the corresponding RPC is not supported, "
              "use default Redirection Table");
    test_get_rss_reta(iut_rpcs, iut_port->if_index, &reta_size, &reta_conf);

    TEST_STEP("Change @p tmpl to be sure that packet will be received "
              "on the @p deferred_queue queue using "
              "Redirection Table and RSS hash configuration");
    reta_indxs_q = tapi_calloc(reta_size, sizeof(*reta_indxs_q));
    for (i = 0, nb_reta_indxs_q = 0; i < reta_size; i++)
    {
        if (reta_conf[i / RPC_RTE_RETA_GROUP_SIZE].reta[
                i % RPC_RTE_RETA_GROUP_SIZE] == deferred_queue)
            reta_indxs_q[nb_reta_indxs_q++] = i;
    }

    src_addr = te_sockaddr_get_netaddr(tst_addr);
    addr_size = (unsigned int)te_netaddr_get_size(tst_addr->sa_family);

    hash_cache = te_toeplitz_cache_init_size(rss_conf->rss_key.rss_key_val,
                                             rss_conf->rss_key_len);

    CHECK_RC(test_calc_hash_by_tmpl_and_hf(
                rss_conf->rss_hf, rss_conf->rss_key.rss_key_val,
                rss_conf->rss_key_len, tmpl, &packet_hash, NULL));

    rc = test_change_src_addr_by_reta_index(hash_cache, packet_hash,
                                            src_addr, addr_size, reta_size,
                                            reta_indxs_q, nb_reta_indxs_q);
    if (rc != 0)
    {
        TEST_VERDICT("RSS configuration does not allow to receive a packet "
                     "on the %u queue", deferred_queue);
    }

    CHECK_RC(asn_write_value_field(tmpl, src_addr, addr_size,
                                   (addr_size == TEST_IP4_ADDR_LEN) ?
                                   "pdus.1.#ip4.src-addr.#plain" :
                                   "pdus.1.#ip6.src-addr.#plain"));

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Transmit and sniff packet from @p tst_if");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                            tst_if->if_name, tmpl, NULL, NULL));

    TEST_STEP("Try to receive packets on @p deferred_queue queue "
              "Check that @p iut_port hasn't received any packets on "
              "@p deferred_queue queue");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index,
                                         deferred_queue, mbufs,
                                         TE_ARRAY_LEN(mbufs),
                                         0, NULL, TRUE));

    TEST_STEP("Start @p deferred_queue RX queue");
    test_start_rx_queue(iut_rpcs, iut_port->if_index, deferred_queue);

    TEST_STEP("Check that the @p deferred_queue RX queue have no packets received");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index,
                                         deferred_queue, mbufs,
                                         TE_ARRAY_LEN(mbufs),
                                         0, NULL, TRUE));

    TEST_STEP("Transmit and sniff packet from @p tst_if");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, tmpl, NULL, &ptrn));

    TEST_STEP("Receive packet on @p deferred_queue queue");
    received = test_rx_burst_with_retries(iut_rpcs, iut_port->if_index,
                                          deferred_queue, mbufs,
                                          TE_ARRAY_LEN(mbufs), 1);

    TEST_STEP("If the packet has not hit the target queue, consider symmetric RSS hash. "
              "Change the template so that it will produce a packet hitting the target "
              "queue in terms of symmetric hash. Then try to send and capture it again.");
    if (received == 0)
    {
        uint32_t hash_symmetric;

        CHECK_RC(test_calc_hash_by_tmpl_and_hf(rss_conf->rss_hf,
                                               rss_conf->rss_key.rss_key_val,
                                               rss_conf->rss_key_len,
                                               tmpl, NULL, &hash_symmetric));

        CHECK_RC(test_change_src_addr_by_reta_index(hash_cache, hash_symmetric,
                                                    src_addr + addr_size -
                                                    TEST_IP4_ADDR_LEN,
                                                    TEST_IP4_ADDR_LEN,
                                                    reta_size, reta_indxs_q,
                                                    nb_reta_indxs_q));

        CHECK_RC(asn_write_value_field(tmpl, src_addr, addr_size,
                                       (addr_size == TEST_IP4_ADDR_LEN) ?
                                       "pdus.1.#ip4.src-addr.#plain" :
                                       "pdus.1.#ip6.src-addr.#plain"));

        asn_free_value(ptrn);
        CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, tmpl, NULL, &ptrn));

        CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index,
                                             deferred_queue, mbufs,
                                             TE_ARRAY_LEN(mbufs),
                                             1, ptrn, TRUE));
    }
    else
    {
        CHECK_PACKETS_NUM(received, 1);

        TEST_STEP("Check that the received packet matches the sent one from @p tst_if");
        rpc_rte_mbuf_match_pattern(iut_rpcs, ptrn, mbufs, 1, NULL,
                                   &nb_matched);
        CHECK_MATCHED_PACKETS_NUM(nb_matched, 1);
    }

    rpc_rte_pktmbuf_free(iut_rpcs, mbufs[0]);

    TEST_STEP("Stop @p deferred_queue RX queue");
    rpc_rte_eth_dev_rx_queue_stop(iut_rpcs, iut_port->if_index, deferred_queue);

    TEST_STEP("Retrieve information about RX queue and make sure that "
              "the rx_deferred_start flag is set after stopping "
              "@p deferred_queue RX queue "
              "Ignore this check if rx_queue_info_get function is not supported");
    if (rx_queue_info_get_supported)
    {
        rpc_rte_eth_rx_queue_info_get(iut_rpcs, iut_port->if_index,
                                      deferred_queue, &rx_qinfo);

        if (rx_qinfo.conf.rx_deferred_start != 1)
            TEST_VERDICT("Deferred start has not been applied for RX queue");
    }

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
