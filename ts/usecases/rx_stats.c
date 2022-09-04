/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-rx_stats Test of Rx statistics
 * @ingroup usecases
 * @{
 *
 * @objective Check the correctness of Rx statistics
 *
 * @param tmpl          Traffic template
 * @param nb_pkts       The number of packets
 * @param payload_len   Payload length
 *
 * @type use case
 *
 * @author Roman Zhukov <Roman.Zhukov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/rx_stats"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

#include "tapi_rpc_rte_ethdev.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                        *iut_rpcs = NULL;
    rcf_rpc_server                        *tst_rpcs = NULL;
    tapi_env_host                         *tst_host;
    const struct if_nameindex             *iut_port = NULL;
    const struct if_nameindex             *tst_if = NULL;

    asn_value                             *tmpl;
    rpc_rte_mbuf_p                         mbufs[BURST_SIZE] = {};
    asn_value                             *ptrn;
    int                                    i;
    int                                    nb_xstats;

    struct test_ethdev_config              ethdev_config;
    struct tarpc_rte_eth_xstat            *xstats_init;
    struct tarpc_rte_eth_xstat            *xstats;
    struct tarpc_rte_eth_xstat_name       *xstats_names;
    struct tarpc_rte_eth_stats             stats_init;
    struct tarpc_rte_eth_stats             stats;
    unsigned                               nb_pkts;
    unsigned                               nb_bytes;
    unsigned                               payload_len;
    uint32_t                               pkt_len;
    uint16_t                               iut_mtu;
    te_bool                                is_ucast_pkts = FALSE;
    te_bool                                is_mcast_pkts = FALSE;
    te_bool                                is_bcast_pkts = FALSE;
    te_bool                                is_fcs_included = FALSE;
    te_string                              tmp_wrong_xstats = TE_STRING_INIT;
    te_string                              wrong_xstats = TE_STRING_INIT;
    te_string                              zero_xstats = TE_STRING_INIT;
    const struct sockaddr                 *bcast_addr = NULL;
    const struct sockaddr                 *mcast_addr = NULL;
    const char *                           mac_addr;
    asn_value                             *eth_pdu = NULL;
    asn_value                             *pdus = NULL;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_UINT_PARAM(nb_pkts);
    TEST_GET_UINT_PARAM(payload_len);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);
    TEST_GET_LINK_ADDR(bcast_addr);
    TEST_GET_LINK_ADDR(mcast_addr);

    TEST_STEP("Initialize EAL");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &ethdev_config,
                                         TEST_ETHDEV_INITIALIZED));

    if (payload_len > TEST_RTE_MEMPOOL_DEF_DATA_ROOM)
        ethdev_config.mp = test_rte_pktmbuf_pool_create(
                               iut_rpcs, TEST_PKTS_MEMPOOL_NAME,
                               TEST_RTE_MEMPOOL_DEF_SIZE,
                               TEST_RTE_MEMPOOL_DEF_CACHE,
                               TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                               TEST_RTE_MEMPOOL_DEF_JUMBO_DATA_ROOM,
                               ethdev_config.socket_id);

    TEST_STEP("Start the Ethernet device");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Prepare @p tmpl for test");
    tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                         &test_params, TEST_IUT_PORT_MAC_NAME);

    CHECK_RC(asn_write_int32(tmpl, 1, "arg-sets.0.#simple-for.begin"));
    CHECK_RC(asn_write_int32(tmpl, nb_pkts, "arg-sets.0.#simple-for.end"));
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  payload_len));

    TEST_STEP("Change @p tst_if MTU and @p iut_port MTU to be able to transmit "
              "or receive bigger frames if required");
    CHECK_RC(tapi_cfg_base_if_set_mtu_leastwise(tst_host->ta, tst_if->if_name,
                                                payload_len));

    rpc_rte_eth_dev_get_mtu(iut_rpcs, iut_port->if_index, &iut_mtu);

    if (iut_mtu < payload_len)
        test_rte_eth_dev_set_mtu_await_link_up(iut_rpcs, iut_port->if_index,
                                               payload_len, &ethdev_config);

    TEST_STEP("Identify destination address: unicast, broadcast, multicast");
    CHECK_RC(asn_get_subvalue(tmpl, &pdus, "pdus"));
    eth_pdu = asn_find_child_choice_value(pdus, TE_PROTO_ETH);
    CHECK_NOT_NULL(eth_pdu);
    CHECK_RC(asn_get_field_data(eth_pdu, &mac_addr, "dst-addr.#plain"));

    if (!memcmp(mac_addr, bcast_addr->sa_data, ETHER_ADDR_LEN))
        is_bcast_pkts = TRUE;
    else if (!memcmp(mac_addr, mcast_addr->sa_data, ETHER_ADDR_LEN))
        is_mcast_pkts = TRUE;
    else
        is_ucast_pkts = TRUE;

    TEST_STEP("Enable all-multicast mode if it is multicast destination address");
    if (is_mcast_pkts)
        test_rte_eth_allmulticast_enable(iut_rpcs, iut_port->if_index,
                                         TEST_OP_REQUIRED);

    TEST_STEP("If running in \"speed-up\" mode, await stats reappearance "
              "holding numbers valid for the previous (test) iteration "
              "in order to pick those numbers (if any) as initial ones "
              "(instead of getting zeroes) to calculate the difference");
    if (dpdk_reuse_rpcs())
        MSLEEP(TEST_STATS_UPDATE_WAIT_MS);

    TEST_STEP("Get initial Rx statistics");
    rpc_rte_eth_stats_get(iut_rpcs, iut_port->if_index, &stats_init);

    nb_xstats = rpc_rte_eth_xstats_get_names(iut_rpcs, iut_port->if_index,
                                             NULL, 0);

    xstats_names = tapi_calloc(nb_xstats,
                               sizeof(struct tarpc_rte_eth_xstat_name));
    xstats_init = tapi_malloc(sizeof(xstats_init[0]) * nb_xstats);

    rpc_rte_eth_xstats_get_names(iut_rpcs, iut_port->if_index,
                                 xstats_names, nb_xstats);
    rpc_rte_eth_xstats_get(iut_rpcs, iut_port->if_index, xstats_init,
                           nb_xstats);

    TEST_STEP("Transmit and sniff @p nb_pkts packets from @p tst_if");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, tmpl, NULL, &ptrn));

    TEST_STEP("Receive packets on @p iut_port and check that they match the sent ones");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, 0,
                                         mbufs, TE_ARRAY_LEN(mbufs),
                                         nb_pkts, ptrn, TRUE));

    TEST_STEP("Wait statistics update");
    MSLEEP(TEST_STATS_UPDATE_WAIT_MS);

    TEST_STEP("Check that general Rx statistics are correct");
    rpc_rte_eth_stats_get(iut_rpcs, iut_port->if_index, &stats);

    if (stats.ipackets - stats_init.ipackets != nb_pkts)
        TEST_VERDICT("Wrong statistic ipackets");

    pkt_len = rpc_rte_pktmbuf_get_pkt_len(iut_rpcs, mbufs[0]);
    nb_bytes = pkt_len * nb_pkts;

    if (stats.ibytes - stats_init.ibytes != nb_bytes)
    {
        if (stats.ibytes - stats_init.ibytes ==
                        nb_bytes + nb_pkts * ETHER_CRC_LEN)
        {
            RING_VERDICT("FCS is included in ibytes");
            pkt_len += ETHER_CRC_LEN;
            nb_bytes += nb_pkts*ETHER_CRC_LEN;
            is_fcs_included = TRUE;
        }
        else
            TEST_VERDICT("Wrong statistics ibytes");
    }

    TEST_STEP("Get extended Rx statistics");
    xstats = tapi_malloc(sizeof(xstats[0]) * nb_xstats);
    rpc_rte_eth_xstats_get(iut_rpcs, iut_port->if_index, xstats,
                           nb_xstats);

    for (i = 0; i < nb_xstats; i++)
        xstats[i].value -= xstats_init[i].value;

    TEST_STEP("Check that extended Rx statistics are correct");
    test_check_xstat("rx_good_packets", nb_pkts, xstats_names, xstats,
                     nb_xstats, &wrong_xstats, &zero_xstats);
    test_check_xstat("rx_pkts", nb_pkts, xstats_names, xstats,
                     nb_xstats, &wrong_xstats, &zero_xstats);

    test_check_xstat("rx_good_bytes", nb_bytes, xstats_names, xstats,
                     nb_xstats, &wrong_xstats, &zero_xstats);
    test_check_xstat("rx_octets", nb_bytes, xstats_names, xstats,
                     nb_xstats, &tmp_wrong_xstats, &zero_xstats);

    /*
     * Generic statistics do not affect rx_octets, so this xstat may still
     * include FCS.
     */
    if (!is_fcs_included && tmp_wrong_xstats.len > 0)
    {
        te_string_reset(&tmp_wrong_xstats);
        test_check_xstat("rx_octets", nb_bytes + nb_pkts * ETHER_CRC_LEN,
                         xstats_names, xstats, nb_xstats, &tmp_wrong_xstats,
                         &zero_xstats);
        if (tmp_wrong_xstats.len == 0)
        {
            RING_VERDICT("FCS is included in rx_octets, but not in ibytes");
        }
        else
        {
            if (tmp_wrong_xstats.len > 0)
                te_string_append(&wrong_xstats, ", ");
            te_string_append(&wrong_xstats, "rx_octets");
        }
        te_string_free(&tmp_wrong_xstats);
    }


    test_check_xstat("rx_le_64_pkts",
                     (pkt_len <= 64) ? nb_pkts : 0, xstats_names,
                     xstats, nb_xstats, &wrong_xstats, &zero_xstats);
    test_check_xstat("rx_65_to_127_pkts",
                     (pkt_len >= 65 && pkt_len <= 127) ? nb_pkts : 0,
                     xstats_names, xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);
    test_check_xstat("rx_128_to_255_pkts",
                     (pkt_len >= 128 && pkt_len <= 255) ? nb_pkts : 0,
                     xstats_names, xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);
    test_check_xstat("rx_256_to_511_pkts",
                     (pkt_len >= 256 && pkt_len <= 511) ? nb_pkts : 0,
                     xstats_names, xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);
    test_check_xstat("rx_512_to_1023_pkts",
                     (pkt_len >= 512 && pkt_len <= 1023) ? nb_pkts : 0,
                     xstats_names, xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);
    test_check_xstat("rx_1024_to_15xx_pkts",
                     (pkt_len >= 1024 && pkt_len <= 1500) ? nb_pkts : 0,
                     xstats_names, xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);
    test_check_xstat("rx_ge_15xx_pkts",
                     (pkt_len >= 1600) ? nb_pkts : 0, xstats_names,
                     xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);

    test_check_xstat("rx_unicst_pkts",
                     is_ucast_pkts ? nb_pkts : 0, xstats_names,
                     xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);
    test_check_xstat("rx_multicst_pkts",
                     is_mcast_pkts ? nb_pkts : 0, xstats_names,
                     xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);
    test_check_xstat("rx_brdcst_pkts",
                     is_bcast_pkts ? nb_pkts : 0, xstats_names,
                     xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);

    if (wrong_xstats.len > 0)
        RING_VERDICT("Wrong xstats: %s", wrong_xstats.ptr);
    if (zero_xstats.len > 0)
        RING_VERDICT("Unexpected zero xstats: %s", zero_xstats.ptr);

    if (wrong_xstats.len + zero_xstats.len > 0)
        TEST_STOP;

    TEST_SUCCESS;

cleanup:

    TEST_END;
}
/** @} */
