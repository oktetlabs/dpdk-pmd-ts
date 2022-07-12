/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-tx_desc_nb Tx behavior with different descriptors number
 * @ingroup usecases
 * @{
 *
 * @objective Given some descriptor count, verify queue setup and packet transmit
 *
 * @param template      Traffic template
 * @param nb_txd        Tx descriptors number
 *
 * @type use case
 *
 * @author Ivan Ilchenko <Ivan.Ilchenko@oktetlabs.ru>
 *
 * The test sets up Tx queue with specified descriptors number checking
 * reported limits. IUT transmits a big packets burst to verify the number
 * of packets device reported to send and tester received with respect to
 * set up descriptors.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "usecases/tx_desc_nb"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

#define TEST_PAYLOAD_LEN 128
#define TEST_PACKETS_TO_DESC_SCALE 4
#define TEST_TXQ 0
#define TEST_RXQ 0

int
main(int argc, char *argv[])
{
    rcf_rpc_server                     *iut_rpcs = NULL;
    tapi_env_host                      *tst_host = NULL;
    const struct if_nameindex          *iut_port = NULL;
    const struct if_nameindex          *tst_if   = NULL;

    asn_value                          *ptrn  = NULL;
    asn_value                          *template  = NULL;
    rpc_rte_mempool_p                   mp;
    rpc_rte_mbuf_p                     *mbufs = NULL;
    struct test_ethdev_config           ethdev_config;
    unsigned int                        count;
    uint16_t                            sent;
    csap_handle_t                       rx_csap;
    unsigned int                        received;
    unsigned int                        no_match_pkts;
    unsigned int                        nb_pkts;
    unsigned int                        nb_txd;
    unsigned int                        init_nb_txd;
    struct tarpc_rte_eth_txq_info       tx_qinfo;
    struct tarpc_rte_eth_desc_lim      *tx_desc_lim;
    te_bool                             fail_test = FALSE;
    te_bool                             limits_violated;

    TEST_START;
    TEST_GET_NDN_TRAFFIC_TEMPLATE(template);
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_UINT_PARAM(nb_txd);
    init_nb_txd = nb_txd;

    TEST_STEP("Prepare default config and mbuf pool");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);
    nb_pkts = nb_txd * TEST_PACKETS_TO_DESC_SCALE + 1;
    mp = rpc_rte_pktmbuf_pool_create(iut_rpcs, TEST_PKTS_MEMPOOL_NAME,
                                     MAX(nb_pkts,
                                         TEST_RTE_MEMPOOL_DEF_CACHE << 1),
                                     TEST_RTE_MEMPOOL_DEF_CACHE,
                                     TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                     TEST_RTE_MEMPOOL_DEF_DATA_ROOM +
                                     TEST_PAYLOAD_LEN,
                                     ethdev_config.socket_id);
    ethdev_config.mp = mp;
    TEST_STEP("Prepare @c TEST_ETHDEV_CONFIGURED state");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_CONFIGURED));
    tx_desc_lim = &ethdev_config.dev_info.tx_desc_lim;

    TEST_STEP("Set up Tx queue with specified number of Tx descriptors");
    limits_violated = test_desc_nb_violates_limits(nb_txd, tx_desc_lim);
    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_tx_queue_setup(iut_rpcs, iut_port->if_index, TEST_TXQ,
                                    nb_txd, ethdev_config.socket_id, NULL);
    if (rc != 0)
    {
        if (limits_violated && rc == -TE_RC(TE_RPC, TE_EINVAL))
        {
            RING("Tx queue set up failed due to violated descriptors limits");
            TEST_SUCCESS;
        }
        TEST_VERDICT("Failed to setup Tx queue: %s", errno_rpc2str(-rc));
    }

    TEST_STEP("Check number of descriptor in Tx queue info if supported");
    /*
     * The driver could silently set up another descriptors number.
     * Try to get the actual value.
     */
    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_tx_queue_info_get(iut_rpcs, iut_port->if_index, TEST_TXQ,
                                       &tx_qinfo);
    if (rc != 0)
    {
        if (rc == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
            RING_VERDICT("Get queue info isn't supported");
        else
            TEST_VERDICT("Failed to get TxQ %u info: %s", TEST_TXQ,
                         errno_rpc2str(-rc));
    }
    else
    {
        if (nb_txd != tx_qinfo.nb_desc)
        {
            if (limits_violated)
            {
                RING_VERDICT("Requested descriptors number violates limits; "
                             "Driver set up %u descriptors instead",
                             tx_qinfo.nb_desc);
            }
            else
            {
                WARN_VERDICT("Requested descriptors number violates NO limits; "
                             "Driver set up %u descriptors instead",
                             tx_qinfo.nb_desc);
                fail_test = TRUE;
            }
            nb_txd = tx_qinfo.nb_desc;
            limits_violated = test_desc_nb_violates_limits(nb_txd, tx_desc_lim);
        }
    }

    if (limits_violated)
    {
        WARN_VERDICT("Setup descriptors number violates reported limits but"
                     " successfully set");
        fail_test = TRUE;
    }

    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    ethdev_config.cur_state = TEST_ETHDEV_TX_SETUP_DONE;
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Prepare mbufs to be sent and pattern to match it by @p template");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));

    CHECK_RC(asn_write_int32(template, 1, "arg-sets.0.#simple-for.begin"));
    CHECK_RC(asn_write_int32(template, nb_pkts, "arg-sets.0.#simple-for.end"));
    CHECK_RC(tapi_ndn_subst_env(template, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&template, FALSE, NULL,
                                                  TEST_PAYLOAD_LEN));

    tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, template, mp, NULL,
                                     &mbufs, &count, &ptrn);
    if (count != nb_pkts)
    {
        TEST_VERDICT("Unexpected number of prepared mbufs: %d mbufs have "
                     "been produced, but should be %d", count, nb_pkts);
    }

    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(tst_host->ta, 0,
                                                tst_if->if_name,
                                                TAD_ETH_RECV_DEF,
                                                template, &rx_csap));

    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, rx_csap, ptrn,
                                   RECEIVE_TIMEOUT_DEF, 0,
                                   RCF_TRRECV_PACKETS | RCF_TRRECV_SEQ_MATCH |
                                   RCF_TRRECV_MISMATCH));

    TEST_STEP("Send the mbufs from @p iut_port");
    sent = test_tx_prepare_and_burst(iut_rpcs, iut_port->if_index, TEST_TXQ,
                                     mbufs, count);
    TEST_STEP("Receive and match the packets");
    CHECK_RC(test_rx_await_pkts(tst_host->ta, rx_csap, sent,
                                100 * 1000 + 10 * nb_pkts));

    CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL, &received));

    TEST_STEP("Check that no extra packets are received on Tester");
    CHECK_RC(tapi_tad_csap_get_no_match_pkts(tst_host->ta, 0, rx_csap,
                                             &no_match_pkts));
    if (no_match_pkts != 0)
        TEST_VERDICT("%u unmatched packets were received", no_match_pkts);

    TEST_STEP("Check that received and sent packet counts match");
    CHECK_PACKETS_NUM(received, sent);

    if (sent > nb_txd)
        TEST_VERDICT("%u sent packets are greater than setup Tx ring size",
                     sent);
    if (sent < init_nb_txd)
        RING_VERDICT("%u sent packets smaller than initially requested number of Tx descriptors",
                     sent);
    if (sent == init_nb_txd)
        RING("%u sent packets match initially requested number of Tx descriptors",
             sent);
    if (sent > init_nb_txd)
    {
        fail_test = TRUE;
        WARN_VERDICT("%u sent packets greater than initially requested number of Tx descriptors",
                     sent);
    }

    if (fail_test)
        TEST_STOP;

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
