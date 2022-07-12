/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-tx_stats Test of Tx statistics
 * @ingroup usecases
 * @{
 *
 * @objective Check the correctness of Tx statistics
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

#define TE_TEST_NAME  "usecases/tx_stats"

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
    rpc_rte_mbuf_p                        *mbufs = NULL;
    asn_value                             *ptrn = NULL;
    int                                    i;
    int                                    nb_xstats;

    struct test_ethdev_config              ethdev_config;
    struct tarpc_rte_eth_xstat            *xstats_init;
    struct tarpc_rte_eth_xstat            *xstats;
    struct tarpc_rte_eth_xstat_name       *xstats_names;
    struct tarpc_rte_eth_stats             stats_init;
    struct tarpc_rte_eth_stats             stats;
    unsigned int                           nb_pkts;
    unsigned int                           nb_bytes;
    unsigned int                           count;
    unsigned int                           m_eth_d_len;
    struct tarpc_rte_pktmbuf_tx_offload    m_tx_ol;
    uint16_t                               sent;
    csap_handle_t                          rx_csap;
    unsigned int                           received;
    unsigned int                           no_match_pkts;
    unsigned int                           payload_len;
    uint32_t                               pkt_len;
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

    TEST_STEP("Initialize EAL, preparing of @p ethdev_state Ethernet device state");
    (void)test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);
    ethdev_config.min_tx_desc = nb_pkts;
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Prepare mbufs to be sent and pattern to match it by @p tmpl");
    tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                         &test_params, TEST_IUT_PORT_MAC_NAME);

    CHECK_RC(asn_write_int32(tmpl, 1, "arg-sets.0.#simple-for.begin"));
    CHECK_RC(asn_write_int32(tmpl, nb_pkts, "arg-sets.0.#simple-for.end"));
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  payload_len));

    tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, tmpl, ethdev_config.mp, NULL,
                                     &mbufs, &count, &ptrn);
    if (count != nb_pkts)
    {
        TEST_VERDICT("Unexpected number of prepared mbufs: %d mbufs have "
                     "been produced, but should be %d", count, nb_pkts);
    }

    TEST_STEP("Adjust MTU on both ends");
    rpc_rte_pktmbuf_get_tx_offload(iut_rpcs, mbufs[0], &m_tx_ol);
    m_eth_d_len = rpc_rte_pktmbuf_get_pkt_len(iut_rpcs, mbufs[0]) -
                  m_tx_ol.l2_len;

    /* ETHER_DATA_LEN is a standard Ethernet MTU of 1500 bytes */
    if (m_eth_d_len > ETHER_DATA_LEN)
    {
        test_rte_eth_dev_set_mtu_await_link_up(iut_rpcs, iut_port->if_index,
                                               m_eth_d_len, &ethdev_config);

        CHECK_RC(tapi_cfg_base_if_set_mtu_leastwise(tst_host->ta,
                                                    tst_if->if_name,
                                                    m_eth_d_len));
    }

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

    TEST_STEP("Calculate packet length and number of bytes to transmit");
    pkt_len = rpc_rte_pktmbuf_get_pkt_len(iut_rpcs, mbufs[0]);
    nb_bytes = pkt_len * nb_pkts;

    TEST_STEP("Although valid figures are expected to become available "
              "instantly upon the port start, in rare cases a Tx pause "
              "frame may be generated and counted once within a second "
              "thus not being considered on the step where the initial "
              "figures are collected but being spotted among the final "
              "readings; to cope with the issue, wait for a one second");
    MSLEEP(TEST_STATS_UPDATE_WAIT_MS);

    TEST_STEP("Get initial Tx statistics");
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

    TEST_STEP("Create Ethernet-based CSAP which starts to listen to network "
              "immediately");
    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(tst_host->ta, 0,
                                                tst_if->if_name,
                                                TAD_ETH_RECV_DEF,
                                                tmpl, &rx_csap));

    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, rx_csap, ptrn,
                                   RECEIVE_TIMEOUT_DEF, 0,
                                   RCF_TRRECV_PACKETS | RCF_TRRECV_SEQ_MATCH |
                                   RCF_TRRECV_MISMATCH));

    TEST_STEP("Validate and send the mbufs to @p iut_port "
              "Make sure that all are sent");
    sent = test_tx_prepare_and_burst(iut_rpcs, iut_port->if_index, 0,
                                     mbufs, count);
    if (sent != count)
        TEST_VERDICT("Cannot send all %u packets at once", count);

    TEST_STEP("Receive packet on @p tst_if "
              "Check that the received packets matches the sent one from @p iut_port");
    CHECK_RC(test_rx_await_pkts(tst_host->ta, rx_csap, count, 0));
    CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL, &received));

    TEST_STEP("Check that no extra packets are received on Tester");
    CHECK_RC(tapi_tad_csap_get_no_match_pkts(tst_host->ta, 0, rx_csap,
                                             &no_match_pkts));
    if (no_match_pkts != 0)
        TEST_VERDICT("%u unmatched packets were received", no_match_pkts);

    TEST_STEP("Check that number of received packets equal to sent");
    CHECK_PACKETS_NUM(received, count);

    TEST_STEP("Wait statistics update");
    MSLEEP(TEST_STATS_UPDATE_WAIT_MS);

    TEST_STEP("Check that general Tx statistics are correct");
    rpc_rte_eth_stats_get(iut_rpcs, iut_port->if_index, &stats);

    if (stats.opackets - stats_init.opackets != nb_pkts)
        TEST_VERDICT("Wrong statistic opackets");

    if (stats.obytes - stats_init.obytes != nb_bytes)
    {
        if (stats.obytes - stats_init.obytes ==
                        nb_bytes + nb_pkts * ETHER_CRC_LEN)
        {
            RING_VERDICT("FCS is included in obytes");
            pkt_len += ETHER_CRC_LEN;
            nb_bytes += nb_pkts*ETHER_CRC_LEN;
            is_fcs_included = TRUE;
        }
        else
            TEST_VERDICT("Wrong statistics obytes");
    }

    TEST_STEP("Get extended Tx statistics");
    xstats = tapi_malloc(sizeof(xstats[0]) * nb_xstats);
    rpc_rte_eth_xstats_get(iut_rpcs, iut_port->if_index, xstats,
                           nb_xstats);

    for (i = 0; i < nb_xstats; i++)
        xstats[i].value -= xstats_init[i].value;

    TEST_STEP("Check that extended Tx statistics are correct");
    test_check_xstat("tx_good_packets", nb_pkts, xstats_names, xstats,
                     nb_xstats, &wrong_xstats, &zero_xstats);
    test_check_xstat("tx_pkts", nb_pkts, xstats_names, xstats,
                     nb_xstats, &wrong_xstats, &zero_xstats);

    test_check_xstat("tx_good_bytes", nb_bytes, xstats_names, xstats,
                     nb_xstats, &wrong_xstats, &zero_xstats);
    test_check_xstat("tx_octets", nb_bytes, xstats_names, xstats,
                     nb_xstats, &tmp_wrong_xstats, &zero_xstats);

    /*
     * Generic statistics do not affect tx_octets, so this xstat may still
     * include FCS.
     */
    if (!is_fcs_included && tmp_wrong_xstats.len > 0)
    {
        te_string_reset(&tmp_wrong_xstats);
        test_check_xstat("tx_octets", nb_bytes + nb_pkts * ETHER_CRC_LEN,
                         xstats_names, xstats, nb_xstats, &tmp_wrong_xstats,
                         &zero_xstats);
        if (tmp_wrong_xstats.len == 0)
        {
            RING_VERDICT("FCS is included in tx_octets, but not in obytes");
        }
        else
        {
            if (tmp_wrong_xstats.len > 0)
                te_string_append(&wrong_xstats, ", ");
            te_string_append(&wrong_xstats, "tx_octets");
        }
        te_string_free(&tmp_wrong_xstats);
    }

    test_check_xstat("tx_le_64_pkts",
                     (pkt_len <= 64) ? nb_pkts : 0, xstats_names,
                     xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);
    test_check_xstat("tx_65_to_127_pkts",
                     (pkt_len >= 65 && pkt_len <= 127) ? nb_pkts : 0,
                     xstats_names, xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);
    test_check_xstat("tx_128_to_255_pkts",
                     (pkt_len >= 128 && pkt_len <= 255) ? nb_pkts : 0,
                     xstats_names, xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);
    test_check_xstat("tx_256_to_511_pkts",
                     (pkt_len >= 256 && pkt_len <= 511) ? nb_pkts : 0,
                     xstats_names, xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);
    test_check_xstat("tx_512_to_1023_pkts",
                     (pkt_len >= 512 && pkt_len <= 1023) ? nb_pkts : 0,
                     xstats_names, xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);
    test_check_xstat("tx_1024_to_15xx_pkts",
                     (pkt_len >= 1024 && pkt_len <= 1500) ? nb_pkts : 0,
                     xstats_names, xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);
    test_check_xstat("tx_ge_15xx_pkts",
                     (pkt_len >= 1600) ? nb_pkts : 0, xstats_names,
                     xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);

    test_check_xstat("tx_unicst_pkts",
                     is_ucast_pkts ? nb_pkts : 0, xstats_names,
                     xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);
    test_check_xstat("tx_multicst_pkts",
                     is_mcast_pkts ? nb_pkts : 0, xstats_names,
                     xstats, nb_xstats,
                     &wrong_xstats, &zero_xstats);
    test_check_xstat("tx_brdcst_pkts",
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
