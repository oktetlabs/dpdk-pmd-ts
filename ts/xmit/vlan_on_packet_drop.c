/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Transmit functionality
 */

/** @defgroup xmit-vlan_on_packet_drop Sanity check VLAN offload on packet drop
 * @ingroup xmit
 * @{
 *
 * @objective Check that VLAN offload is not broken by previous packet drop
 *
 * @param tmpl Traffic template
 *
 * @type conformance
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 *
 * PMDs tend to maintain offload state in software for various reasons, and
 * in the case of VLAN offload there is a possible pitfall when a packet is
 * dropped due to lack of room for descriptors: if this packet enables VLAN
 * offload, the PMD must make sure that its software state for that offload
 * is rolled back, otherwise the next packet with exactly the same VLAN TCI
 * will make the PMD assume that the HW is already set for this offload and
 * nothing has to be changed. In the latter case, the offload will be wrong.
 *
 * The test tries to reproduce this corner case by sending two packets with
 * identical VLAN IDs, the first one being wittingly sliced into more mbufs
 * than the number of descriptors in Tx ring and thus going to be dismissed.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "xmit/vlan_on_packet_drop"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

#include "tapi_ndn.h"
#include "tapi_tad.h"

#define TEST_NB_TX_DESCS 64

int
main(int argc, char *argv[])
{
    rcf_rpc_server                      *iut_rpcs;
    tapi_env_host                       *tst_host;
    const struct if_nameindex           *iut_port;
    const struct if_nameindex           *tst_if;
    asn_value                           *tmpl;

    struct test_ethdev_config            tec;
    struct tarpc_rte_eth_conf            econf;
    uint64_t                             test_offloads = 0;
    struct tarpc_rte_eth_txconf          txconf;
    struct tarpc_rte_eth_txconf         *txconfp;
    uint16_t                             nb_tx_descs;
    unsigned int                         payload_len;
    asn_value                           *tmpl_copy;
    uint16_t                             vlan_id;
    send_transform                       cond;
    rpc_rte_mbuf_p                      *mbufs;
    unsigned int                         nb_mbufs;
    rpc_rte_mbuf_p                       m;
    struct tarpc_rte_pktmbuf_tx_offload  m_tx_ol;
    uint32_t                             m_pkt_len;
    uint32_t                             m_eth_d_len;
    unsigned int                         nb_seg_groups;
    struct tarpc_pktmbuf_seg_group      *seg_groups;
    asn_value                           *pattern;
    csap_handle_t                        rx_csap = CSAP_INVALID_HANDLE;
    unsigned int                         nb_rx;
    unsigned int                         i;

    TEST_START;

    TEST_GET_PCO(iut_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);

    TEST_STEP("Prepare @c TEST_ETHDEV_INITIALIZED state.");
    (void)test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &tec);

    tec.nb_rx_queue = 1;
    tec.nb_tx_queue = 1;

    (void)test_rpc_rte_eth_make_eth_conf(iut_rpcs, iut_port->if_index, &econf);
    tec.eth_conf = &econf;

    CHECK_RC(test_prepare_ethdev(&tec, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Obtain the source Ethernet address.");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params, "iut_mac"));

    TEST_STEP("Fill in offloads needed by the test regardless whether these "
              "capabilities are supported by the current RTE release or not. "
              "Preserve automatically added device offloads.");
    test_offloads = tec.eth_conf->txmode.offloads;
    test_offloads |= (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_VLAN_INSERT_BIT);
    test_offloads |= (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_MULTI_SEGS_BIT);

    txconfp = &txconf;
    tec.tx_confs = &txconfp;

    TEST_STEP("Sort out the set of capabilities to use valid Tx mode "
              "configuration.");
    rc = test_mk_txmode_txconf(&tec, test_offloads, &econf.txmode, txconfp);
    if (rc == TE_EINVAL)
        TEST_SKIP("");

    CHECK_RC(rc);

    TEST_STEP("Prepare @c TEST_ETHDEV_RX_SETUP_DONE state.");
    CHECK_RC(test_prepare_ethdev(&tec, TEST_ETHDEV_CONFIGURED));

    tec.mp = test_rte_pktmbuf_rx_pool_create(iut_rpcs, iut_port->if_index,
                                             &tec.dev_info,
                                             TEST_PKTS_MEMPOOL_NAME,
                                             TEST_RTE_MEMPOOL_DEF_SIZE,
                                             TEST_RTE_MEMPOOL_DEF_CACHE,
                                             TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                             TEST_RTE_MEMPOOL_DEF_DATA_ROOM,
                                             tec.socket_id);

    CHECK_RC(test_prepare_ethdev(&tec, TEST_ETHDEV_RX_SETUP_DONE));

    TEST_STEP("Choose a reasonable number of Tx queue descriptors and set "
              "sufficient payload length for the first packet so that it "
              "can be sliced into as many segments as how many descriptors "
              "the queue has, plus one.");
    nb_tx_descs = MAX(TEST_NB_TX_DESCS, tec.dev_info.tx_desc_lim.nb_min);
    if (tec.dev_info.tx_desc_lim.nb_align > 0)
        nb_tx_descs = TE_ALIGN(nb_tx_descs, tec.dev_info.tx_desc_lim.nb_align);

    if (tec.dev_info.tx_desc_lim.nb_max > 0 &&
        nb_tx_descs > tec.dev_info.tx_desc_lim.nb_max)
        TEST_VERDICT("Too many Tx descriptors requested");

    payload_len = DPMD_TS_PAYLOAD_LEN_DEF + nb_tx_descs;

    TEST_STEP("Adjust the traffic template and make its copy.");
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    asn_write_value_field(tmpl, &payload_len, sizeof(payload_len),
                          "payload.#length");

    tmpl_copy = asn_copy_value(tmpl);
    CHECK_NOT_NULL(tmpl_copy);

    TEST_STEP("Setup the only Tx queue.");
    rpc_rte_eth_tx_queue_setup(iut_rpcs, iut_port->if_index, 0,
                               nb_tx_descs, tec.socket_id, txconfp);

    tec.cur_state = TEST_ETHDEV_TX_SETUP_DONE;

    TEST_STEP("Randomly select VLAN ID.");
    vlan_id = rand_range(1, TEST_MAX_VLAN_ID);

    TEST_STEP("Describe 'send transformations' so that it is possible to "
              "generate traffic pattern aware of VLAN offload on the next step.");
    memset(&cond, 0, sizeof(cond));
    cond.hw_flags |= SEND_COND_HW_OFFL_VLAN;
    cond.vlan_tci = vlan_id;

    TEST_STEP("Prepare the first packet mbuf.");
    tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, tmpl, tec.mp, &cond,
                                    &mbufs, &nb_mbufs, NULL);

    m = mbufs[0];

    TEST_STEP("Query header length fields of the packet; this result will stay "
              "valid for the second packet, too.");
    rpc_rte_pktmbuf_get_tx_offload(iut_rpcs, m, &m_tx_ol);

    TEST_STEP("Derive Ethernet payload length of the packet; this result will "
              "stay valid for the second packet, too.");
    m_pkt_len = rpc_rte_pktmbuf_get_pkt_len(iut_rpcs, m);
    m_eth_d_len = m_pkt_len - m_tx_ol.l2_len;

    TEST_STEP("Fix MTU on both ends.");
    rpc_rte_eth_dev_set_mtu(iut_rpcs, iut_port->if_index, m_eth_d_len);
    CHECK_RC(tapi_cfg_base_if_set_mtu_leastwise(tst_host->ta, tst_if->if_name,
                                                m_eth_d_len));

    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state.");
    CHECK_RC(test_prepare_ethdev(&tec, TEST_ETHDEV_STARTED));

    TEST_STEP("Slice the first packet mbuf into as many segments as how many "
              "descriptors the transmit queue has, plus one, so that the packet "
              "is going to be rejected.");
    nb_seg_groups = 1 + (nb_tx_descs + UINT8_MAX - 1) / UINT8_MAX;

    if (nb_seg_groups > UINT8_MAX)
        TEST_VERDICT("Too many segmentation groups requested");

    seg_groups = TE_ALLOC(nb_seg_groups * sizeof(*seg_groups));
    CHECK_NOT_NULL(seg_groups);

    seg_groups[0].len = m_pkt_len - nb_tx_descs;
    seg_groups[0].num = 1;

    for (i = 1; i < nb_seg_groups; ++i)
    {
        seg_groups[i].len = 1;
        seg_groups[i].num = UINT8_MAX;
    }

    if (rpc_rte_pktmbuf_redist(iut_rpcs, &m, seg_groups,
                               (uint8_t)nb_seg_groups) !=
        (nb_tx_descs + 1))
        TEST_VERDICT("Failed to slice the first packet");

    TEST_STEP("Try to send the first packet and make sure it "
              "gets dismissed. Don't validate the packet since "
              "Tx prepare API will reject it too early without "
              "any chance to face Tx burst API and check VLAN TCI "
              "rollback behaviour");
    if (rpc_rte_eth_tx_burst(iut_rpcs, iut_port->if_index, 0, &m, 1) != 0)
        TEST_VERDICT("The first packet was not rejected");

    rpc_rte_pktmbuf_free(iut_rpcs, m);

    TEST_STEP("Prepare the second packet mbuf.");
    tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, tmpl_copy, tec.mp, &cond,
                                     &mbufs, &nb_mbufs, &pattern);

    m = mbufs[0];

    TEST_STEP("Create an Rx CSAP on TST host according to the template.");
    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(tst_host->ta, 0,
                                                tst_if->if_name,
                                                TAD_ETH_RECV_DEF,
                                                tmpl_copy, &rx_csap));

    TEST_STEP("Start capturing traffic with the pattern prepared.");
    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, rx_csap, pattern,
                                   TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_PACKETS | RCF_TRRECV_MISMATCH));

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Validate and send the second packet and make sure it does not "
              "get dismissed.");
    if (test_tx_prepare_and_burst(iut_rpcs, iut_port->if_index, 0, &m, 1) != 1)
    {
        rpc_rte_pktmbuf_free(iut_rpcs, m);
        TEST_VERDICT("Failed to send the second packet");
    }

    TEST_STEP("Stop capturing traffic.");
    CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL, &nb_rx));

    TEST_STEP("Verify the number of matching packets received.");
    if (nb_rx == 0)
        TEST_VERDICT("Failed to receive the second packet");
    else if (nb_rx != 1)
        TEST_VERDICT("The number of captured packets is wrong: "
                  "expected 0 or 1, got %u", nb_rx);

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
