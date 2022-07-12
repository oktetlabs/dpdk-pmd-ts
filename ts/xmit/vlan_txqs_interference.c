/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Transmit functionality
 */

/** @defgroup xmit-vlan_txqs_interference Exclude VLAN tag interference between TxQs
 * @ingroup xmit
 * @{
 *
 * @objective Make sure that VLAN offloads on a TxQ have no impact on the others
 *
 * @param template              Traffic template
 * @param burst_size_per_txq    The number of packets to pass on each TxQ
 * @param nb_tx_queues          The number of Tx queues
 *
 * @type conformance
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 *
 * The test is to verify that VLAN offload requests on different Tx queues don't
 * interfere with each other or with Tx queues which don't involve VLAN offloads
 *
 * @par Scenario:
 */

#define TE_TEST_NAME                   "xmit/vlan_txqs_interference"

#include "dpdk_pmd_test.h"
#include "te_defs.h"
#include "tapi_cfg_base.h"

#include "tapi_ndn.h"
#include "tapi_tad.h"

#define MAX_NB_TX_QUEUES        64
#define MAX_BURST_SIZE_PER_TXQ  10
#define MAX_NB_PATTERNS         (MAX_NB_TX_QUEUES * MAX_BURST_SIZE_PER_TXQ)

struct matched_patterns {
    unsigned int        *patterns_recv_nb;
    const unsigned int  *patterns_cnt;
    const unsigned int  *burst_size_per_txq;
    unsigned int        *patterns_seqno;
    unsigned int        curr_seqno;
};

static void
check_patterns_match_consistency(struct matched_patterns *mp)
{
    te_string pack_info = TE_STRING_INIT;
    te_bool packet_loss = FALSE;
    te_bool duplicates = FALSE;
    te_bool out_of_order = FALSE;
    te_bool test_fail = FALSE;
    unsigned int pkt_in_burst;
    unsigned int recv_nb;
    unsigned int qid;
    unsigned int i;
    int j;

    for (i = 0; i < *mp->patterns_cnt; i++)
    {
#define QUEUE_ID(p) (p / *mp->burst_size_per_txq)
#define PKT_IN_BURST(p) (p % *mp->burst_size_per_txq)

        pkt_in_burst = PKT_IN_BURST(i);
        qid = QUEUE_ID(i);

        te_string_free(&pack_info);
        CHECK_RC(te_string_append(&pack_info,
                                  "Packet number %u in queue %u ",
                                  pkt_in_burst, qid));

        recv_nb = mp->patterns_recv_nb[i];
        if (recv_nb == 0)
        {
            packet_loss = TRUE;
            ERROR("%s is lost", pack_info.ptr);
        }

        if (recv_nb > 1)
        {
            duplicates = TRUE;
            ERROR("%s is duplicated: got %u times", pack_info.ptr, i, recv_nb);
        }

        for (j = i - 1; j >= 0 && QUEUE_ID(j) == qid; j--)
        {
            if (mp->patterns_recv_nb[j] > 0 &&
                mp->patterns_seqno[j] > mp->patterns_seqno[i])
            {
                out_of_order = TRUE;
                ERROR("%s is out of order - earlier %u", pack_info.ptr,
                      PKT_IN_BURST(j));
            }
        }

#undef QUEUE_ID
#undef PKT_IN_BURST
    }

    if (packet_loss)
    {
        ERROR_VERDICT("There are lost packets");
        test_fail = true;
    }

    if (duplicates)
    {
        ERROR_VERDICT("There are duplicated packets");
        test_fail = true;
    }

    if (out_of_order)
    {
        ERROR_VERDICT("There are out of order packets");
        test_fail = true;
    }

    if (test_fail)
        TEST_STOP;
}

static void
check_pattern(asn_value *packet, void *user_data)
{
    struct matched_patterns *mp = (struct matched_patterns *)user_data;
    int32_t match_unit;

    CHECK_RC(asn_read_int32(packet, &match_unit, "match-unit"));

    if ((match_unit >= 0) &&
        ((unsigned int)match_unit < *mp->patterns_cnt))
    {
        mp->patterns_recv_nb[match_unit]++;
        /*
         * Since there might be duplicates, set sequence number of the first
         * packet arrived.
         */
        if (mp->patterns_recv_nb[match_unit] == 1)
            mp->patterns_seqno[match_unit] = mp->curr_seqno++;
    }
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server                     *iut_rpcs = NULL;
    tapi_env_host                      *tst_host = NULL;
    const struct if_nameindex          *iut_port = NULL;
    const struct if_nameindex          *tst_if = NULL;

    asn_value                          *template;
    unsigned int                        burst_size_per_txq = 0;
    unsigned int                        nb_tx_queues = 0;

    struct test_ethdev_config           ethdev_config;
    struct tarpc_rte_eth_conf           eth_conf;
    unsigned int                        payload_len = DPMD_TS_PAYLOAD_LEN_DEF;
    rpc_rte_mbuf_p                    **bursts = NULL;
    asn_value                         **patterns;
    unsigned int                        mp_n_mbufs;
    rpc_rte_mempool_p                   mp;
    unsigned int                        i;
    asn_value                          *pattern;
    csap_handle_t                       rx_csap = CSAP_INVALID_HANDLE;
    unsigned int                        packets_sent = 0;
    unsigned int                        packets_matched;

    unsigned int                        patterns_recv_nb[MAX_NB_PATTERNS];
    unsigned int                        patterns_seqno[MAX_NB_PATTERNS];
    unsigned int                        patterns_cnt;
    struct matched_patterns             match_p = {patterns_recv_nb,
                                                   &patterns_cnt,
                                                   &burst_size_per_txq,
                                                   patterns_seqno, 0};
    tapi_tad_trrecv_cb_data             rcv_cb = {check_pattern, &match_p};

    TEST_START;

    TEST_GET_PCO(iut_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_GET_NDN_TRAFFIC_TEMPLATE(template);
    TEST_GET_UINT_PARAM(burst_size_per_txq);
    TEST_GET_UINT_PARAM(nb_tx_queues);

    TEST_STEP("Check maximum number of Tx queues");
    if (!(MAX_NB_TX_QUEUES >= 0 && MAX_NB_TX_QUEUES >= nb_tx_queues &&
          MAX_BURST_SIZE_PER_TXQ >= 0 &&
          MAX_BURST_SIZE_PER_TXQ >= burst_size_per_txq))
    {
        ERROR_VERDICT("Maximum Tx queues number %d VS requested %u;"
                      " Maximum burst size per TxQ %d VS requested %u",
                      MAX_NB_TX_QUEUES, nb_tx_queues, MAX_BURST_SIZE_PER_TXQ,
                      burst_size_per_txq);
        TEST_FAIL("Some of upper limits are set wrong");
    }
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                            &ethdev_config, TEST_ETHDEV_INITIALIZED));

    if (nb_tx_queues > ethdev_config.dev_info.max_tx_queues)
        TEST_SKIP("So many Tx queues are not supported");

    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    (void)test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);

    ethdev_config.nb_rx_queue = 1;
    ethdev_config.nb_tx_queue = nb_tx_queues;
    ethdev_config.min_tx_desc = burst_size_per_txq;

    (void)test_rpc_rte_eth_make_eth_conf(iut_rpcs, iut_port->if_index,
                                         &eth_conf);
    ethdev_config.eth_conf = &eth_conf;

    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Verify the configuration requested");
    if ((ethdev_config.dev_info.tx_offload_capa &
        (1U << TARPC_RTE_DEV_TX_OFFLOAD_VLAN_INSERT_BIT)) == 0)
        TEST_SKIP("TX VLAN insertion is not available");

    TEST_STEP("Obtain the source Ethernet address");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params, "iut_mac"));

    TEST_STEP("Adjust the traffic template");
    CHECK_RC(tapi_ndn_subst_env(template, &test_params, &env));

    (void)asn_free_child(template, PRIVATE, NDN_TMPL_PAYLOAD);
    asn_write_value_field(template, &payload_len, sizeof(payload_len),
                          "payload.#length");

    (void)asn_free_child(template, PRIVATE, NDN_TMPL_ARGS);
    CHECK_RC(asn_write_int32(template, 1, "arg-sets.0.#simple-for.begin"));
    CHECK_RC(asn_write_int32(template, burst_size_per_txq,
                             "arg-sets.0.#simple-for.end"));

    TEST_STEP("Book a suitable mempool");
    mp_n_mbufs = te_round_up_pow2(nb_tx_queues * burst_size_per_txq + 1);
    mp = rpc_rte_pktmbuf_pool_create(iut_rpcs, "vlan_txqs_interference",
                                     mp_n_mbufs,
                                     mp_n_mbufs / 2,
                                     TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                     TEST_RTE_MEMPOOL_DEF_DATA_ROOM,
                                     ethdev_config.socket_id);

    TEST_STEP("Prepare packet bursts with randomly selected VLAN offload requests");
    bursts = tapi_calloc(nb_tx_queues, sizeof(*bursts));
    patterns = tapi_calloc(nb_tx_queues, sizeof(*patterns));

    for (i = 0; i < nb_tx_queues; ++i)
    {
        int             vlan_id = rand_range(-1, TEST_MAX_VLAN_ID);
        send_transform  cond;
        unsigned int    n_mbufs;

        memset(&cond, 0, sizeof(cond));
        if (vlan_id >= 0)
        {
            cond.hw_flags |= SEND_COND_HW_OFFL_VLAN;
            cond.vlan_tci = (uint16_t)vlan_id;
        }

        tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, template, mp, &cond,
                                         &bursts[i], &n_mbufs, &patterns[i]);
        if (n_mbufs != burst_size_per_txq)
            TEST_VERDICT("Wrong number of mbufs prepared for the burst "
                         "no. %u: %u (should be %u)",
                         i, n_mbufs, burst_size_per_txq);
    }

    TEST_STEP("Aggregate all the patterns to yield a single pattern");
    CHECK_RC(tapi_tad_aggregate_patterns(patterns, nb_tx_queues, &pattern));

    TEST_STEP("Create an RX CSAP on the TST host according to the template");
    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(tst_host->ta, 0,
                                                tst_if->if_name,
                                                TAD_ETH_RECV_DEF,
                                                template, &rx_csap));

    TEST_STEP("Start to capture traffic with the pattern prepared");
    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, rx_csap, pattern,
                                   TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_PACKETS |
                                   RCF_TRRECV_MISMATCH));

    TEST_STEP("Validate and send the bursts");
    for (i = 0; i < nb_tx_queues; ++i)
    {
        packets_sent += test_tx_prepare_and_burst(iut_rpcs,
                                                  iut_port->if_index, i,
                                                  bursts[i],
                                                  burst_size_per_txq);
        if (packets_sent != ((i + 1) * burst_size_per_txq))
            TEST_VERDICT("Cannot send the burst on the queue no. %u", i);
    }

    TEST_STEP("Stop capturing traffic");
    patterns_cnt = nb_tx_queues * burst_size_per_txq;
    memset(patterns_recv_nb, 0, patterns_cnt * sizeof(patterns_recv_nb[0]));
    memset(patterns_seqno, 0, patterns_cnt * sizeof(patterns_seqno[0]));
    CHECK_RC(test_rx_await_pkts_exec_cb(tst_host->ta, rx_csap,
                                nb_tx_queues * burst_size_per_txq, 0, &rcv_cb));
    CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL,
                                  &packets_matched));

    TEST_STEP("Verify patterns match consistency");
    check_patterns_match_consistency(&match_p);

    TEST_SUCCESS;

cleanup:
    if (bursts != NULL)
    {
        for (i = 0; i < nb_tx_queues; ++i)
        {
            if ((bursts[i] != NULL) &&
                (packets_sent < ((i + 1) * burst_size_per_txq)))
            {
                unsigned int j;

                for (j = packets_sent % burst_size_per_txq;
                     j < burst_size_per_txq;
                     ++j)
                    rpc_rte_pktmbuf_free(iut_rpcs, bursts[i][j]);
            }
        }
    }

    TEST_END;
}
/** @} */
