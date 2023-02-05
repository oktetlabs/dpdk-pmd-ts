/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Transmit functionality
 */

/** @defgroup xmit-one_packet_with_dpdk_rx Send one packet and request offloads
 * @ingroup xmit
 * @{
 *
 * @objective Evaluate transmit operation correctness by sending one packet
 *
 * @param tmpl               Traffic template
 * @param pld_size           Payload size
 * @param vlan               VLAN offload: VLAN ID (ON) or @c -1 (OFF)
 * @param outer_ip_cksum     Outer IP checksum offload toggle
 * @param outer_udp_cksum    Outer UDP checksum offload toggle
 * @param innermost_ip_cksum Innermost IP checksum offload toggle
 * @param innermost_l4_cksum Innermost L4 checksum offload toggle
 * @param tso                TSO: segment size (ON) or @c 0 (OFF)
 * @param segmentation       Mbuf segmentation pattern or an empty line
 *
 * @type conformance
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "xmit/one_packet_with_dpdk_rx"

#if HAVE_ASSERT_H
#include <assert.h>
#endif /* HAVE_ASSERT_H */

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"
#include "tapi_ndn.h"
#include "tapi_rte_mbuf.h"
#include "te_alloc.h"
#include "te_defs.h"

static void
check_match(const struct tarpc_rte_mbuf_report *report)
{
    switch (report->match_status)
    {
        case TARPC_RTE_MBUF_MATCH_TX_RX_MATCHED:
            break;
        case TARPC_RTE_MBUF_MATCH_TX_RX_VLAN_MISMATCH:
            TEST_VERDICT("Packet #%u has mismatched VLAN ID",
                         report->mismatch_idx);
            break;
        case TARPC_RTE_MBUF_MATCH_TX_RX_UNEXPECTED_PACKET:
            TEST_VERDICT("Packet #%u is not expected",
                         report->mismatch_idx);
            break;
        case TARPC_RTE_MBUF_MATCH_TX_RX_LESS_DATA:
            TEST_VERDICT("Not enough data to match packet #%u",
                         report->mismatch_idx);
            break;
        case TARPC_RTE_MBUF_MATCH_TX_RX_INCONISTENT_TSO_OFFSET:
            TEST_VERDICT("Packet #%u has inconsistent TSO cutoff offset",
                         report->mismatch_idx);
            break;
        case TARPC_RTE_MBUF_MATCH_TX_RX_PAYLOAD_MISMATCH:
            TEST_VERDICT("Packet #%u has mismatched payload",
                         report->mismatch_idx);
            break;
        case TARPC_RTE_MBUF_MATCH_TX_RX_HEADER_MISMATCH:
            TEST_VERDICT("Packet #%u has mismatched header",
                         report->mismatch_idx);
            break;
        default:
            TEST_VERDICT("Failed to match packets for an unexpected reason");
            break;
    }
}

int
main(int argc, char *argv[])
{
    const struct if_nameindex *iut_port = NULL;
    rcf_rpc_server            *iut_rpcs = NULL;
    tapi_env_host             *tst_host = NULL;
    rcf_rpc_server            *tst_rpcs = NULL;
    const struct if_nameindex *tst_if = NULL;

    te_bool                     innermost_ip_cksum;
    te_bool                     innermost_l4_cksum;
    te_bool                     outer_udp_cksum;
    te_bool                     outer_ip_cksum;
    struct test_param_seg_ptrn  segmentation;
    unsigned int                pld_size;
    int                         vlan;
    asn_value                  *tmpl;
    unsigned int                tso;

    te_tad_protocols_t                   tmpl_protocols[TAPI_NDN_NLEVELS];
    rpc_rte_mbuf_p                       rx_burst_garbage[BURST_SIZE];
    size_t                               mp_tst_data_room;
    size_t                               mp_tst_elt_count;
    size_t                               m_tx_hdr_size;
    uint32_t                             m_rx_size_max;
    struct tarpc_rte_pktmbuf_tx_offload  m_tx_offload;
    struct test_ethdev_config            test_ethdev;
    struct tarpc_rte_eth_txconf         *eth_txconfp;
    struct tarpc_rte_eth_txconf          eth_txconf;
    uint64_t                             m_tx_flags;
    uint16_t                             nb_tx_desc;
    uint16_t                             port_id_rx;
    uint32_t                             m_tx_size;
    unsigned int                         nb_rx_exp;
    unsigned int                         nb_rx = 0;
    struct tarpc_rte_eth_conf            eth_conf;
    uint64_t                             offloads;
    rpc_rte_mbuf_p                      *mbufs_tx;
    rpc_rte_mbuf_p                       m_tx_tst;
    rpc_rte_mbuf_p                      *rx_burst;
    uint16_t                             nb_segs;
    uint16_t                             nb_prep_exp;
    uint16_t                             nb_prep;
    rpc_rte_mempool_p                    mp_iut;
    rpc_rte_mempool_p                    mp_tst;
    struct tarpc_rte_mbuf_report         report;
    unsigned int                         nb_tx;
    rpc_rte_mbuf_p                       m_tx;

    TEST_START;

    TEST_GET_HOST(tst_host);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_GET_MBUF_SEG_PTRN_PARAM(segmentation);
    TEST_GET_BOOL_PARAM(innermost_ip_cksum);
    TEST_GET_BOOL_PARAM(innermost_l4_cksum);
    TEST_GET_BOOL_PARAM(outer_udp_cksum);
    TEST_GET_BOOL_PARAM(outer_ip_cksum);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_UINT_PARAM(pld_size);
    TEST_GET_VLAN_ID_PARAM(vlan);
    TEST_GET_UINT_PARAM(tso);

    TEST_STEP("Learn about protocol types from the traffic template");
    CHECK_RC(tapi_ndn_tmpl_classify(tmpl, tmpl_protocols));

    TEST_STEP("Prepare state TEST_ETHDEV_INITIALIZED");
    CHECK_NOT_NULL(test_prepare_config_def_mk(&env, iut_rpcs, iut_port,
                                              &test_ethdev));
    CHECK_NOT_NULL(test_rpc_rte_eth_make_eth_conf(iut_rpcs, iut_port->if_index,
                                                  &eth_conf));
    test_ethdev.eth_conf = &eth_conf;
    CHECK_RC(test_prepare_ethdev(&test_ethdev, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Attempt to configure tunnel UDP port number in the PMD");
    CHECK_RC(test_add_tunnel_udp_port_from_tmpl(&test_ethdev, tmpl, FALSE));

    offloads = test_ethdev.eth_conf->txmode.offloads;

    if (vlan >= 0)
    {
        TEST_STEP("Enable VLAN offload in the PMD");
        offloads |= (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_VLAN_INSERT_BIT);
    }

    if (outer_ip_cksum)
    {
        TEST_STEP("Enable outer IP checksum offload in the PMD");
        offloads |= (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_OUTER_IPV4_CKSUM_BIT);
    }

    if (outer_udp_cksum)
    {
        TEST_STEP("Enable outer UDP checksum offload in the PMD");
        offloads |= (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_OUTER_UDP_CKSUM_BIT);
    }

    if (innermost_ip_cksum)
    {
        uint64_t ol_ipv4_cs_capa;
        uint64_t ol_ipv4_cs;

        ol_ipv4_cs = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_IPV4_CKSUM_BIT);
        ol_ipv4_cs_capa = test_ethdev.dev_info.tx_offload_capa & ol_ipv4_cs;

        if (tso != 0 && ol_ipv4_cs_capa == 0)
        {
            /*
             * In VirtIO use case, bona fide Tx IPv4 checksum offload is
             * unsupported on device / queue level, so, not surprisingly,
             * adding the offload bit over here would make the test fail.
             *
             * For TSO transactions, however, this offload is implicitly
             * carried out in the form of SW-assisted incremental update.
             * In order to let the test proceed and verify this use case,
             * skip adding the offload bit to the offload mask over here.
             */
        }
        else
        {
            TEST_STEP("Enable innermost IP checksum offload in the PMD");
            offloads |= ol_ipv4_cs;
        }
    }

    if (innermost_l4_cksum)
    {
        TEST_STEP("Enable innermost L4 checksum offload in the PMD");
        offloads |= (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_TCP_CKSUM_BIT);
        offloads |= (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_UDP_CKSUM_BIT);
    }

    if (tso != 0)
    {
        uint64_t ol_tso;

        switch (tmpl_protocols[TAPI_NDN_TUNNEL])
        {
            case TE_PROTO_INVALID:
                TEST_STEP("Enable non-tunnel TSO in the PMD");
                ol_tso = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_TCP_TSO_BIT);
                break;

            case TE_PROTO_VXLAN:
                TEST_STEP("Enable VXLAN TSO in the PMD");
                ol_tso = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_VXLAN_TNL_TSO_BIT);
                break;

            case TE_PROTO_GENEVE:
                TEST_STEP("Enable Geneve TSO in the PMD");
                ol_tso = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_GENEVE_TNL_TSO_BIT);
                break;

            default:
                assert(FALSE);
                break;
        }

        offloads |= ol_tso;
    }

    if (segmentation.nb_seg_groups > 0)
    {
        TEST_STEP("Enable multi-seg mbuf support in the PMD");
        offloads |= (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_MULTI_SEGS_BIT);
    }

    eth_txconfp = &eth_txconf;
    test_ethdev.tx_confs = &eth_txconfp;
    memset(&eth_txconf, 0, sizeof(eth_txconf));

    rc = test_mk_txmode_txconf(&test_ethdev, offloads, &eth_conf.txmode,
                               test_ethdev.tx_confs[0]);
    if (rc == TE_EINVAL)
        TEST_SKIP("");

    CHECK_RC(rc);

    TEST_STEP("Prepare state TEST_ETHDEV_CONFIGURED");
    CHECK_RC(test_prepare_ethdev(&test_ethdev, TEST_ETHDEV_CONFIGURED));

    TEST_STEP("Prepare state TEST_ETHDEV_RX_SETUP_DONE");
    mp_iut = test_rte_pktmbuf_rx_pool_create(iut_rpcs, iut_port->if_index,
                                             &test_ethdev.dev_info,
                                             TEST_PKTS_MEMPOOL_NAME,
                                             TEST_RTE_MEMPOOL_DEF_SIZE,
                                             TEST_RTE_MEMPOOL_DEF_CACHE,
                                             TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                             UINT16_MAX, test_ethdev.socket_id);
    test_ethdev.mp = mp_iut;
    CHECK_RC(test_prepare_ethdev(&test_ethdev, TEST_ETHDEV_RX_SETUP_DONE));

    TEST_STEP("Cook the traffic template");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params, "iut_mac"));
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_ndn_tmpl_set_payload_len(tmpl, 0));

    TEST_STEP("Generate and customise a packet mbuf");
    CHECK_RC(rpc_rte_mk_mbuf_from_template(iut_rpcs, tmpl, test_ethdev.mp,
                                           &mbufs_tx, &nb_tx));
    m_tx = mbufs_tx[0];

    CHECK_RC(test_tx_mbuf_set_random_payload(iut_rpcs, pld_size, m_tx));

    m_tx_flags = rpc_rte_pktmbuf_get_flags(iut_rpcs, m_tx);
    rpc_rte_pktmbuf_get_tx_offload(iut_rpcs, m_tx, &m_tx_offload);

    if (vlan >= 0)
    {
        TEST_STEP("Add VLAN offload request to the mbuf");
        m_tx_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_VLAN);
        rpc_rte_pktmbuf_set_vlan_tci(iut_rpcs, m_tx, (uint16_t)vlan);
    }

    if (outer_ip_cksum && tmpl_protocols[TAPI_NDN_OUTER_L3] == TE_PROTO_IP4)
    {
        TEST_STEP("Add outer IP checksum offload request to the mbuf");
        m_tx_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_OUTER_IP_CKSUM);
    }

    if (outer_udp_cksum && tmpl_protocols[TAPI_NDN_OUTER_L4] == TE_PROTO_UDP)
    {
        TEST_STEP("Add outer UDP checksum offload request to the mbuf");
        m_tx_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_OUTER_UDP_CKSUM);
    }

    if (innermost_ip_cksum && tmpl_protocols[TAPI_NDN_INNER_L3] == TE_PROTO_IP4)
    {
        TEST_STEP("Add innermost IP checksum offload request to the mbuf");
        m_tx_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_IP_CKSUM);
    }

    if (innermost_l4_cksum && tmpl_protocols[TAPI_NDN_INNER_L4] == TE_PROTO_TCP)
    {
        TEST_STEP("Add TCP checksum offload request to the mbuf");
        m_tx_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_TCP_CKSUM);
    }

    if (innermost_l4_cksum && tmpl_protocols[TAPI_NDN_INNER_L4] == TE_PROTO_UDP)
    {
        TEST_STEP("Add innermost UDP checksum offload request to the mbuf");
        m_tx_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_UDP_CKSUM);
    }

    if (tso != 0)
    {
        TEST_STEP("Add TSO request to the mbuf");
        m_tx_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_TCP_SEG);
        m_tx_offload.tso_segsz = tso;
    }

    rpc_rte_pktmbuf_set_flags(iut_rpcs, m_tx, m_tx_flags);
    rpc_rte_pktmbuf_set_tx_offload(iut_rpcs, m_tx, &m_tx_offload);

    CHECK_RC(rpc_rte_mbuf_match_tx_rx_pre(iut_rpcs, m_tx));

    TEST_STEP("Clone the mbuf from IUT RPCS to TST RPCS");
    m_tx_size = rpc_rte_pktmbuf_get_pkt_len(iut_rpcs, m_tx);
    mp_tst_data_room = MIN(m_tx_size + TEST_RTE_MEMPOOL_DATA_ROOM_OVERHEAD,
                           UINT16_MAX);
    mp_tst_elt_count = TE_DIV_ROUND_UP(m_tx_size, mp_tst_data_room -
                                       TEST_RTE_MEMPOOL_DATA_ROOM_OVERHEAD);
    mp_tst = test_rte_pktmbuf_pool_create(tst_rpcs, "clone-mbuf-pool",
                                         mp_tst_elt_count, 0,
                                         TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                         mp_tst_data_room, 0);
    CHECK_RC(test_clone_mbuf(iut_rpcs, m_tx, tst_rpcs, mp_tst, &m_tx_tst));

    if (segmentation.nb_seg_groups > 0)
    {
        TEST_STEP("Redistribute the packet data across multiple mbuf segments");
        (void)rpc_rte_pktmbuf_redist(iut_rpcs, &m_tx, segmentation.seg_groups,
                                     (uint8_t)segmentation.nb_seg_groups);
    }

    TEST_STEP("Evaluate TxQ ring size");
    nb_segs = rpc_rte_pktmbuf_get_nb_segs(iut_rpcs, m_tx);
    nb_tx_desc = MAX(nb_segs + test_get_extra_tx_descs_per_pkt(),
                     test_ethdev.dev_info.tx_desc_lim.nb_min);
    if (test_ethdev.dev_info.tx_desc_lim.nb_align > 0)
        nb_tx_desc = TE_ALIGN(nb_tx_desc,
                              test_ethdev.dev_info.tx_desc_lim.nb_align);
    assert(test_ethdev.dev_info.tx_desc_lim.nb_max > 0);
    if (tso != 0)
    {
        TEST_STEP("Override the number of descriptors by the maximum supported "
                  "value since the NIC may need additional descriptor room for "
                  "TSO transaction while being unable to report this in limits");
        nb_tx_desc = test_ethdev.dev_info.tx_desc_lim.nb_max;
    }

    RING("Use %u as TxQ ring size", nb_tx_desc);
    if (nb_tx_desc > test_ethdev.dev_info.tx_desc_lim.nb_max)
        TEST_VERDICT("IUT: TxQ ring size is too large");

    TEST_STEP("Prepare state TEST_ETHDEV_TX_SETUP_DONE");
    CHECK_RC(rpc_rte_eth_tx_queue_setup(iut_rpcs, iut_port->if_index, 0,
                                        nb_tx_desc, test_ethdev.socket_id,
                                        test_ethdev.tx_confs[0]));
    test_ethdev.cur_state = TEST_ETHDEV_TX_SETUP_DONE;

    TEST_STEP("Prepare state TEST_ETHDEV_STARTED");
    CHECK_RC(test_prepare_ethdev(&test_ethdev, TEST_ETHDEV_STARTED));

    TEST_STEP("Check mbuf segmentation vs limits");
    nb_prep_exp = test_tx_mbuf_segs_good(iut_rpcs, m_tx,
                                         &test_ethdev.dev_info) ? 1 : 0;

    TEST_STEP("Sanity check the packet mbuf with Tx prepare API");
    nb_prep = rpc_rte_eth_tx_prepare(iut_rpcs, iut_port->if_index, 0, &m_tx, 1);
    if (nb_prep == 0)
    {
        if (nb_prep_exp == 0)
            TEST_SUCCESS;

        TEST_VERDICT("The packet mbuf cannot pass Tx prepare API checks");
    }
    else if (nb_prep != 1)
    {
        TEST_VERDICT("Bad return value by Tx prepare API: %" PRIu16, nb_prep);
    }
    else if (nb_prep_exp == 0)
    {
        ERROR_VERDICT("Packet has been accepted by Tx prepare unexpectedly");
    }

    TEST_STEP("Derive the maximum packet length and the "
              "number of packets to be seen on Rx side");
    m_tx_hdr_size = m_tx_offload.outer_l2_len + m_tx_offload.outer_l3_len +
                    m_tx_offload.l2_len + m_tx_offload.l3_len +
                    m_tx_offload.l4_len;

    if (tso != 0) {
        size_t chunk_size = m_tx_size - m_tx_hdr_size;

        m_rx_size_max = m_tx_size - pld_size + MIN(tso, pld_size);
        nb_rx_exp = MAX(TE_DIV_ROUND_UP(chunk_size, tso), 1);
    }
    else
    {
        m_rx_size_max = m_tx_size;
        nb_rx_exp = 1;
    }

    if (m_rx_size_max > ETHER_HDR_LEN + ETHER_DATA_LEN)
    {
        unsigned int mtu = m_rx_size_max - ETHER_HDR_LEN;

        TEST_STEP("Enlarge MTU on both ends");
        CHECK_RC(rpc_rte_eth_dev_set_mtu(iut_rpcs, iut_port->if_index, mtu));
        CHECK_RC(tapi_cfg_base_if_set_mtu_leastwise(tst_host->ta,
                                                    tst_if->if_name, mtu));
    }

    TEST_STEP("Deploy RTE af_packet on top of TST network interface");
    port_id_rx = test_rte_af_packet_on_tst_if_deploy(tst_rpcs, tst_if,
                                                     m_rx_size_max, nb_rx_exp);

    TEST_STEP("Squeeze out garbage packets possibly captured by RTE af_packet");
    (void)rpc_rte_eth_rx_burst(tst_rpcs, port_id_rx, 0, rx_burst_garbage,
                               TE_ARRAY_LEN(rx_burst_garbage));

    TEST_STEP("Send the packet");
    if (rpc_rte_eth_tx_burst(iut_rpcs, iut_port->if_index, 0, &m_tx, 1) != 1)
        TEST_VERDICT("The packet cannot be sent");

    TEST_STEP("Receive the packet(s)");
    rx_burst = TE_ALLOC(nb_rx_exp * sizeof(*rx_burst));
    CHECK_NOT_NULL(rx_burst);

    nb_rx = test_rx_burst_with_retries(tst_rpcs, port_id_rx, 0,
                                       rx_burst, nb_rx_exp, nb_rx_exp);
    CHECK_PACKETS_NUM(nb_rx, nb_rx_exp);

    TEST_STEP("Match Rx packet(s) and figure out what offloads took place");
    CHECK_RC(rpc_rte_mbuf_match_tx_rx(tst_rpcs, m_tx_tst, rx_burst,
                                      nb_rx, &report));
    check_match(&report);

    if (vlan >= 0 && report.ol_vlan == TARPC_RTE_MBUF_OL_NOT_DONE)
        TEST_VERDICT("VLAN offload did not happen");

    if (outer_ip_cksum &&
        report.ol_outer_ip_cksum == TARPC_RTE_MBUF_OL_NOT_DONE)
        TEST_VERDICT("Outer IP checksum offload did not happen");

    if (!outer_ip_cksum && report.ol_outer_ip_cksum == TARPC_RTE_MBUF_OL_DONE)
        WARN_VERDICT("Outer IP checksum offload happened unexpectedly");

    if (outer_udp_cksum &&
        report.ol_outer_udp_cksum == TARPC_RTE_MBUF_OL_NOT_DONE)
        TEST_VERDICT("Outer UDP checksum offload did not happen");

    if (!outer_udp_cksum && report.ol_outer_udp_cksum == TARPC_RTE_MBUF_OL_DONE)
        WARN_VERDICT("Outer UDP checksum offload happened unexpectedly");

    if (innermost_ip_cksum &&
        report.ol_innermost_ip_cksum == TARPC_RTE_MBUF_OL_NOT_DONE)
        TEST_VERDICT("Innermost IP checksum offload did not happen");

    if (!innermost_ip_cksum &&
        report.ol_innermost_ip_cksum == TARPC_RTE_MBUF_OL_DONE)
        WARN_VERDICT("Innermost IP checksum offload happened unexpectedly");

    if (innermost_l4_cksum &&
        report.ol_innermost_l4_cksum == TARPC_RTE_MBUF_OL_NOT_DONE)
        TEST_VERDICT("Innermost L4 checksum offload did not happen");

    if (!innermost_l4_cksum &&
        report.ol_innermost_l4_cksum == TARPC_RTE_MBUF_OL_DONE)
        WARN_VERDICT("Innermost L4 checksum offload happened unexpectedly");

    if (tso != 0)
    {
        int          tso_cutoff_barrier;
        cfg_val_type cvt = CVT_INTEGER;

        CHECK_RC(cfg_get_instance_fmt(&cvt, &tso_cutoff_barrier,
                                      "/local:/dpdk:/tso_cutoff_barrier:"));

        if (tso_cutoff_barrier != 0 && report.tso_cutoff_barrier == 0)
        {
            WARN_VERDICT("TSO cutoff barrier effect was expected but did not show up");
        }
        else if ((int)report.tso_cutoff_barrier != tso_cutoff_barrier)
        {
            TEST_VERDICT("TSO cutoff barrier mismatch: expected %d, got %u",
                         tso_cutoff_barrier, report.tso_cutoff_barrier);
        }
    }

    TEST_SUCCESS;

cleanup:
    test_rte_af_packet_on_tst_if_release(tst_rpcs, tst_if);

    TEST_END;
}
/** @} */
