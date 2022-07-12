/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-tx_burst_simple TX burst simple test
 * @ingroup usecases
 * @{
 *
 * @objective Transmit packets using @p tmpl from the TX queue
 *
 * @param tmpl         Traffic template
 * @param nb_pkts      Number of packets for sending
 * @param payload_len  Length of payload
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Make sure that packets sent from the TX queue successfully
 * received by iut_port
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/tx_burst_simple"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"


int
main(int argc, char *argv[])
{
    rcf_rpc_server                     *iut_rpcs = NULL;
    tapi_env_host                      *tst_host = NULL;
    const struct if_nameindex          *iut_port = NULL;
    const struct if_nameindex          *tst_if   = NULL;

    asn_value                          *ptrn  = NULL;
    asn_value                          *tmpl  = NULL;
    unsigned int                        mp_n_mbufs;
    rpc_rte_mempool_p                   mp;
    rpc_rte_mbuf_p                     *mbufs = NULL;
    struct test_ethdev_config           ethdev_config;
    unsigned int                        count;
    uint16_t                            sent;
    csap_handle_t                       rx_csap;
    unsigned int                        received;
    unsigned int                        no_match_pkts;
    unsigned int                        payload_len;
    unsigned int                        nb_pkts;
    struct tarpc_rte_pktmbuf_tx_offload m_tx_ol;
    unsigned int                        m_eth_d_len;

    TEST_START;
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_UINT_PARAM(nb_pkts);
    TEST_GET_UINT_PARAM(payload_len);

    TEST_STEP("Initialize EAL, preparing of configured Ethernet device state");
    (void)test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);
    ethdev_config.min_tx_desc = nb_pkts;
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_CONFIGURED));

    TEST_STEP("Prepare mbufs to be sent and pattern to match it by @p tmpl");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));

    CHECK_RC(asn_write_int32(tmpl, 1, "arg-sets.0.#simple-for.begin"));
    CHECK_RC(asn_write_int32(tmpl, nb_pkts, "arg-sets.0.#simple-for.end"));
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  payload_len));

    mp_n_mbufs = te_round_up_pow2(nb_pkts + 1);
    mp = rpc_rte_pktmbuf_pool_create(iut_rpcs, "tx_pool",
                                     mp_n_mbufs,
                                     mp_n_mbufs / 2,
                                     TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                     TEST_RTE_MEMPOOL_DEF_DATA_ROOM +
                                     payload_len,
                                     ethdev_config.socket_id);

    tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, tmpl, mp, NULL,
                                     &mbufs, &count, &ptrn);
    if (count != nb_pkts)
    {
        TEST_VERDICT("Unexpected number of prepared mbufs: %d mbufs have "
                     "been produced, but should be %d", count, nb_pkts);
    }

    TEST_STEP("Find out required MTU");
    rpc_rte_pktmbuf_get_tx_offload(iut_rpcs, mbufs[0], &m_tx_ol);
    m_eth_d_len = rpc_rte_pktmbuf_get_pkt_len(iut_rpcs, mbufs[0]) -
                  m_tx_ol.l2_len;

    ethdev_config.required_mtu = m_eth_d_len;

    TEST_STEP("Adjust MTU on both ends");
    test_set_mtu(iut_rpcs, iut_port->if_index, m_eth_d_len, &ethdev_config);
    CHECK_RC(tapi_cfg_base_if_set_mtu_leastwise(tst_host->ta,
                                                tst_if->if_name,
                                                m_eth_d_len));

    TEST_STEP("Prepare started Ethernet device state");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    /*
     * Create Ethernet-based CSAP which starts to listen to network
     * immediately
     */
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

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
