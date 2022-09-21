/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Transmit functionality
 */

/** @defgroup xmit-one_packet Send one packet
 * @ingroup xmit
 * @{
 *
 * @objective Make sure that a packet can be sent correctly
 *
 * @param tmpl                   Traffic template
 * @param payload_len            Payload length
 * @param inner_ip_cksum_offload Inner or no-tunnel IP checksum offload toggle
 * @param l4_cksum_offload       L4 checksum offload toggle
 * @param vlan_id                VLAN offload: VLAN ID (ON) or @c -1 (OFF)
 * @param tso_segsz              TSO: segment size > @c 0 (ON) or @c 0 (OFF)
 * @param segmentation           Mbuf segmentation pattern or an empty line
 *
 * @type conformance
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 *
 * Validate PMD Tx operation in general and various Tx offloads in particular.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "xmit/one_packet"

#include "dpdk_pmd_test.h"
#include "ndn_ipstack.h"
#include "tapi_cfg_base.h"
#include "tapi_ndn.h"
#include "tapi_rte_mbuf.h"
#include "tapi_tad.h"
#include "tapi_tcp.h"
#include "te_ethernet.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                       *iut_rpcs = NULL;
    tapi_env_host                        *tst_host = NULL;
    const struct if_nameindex            *iut_port = NULL;
    const struct if_nameindex            *tst_if = NULL;

    asn_value                            *tmpl;
    unsigned int                          payload_len;
    te_bool                               outer_ip_cksum_offload;
    te_bool                               inner_ip_cksum_offload;
    te_bool                               l4_cksum_offload;
    int                                   vlan_id;
    unsigned int                          tso_segsz;
    struct test_param_seg_ptrn            segmentation;

    struct test_ethdev_config             ec;
    struct tarpc_rte_eth_conf             eth_conf;
    te_tad_protocols_t                    hdrs[TAPI_NDN_NLEVELS];
    uint64_t                              offloads_adv;
    uint64_t                              outer_ip_cksum_ol;
    te_bool                               outer_ip_cksum_offload_supported;
    uint64_t                              inner_ip_cksum_ol;
    te_bool                               inner_ip_cksum_offload_supported;
    uint64_t                              inner_l4_cksum_ol;
    te_bool                               inner_l4_cksum_offload_supported;
    uint64_t                              vxlan_tso;
    te_bool                               vxlan_tso_supported;
    uint64_t                              geneve_tso;
    te_bool                               geneve_tso_supported;
    te_bool                               tunnel_aware;
    uint16_t                              bc;
    uint64_t                              test_offloads = 0;
    struct tarpc_rte_eth_txconf           txconf;
    struct tarpc_rte_eth_txconf          *txconfp;
    rpc_rte_mempool_p                     mp;
    rpc_rte_mbuf_p                       *mbufs;
    unsigned int                          nb_mbufs;
    asn_value                           **pkts_by_tmpl;
    unsigned int                          nb_pkts_by_tmpl;
    rpc_rte_mbuf_p                        m;
    uint64_t                              m_ol_flags;
    struct tarpc_rte_pktmbuf_tx_offload   m_tx_ol;
    uint16_t                              nb_segs;
    uint16_t                              nb_tx_desc;
    unsigned int                          m_eth_d_len;
    unsigned int                          m_hdrs_len;
    csap_handle_t                         rx_csap = CSAP_INVALID_HANDLE;
    asn_value                            *pkt;
    asn_value                            *ptrn;
    unsigned int                          nb_pkts_expected;
    uint16_t                              nb_prep;
    unsigned int                          nb_pkts_rx;
    unsigned int                          no_match_pkts;

    TEST_START;

    TEST_GET_PCO(iut_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_UINT_PARAM(payload_len);
    outer_ip_cksum_offload = TRUE; /* No dedicated parameter yet. */
    TEST_GET_BOOL_PARAM(inner_ip_cksum_offload);
    TEST_GET_BOOL_PARAM(l4_cksum_offload);
    TEST_GET_VLAN_ID_PARAM(vlan_id);
    TEST_GET_UINT_PARAM(tso_segsz);
    TEST_GET_MBUF_SEG_PTRN_PARAM(segmentation);

    TEST_STEP("Reconcile interdependent test parameters");
    if (outer_ip_cksum_offload && !inner_ip_cksum_offload)
        inner_ip_cksum_offload = TRUE;

    TEST_STEP("Prepare TEST_ETHDEV_INITIALIZED state");
    (void)test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ec);
    (void)test_rpc_rte_eth_make_eth_conf(iut_rpcs, iut_port->if_index,
                                         &eth_conf);
    ec.eth_conf = &eth_conf;
    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Conduct environment substitutions in the traffic template");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params, "iut_mac"));
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));

    TEST_STEP("Learn about protocol types present in the traffic template");
    CHECK_RC(tapi_ndn_tmpl_classify(tmpl, hdrs));

    TEST_STEP("Learn about offload capabilities supported by the PMD");
    offloads_adv = ec.dev_info.tx_offload_capa;
    outer_ip_cksum_ol = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_OUTER_IPV4_CKSUM_BIT);
    outer_ip_cksum_offload_supported = (offloads_adv & outer_ip_cksum_ol);
    inner_ip_cksum_ol = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_IPV4_CKSUM_BIT);
    inner_ip_cksum_offload_supported = (offloads_adv & inner_ip_cksum_ol);

    inner_l4_cksum_ol = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_UDP_CKSUM_BIT) |
                        (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_TCP_CKSUM_BIT);
    vxlan_tso = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_VXLAN_TNL_TSO_BIT);
    vxlan_tso_supported = (offloads_adv & vxlan_tso);
    geneve_tso = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_GENEVE_TNL_TSO_BIT);
    geneve_tso_supported = (offloads_adv & geneve_tso);

    /*
     * Some NICs may support inner UDP checksum offload and miss support
     * for TCP checksum offload, or vice versa. For the sake of simplicity,
     * demand that both offloads be advertised.
     */
    inner_l4_cksum_offload_supported = ((offloads_adv & inner_l4_cksum_ol) ==
                                        inner_l4_cksum_ol);

    tunnel_aware = outer_ip_cksum_offload_supported ||
                   (hdrs[TAPI_NDN_TUNNEL] == TE_PROTO_VXLAN &&
                    vxlan_tso_supported) ||
                   (hdrs[TAPI_NDN_TUNNEL] == TE_PROTO_GENEVE &&
                    geneve_tso_supported);

    /*
     * Dismiss tunnel awareness if the packet is of unknown tunnel type.
     *
     * NVGRE is typically supported by NICs, however, not all driver
     * implementations can handle it since DPDK lacks unambiguous
     * designation for this offload making it hard to tell NVGRE
     * from plain GRE. Treat NVGRE inner frames as opaque data.
     */
    if (hdrs[TAPI_NDN_TUNNEL] != TE_PROTO_INVALID &&
        hdrs[TAPI_NDN_TUNNEL] != TE_PROTO_VXLAN &&
        hdrs[TAPI_NDN_TUNNEL] != TE_PROTO_GENEVE)
        tunnel_aware = FALSE;

    if (hdrs[TAPI_NDN_TUNNEL] != TE_PROTO_INVALID && tso_segsz == 0)
    {
        if (hdrs[TAPI_NDN_OUTER_L3] == TE_PROTO_IP4 &&
            ((tunnel_aware && outer_ip_cksum_offload &&
              outer_ip_cksum_offload_supported) ||
             (!tunnel_aware && inner_ip_cksum_offload &&
              inner_ip_cksum_offload_supported)))
        {
            TEST_STEP("Spoil outer IP checksum in the traffic template");
            CHECK_RC(tapi_ndn_tmpl_set_ip_cksum(tmpl, 0, TAPI_NDN_OUTER_L3));
        }

        /*
         * There's no dedicated test parameter for outer L4
         * checksum offload, so spoil outer UDP checksum in
         * accordance with inner checksum offload specifics.
         */
        if (hdrs[TAPI_NDN_OUTER_L4] == TE_PROTO_UDP &&
            ((l4_cksum_offload && inner_l4_cksum_offload_supported) ||
             (tunnel_aware && tso_segsz > 0)))
        {
            bc = (hdrs[TAPI_NDN_OUTER_L3] == TE_PROTO_IP4) ?
                 TE_IP4_UPPER_LAYER_CSUM_ZERO : TE_IP6_UPPER_LAYER_CSUM_ZERO;

            TEST_STEP("Force zero outer L4 checksum in the traffic template");
            CHECK_RC(tapi_ndn_tmpl_set_udp_cksum(tmpl, bc, TAPI_NDN_OUTER_L4));
        }
    }

    if ((hdrs[TAPI_NDN_TUNNEL] == TE_PROTO_INVALID || tunnel_aware) &&
        tso_segsz == 0)
    {
        if (inner_ip_cksum_offload && inner_ip_cksum_offload_supported &&
            hdrs[TAPI_NDN_INNER_L3] == TE_PROTO_IP4)
        {
            TEST_STEP("Spoil inner IP checksum in the traffic template");
            CHECK_RC(tapi_ndn_tmpl_set_ip_cksum(tmpl, 0, TAPI_NDN_INNER_L3));
        }

        if (l4_cksum_offload && inner_l4_cksum_offload_supported)
        {
            bc = (hdrs[TAPI_NDN_INNER_L3] == TE_PROTO_IP4) ?
                 TE_IP4_UPPER_LAYER_CSUM_BAD : TE_IP6_UPPER_LAYER_CSUM_BAD;

            TEST_STEP("Spoil inner L4 checksum in the traffic template");
            if (hdrs[TAPI_NDN_INNER_L4] == TE_PROTO_TCP)
            {
                CHECK_RC(tapi_ndn_tmpl_set_tcp_cksum(tmpl, bc));
            }
            else if (hdrs[TAPI_NDN_INNER_L4] == TE_PROTO_UDP)
            {
                CHECK_RC(tapi_ndn_tmpl_set_udp_cksum(tmpl, bc,
                                                     TAPI_NDN_INNER_L4));
            }
        }
    }

    TEST_STEP("Set payload length in the traffic template");
    CHECK_RC(tapi_ndn_tmpl_set_payload_len(tmpl, payload_len));

    if (hdrs[TAPI_NDN_TUNNEL] != TE_PROTO_INVALID && tunnel_aware)
    {
        TEST_STEP("Configure tunnel UDP port number in the PMD");
        CHECK_RC(test_add_tunnel_udp_port_from_tmpl(&ec, tmpl, FALSE));
    }

    test_offloads = ec.eth_conf->txmode.offloads;

    if (vlan_id >= 0)
    {
        TEST_STEP("Enable VLAN offload in the PMD");
        test_offloads |= (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_VLAN_INSERT_BIT);
    }

    if (outer_ip_cksum_offload && outer_ip_cksum_offload_supported &&
        hdrs[TAPI_NDN_OUTER_L3] == TE_PROTO_IP4)
    {
        TEST_STEP("Enable outer IP checksum offload in the PMD");
        test_offloads |= outer_ip_cksum_ol;
    }

    if (inner_ip_cksum_offload && inner_ip_cksum_offload_supported &&
        hdrs[TAPI_NDN_INNER_L3] == TE_PROTO_IP4)
    {
        TEST_STEP("Enable inner IP checksum offload in the PMD");
        test_offloads |= inner_ip_cksum_ol;
    }

    if (l4_cksum_offload && inner_l4_cksum_offload_supported &&
        hdrs[TAPI_NDN_INNER_L4] != TE_PROTO_INVALID)
    {
        TEST_STEP("Enable inner L4 checksum offload in the PMD");
        test_offloads |= inner_l4_cksum_ol;
    }

    if (tso_segsz > 0)
    {
        TEST_STEP("Enable TSO in the PMD");
        if (hdrs[TAPI_NDN_TUNNEL] == TE_PROTO_VXLAN)
            test_offloads |= vxlan_tso;
        else if (hdrs[TAPI_NDN_TUNNEL] == TE_PROTO_GENEVE)
            test_offloads |= geneve_tso;
        else
            test_offloads |= (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_TCP_TSO_BIT);
    }

    if (segmentation.nb_seg_groups > 0)
    {
        TEST_STEP("Enable multisegment mbuf support in the PMD");
        test_offloads |= (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_MULTI_SEGS_BIT);
    }

    txconfp = &txconf;
    ec.tx_confs = &txconfp;
    memset(&txconf, 0, sizeof(txconf));

    rc = test_mk_txmode_txconf(&ec, test_offloads, &eth_conf.txmode,
                               ec.tx_confs[0]);
    if (rc == TE_EINVAL)
        TEST_SKIP("");

    CHECK_RC(rc);

    TEST_STEP("Prepare TEST_ETHDEV_RX_SETUP_DONE state");
    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_CONFIGURED));

    mp = test_rte_pktmbuf_rx_pool_create(iut_rpcs, iut_port->if_index,
                                         &ec.dev_info,
                                         TEST_PKTS_MEMPOOL_NAME,
                                         TEST_RTE_MEMPOOL_DEF_SIZE,
                                         TEST_RTE_MEMPOOL_DEF_CACHE,
                                         TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                         MAX(TEST_DEV_HEADER_SIZE +
                                             MAX(payload_len, tso_segsz),
                                             ETHER_HDR_LEN + ETHER_DATA_LEN),
                                         ec.socket_id);
    ec.mp = mp;
    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_RX_SETUP_DONE));

    TEST_STEP("Generate mbuf and a packet sample from the traffic template");
    tapi_rte_mk_mbufs_by_tmpl_get_pkts(iut_rpcs, tmpl, mp, &mbufs, &nb_mbufs,
                                       &pkts_by_tmpl, &nb_pkts_by_tmpl);

    m = mbufs[0];
    m_ol_flags = 0;
    rpc_rte_pktmbuf_get_tx_offload(iut_rpcs, m, &m_tx_ol);

    if (vlan_id >= 0)
    {
        TEST_STEP("Add VLAN offload request to the mbuf");
        m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_VLAN);
        rpc_rte_pktmbuf_set_vlan_tci(iut_rpcs, m, (uint16_t)vlan_id);
    }

    if (hdrs[TAPI_NDN_TUNNEL] != TE_PROTO_INVALID)
    {
        if (tunnel_aware)
        {
            TEST_STEP("Add outer offload flags for the outer frame to the mbuf");

            if (hdrs[TAPI_NDN_TUNNEL] == TE_PROTO_VXLAN)
                m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_TUNNEL_VXLAN);
            else if (hdrs[TAPI_NDN_TUNNEL] == TE_PROTO_GENEVE)
                m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_TUNNEL_GENEVE);

            if (hdrs[TAPI_NDN_OUTER_L3] == TE_PROTO_IP4)
            {
                m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_OUTER_IPV4);

                if (outer_ip_cksum_offload && outer_ip_cksum_offload_supported)
                    m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_OUTER_IP_CKSUM);
            }
            else
            {
                m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_OUTER_IPV6);
            }
        }
        else
        {
            TEST_STEP("Reconcile inner header length fields in the mbuf so "
                      "that they refer to the outer header and fill zeros "
                      "in outer header length fields");

            m_tx_ol.l2_len = m_tx_ol.outer_l2_len;
            m_tx_ol.outer_l2_len = 0;

            m_tx_ol.l3_len = m_tx_ol.outer_l3_len;
            m_tx_ol.outer_l3_len = 0;

            m_tx_ol.l4_len = (hdrs[TAPI_NDN_OUTER_L4] == TE_PROTO_UDP) ?
                             TAD_UDP_HDR_LEN : 0;

            TEST_STEP("Add inner offload flags for the outer frame to the mbuf");

            if (hdrs[TAPI_NDN_OUTER_L3] == TE_PROTO_IP4)
            {
                m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_IPV4);

                if (inner_ip_cksum_offload && inner_ip_cksum_offload_supported)
                    m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_IP_CKSUM);
            }
            else
            {
                m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_IPV6);
            }

            if (l4_cksum_offload && inner_l4_cksum_offload_supported &&
                hdrs[TAPI_NDN_OUTER_L4] == TE_PROTO_UDP)
                    m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_UDP_CKSUM);
        }
    }

    if (hdrs[TAPI_NDN_TUNNEL] == TE_PROTO_INVALID || tunnel_aware)
    {
        TEST_STEP("Add inner offload flags for the inner frame to the mbuf");

        if (hdrs[TAPI_NDN_INNER_L3] == TE_PROTO_IP4)
        {
            m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_IPV4);

            if (inner_ip_cksum_offload && inner_ip_cksum_offload_supported)
                m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_IP_CKSUM);
        }
        else
        {
            m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_IPV6);
        }

        if (l4_cksum_offload && inner_l4_cksum_offload_supported)
        {
            if (hdrs[TAPI_NDN_INNER_L4] == TE_PROTO_TCP)
                m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_TCP_CKSUM);
            else if (hdrs[TAPI_NDN_INNER_L4] == TE_PROTO_UDP)
                m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_UDP_CKSUM);
        }
    }

    if (tso_segsz > 0)
    {
        TEST_STEP("Add TSO request to the mbuf");
        m_ol_flags |= (1ULL << TARPC_RTE_MBUF_F_TX_TCP_SEG);
        m_tx_ol.tso_segsz = tso_segsz;
    }

    rpc_rte_pktmbuf_set_flags(iut_rpcs, m, m_ol_flags);
    rpc_rte_pktmbuf_set_tx_offload(iut_rpcs, m, &m_tx_ol);

    if (segmentation.nb_seg_groups > 0)
    {
        TEST_STEP("Redistribute the packet data across multiple mbuf segments");
        (void)rpc_rte_pktmbuf_redist(iut_rpcs, &m, segmentation.seg_groups,
                                     (uint8_t)segmentation.nb_seg_groups);
    }

    nb_segs = rpc_rte_pktmbuf_get_nb_segs(iut_rpcs, m);
    nb_tx_desc = MAX(nb_segs + test_get_extra_tx_descs_per_pkt(),
                     ec.dev_info.tx_desc_lim.nb_min);
    if (ec.dev_info.tx_desc_lim.nb_align > 0)
        nb_tx_desc = TE_ALIGN(nb_tx_desc, ec.dev_info.tx_desc_lim.nb_align);

    if (ec.dev_info.tx_desc_lim.nb_max > 0 &&
        nb_tx_desc > ec.dev_info.tx_desc_lim.nb_max)
        TEST_VERDICT("Too many Tx descriptors required to send segmented mbuf");

    if (tso_segsz > 0)
    {
        TEST_STEP("Override the number of descriptors by the maximum supported "
                  "value since the NIC may need additional descriptor room for "
                  "TSO transaction while being unable to report this in limits");
        nb_tx_desc = ec.dev_info.tx_desc_lim.nb_max;
    }

    TEST_STEP("Prepare TEST_ETHDEV_TX_SETUP_DONE state");
    rpc_rte_eth_tx_queue_setup(iut_rpcs, iut_port->if_index, 0,
                               nb_tx_desc, ec.socket_id,
                               ec.tx_confs[0]);
    ec.cur_state = TEST_ETHDEV_TX_SETUP_DONE;

    if (tso_segsz > 0)
    {
        if (m_tx_ol.outer_l2_len != 0)
        {
            m_eth_d_len = m_tx_ol.outer_l3_len + m_tx_ol.l2_len +
                          m_tx_ol.l3_len + m_tx_ol.l4_len + tso_segsz;
        }
        else
        {
            m_eth_d_len = m_tx_ol.l3_len + m_tx_ol.l4_len + tso_segsz;
        }

        m_hdrs_len = ETHER_HDR_LEN + (vlan_id >= 0 ? 4 : 0) +
                     m_eth_d_len - tso_segsz;
    }
    else
    {
        size_t l2_len = (m_tx_ol.outer_l2_len != 0) ? m_tx_ol.outer_l2_len :
                                                      m_tx_ol.l2_len;

        m_eth_d_len = rpc_rte_pktmbuf_get_pkt_len(iut_rpcs, m) - l2_len;
    }

    if (m_eth_d_len > ETHER_DATA_LEN /* Standard Ethernet MTU of 1500 bytes */)
    {
        TEST_STEP("Enlarge MTU on both ends to cope with big frame(s)");
        test_set_mtu(iut_rpcs, iut_port->if_index, m_eth_d_len, &ec);
        CHECK_RC(tapi_cfg_base_if_set_mtu_leastwise(tst_host->ta,
                                                    tst_if->if_name,
                                                    m_eth_d_len));
    }

    TEST_STEP("Prepare TEST_ETHDEV_STARTED state");
    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_STARTED));

    TEST_STEP("Validate Tx offloads for the packet");
    nb_prep = rpc_rte_eth_tx_prepare(iut_rpcs, iut_port->if_index, 0, &m, 1);
    if (nb_prep == 0)
        TEST_VERDICT("Tx offloads for the packet were rejected");
    else if (nb_prep != 1)
        TEST_VERDICT("Wrong return value from Tx prepare API: "
                     "expected 0 or 1, got %" PRIu16, nb_prep);

    TEST_STEP("Create an Rx CSAP on the TST host according to the template");
    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(tst_host->ta, 0,
                                                tst_if->if_name,
                                                TAD_ETH_RECV_DEF,
                                                tmpl, &rx_csap));

    pkt = pkts_by_tmpl[0];

    if (vlan_id >= 0)
    {
        TEST_STEP("Inject VLAN tag to the packet sample");
        CHECK_RC(tapi_ndn_pkt_inject_vlan_tag(pkt, (uint16_t)vlan_id));
    }

    if (hdrs[TAPI_NDN_TUNNEL] != TE_PROTO_INVALID)
    {
        TEST_STEP("Replace checksums in the outer frame of the packet sample "
                  "with script values to demand that the checksums in the "
                  "packet(s) received by peer be correct");
        if (tunnel_aware)
        {
            if (hdrs[TAPI_NDN_OUTER_L3] == TE_PROTO_IP4 &&
                ((tso_segsz != 0) || (outer_ip_cksum_offload &&
                                      outer_ip_cksum_offload_supported)))
            {
                CHECK_RC(tapi_ndn_pkt_demand_correct_ip_cksum(
                                                    pkt, TAPI_NDN_OUTER_L3));
            }

            if (hdrs[TAPI_NDN_OUTER_L4] == TE_PROTO_UDP &&
                ((l4_cksum_offload && inner_l4_cksum_offload_supported) ||
                 tso_segsz > 0))
            {
                /*
                 * Tunnel-aware NICs may support and may not support
                 * outer UDP checksum offload. In the latter case,
                 * expect NICs to set the checksum to zero, as
                 * per RFC 7348, RFC 6935 and Geneve draft,
                 * so spoil the checksum in the template.
                 */
                CHECK_RC(tapi_ndn_pkt_demand_correct_udp_cksum(
                                                pkt, TRUE, TAPI_NDN_OUTER_L4));
            }
        }
        else
        {
            if (hdrs[TAPI_NDN_OUTER_L3] == TE_PROTO_IP4 &&
                ((tso_segsz != 0) || (inner_ip_cksum_offload &&
                                      inner_ip_cksum_offload_supported)))
            {
                CHECK_RC(tapi_ndn_pkt_demand_correct_ip_cksum(
                                                    pkt, TAPI_NDN_OUTER_L3));
            }

            if (l4_cksum_offload && inner_l4_cksum_offload_supported &&
                hdrs[TAPI_NDN_OUTER_L4] == TE_PROTO_UDP)
            {
                /*
                 * Tunnel-aware NICs may support and may not support
                 * outer UDP checksum offload. In the latter case,
                 * expect NICs to set the checksum to zero, as
                 * per RFC 7348, RFC 6935 and Geneve draft.
                 */
                CHECK_RC(tapi_ndn_pkt_demand_correct_udp_cksum(
                                            pkt, FALSE, TAPI_NDN_OUTER_L4));
            }
        }
    }

    if (hdrs[TAPI_NDN_TUNNEL] == TE_PROTO_INVALID || tunnel_aware)
    {
        TEST_STEP("Replace checksums in the inner frame of the packet sample "
                  "with script values to demand that the checksums in the "
                  "packet(s) received by the peer be correct");

        if (hdrs[TAPI_NDN_INNER_L3] == TE_PROTO_IP4 &&
            ((tso_segsz != 0) || (inner_ip_cksum_offload &&
                                  inner_ip_cksum_offload_supported)))
        {
            CHECK_RC(tapi_ndn_pkt_demand_correct_ip_cksum(
                                                    pkt, TAPI_NDN_INNER_L3));
        }

        if (l4_cksum_offload && inner_l4_cksum_offload_supported)
        {
            if (hdrs[TAPI_NDN_INNER_L4] == TE_PROTO_TCP)
            {
                CHECK_RC(tapi_ndn_pkt_demand_correct_tcp_cksum(pkt));
            }
            else if (hdrs[TAPI_NDN_INNER_L4] == TE_PROTO_UDP)
            {
                CHECK_RC(tapi_ndn_pkt_demand_correct_udp_cksum(
                                            pkt, FALSE, TAPI_NDN_INNER_L4));
            }
        }
    }

    if (tso_segsz > 0)
    {
        asn_value    **gso_pkts;
        unsigned int   nb_gso_pkts;
        struct tapi_ndn_gso_conf gso_conf = {
            .payload_barrier = test_get_tso_payload_cutoff_barrier(m_hdrs_len),
        };

        TEST_STEP("Conduct TSO edits for the packet sample");

        CHECK_RC(tapi_ndn_superframe_gso(pkt, tso_segsz, &gso_conf,
                                         &gso_pkts, &nb_gso_pkts));

        if (hdrs[TAPI_NDN_TUNNEL] != TE_PROTO_INVALID)
        {
            CHECK_RC(tapi_ndn_gso_pkts_ip_len_edit(gso_pkts, nb_gso_pkts,
                                                   hdrs[TAPI_NDN_OUTER_L3],
                                                   TAPI_NDN_OUTER_L3));
            if (hdrs[TAPI_NDN_OUTER_L3] == TE_PROTO_IP4)
            {
                CHECK_RC(tapi_ndn_gso_pkts_ip_id_edit(
                            gso_pkts, nb_gso_pkts, TAPI_NDN_OUTER_L3));
            }

            CHECK_RC(tapi_ndn_gso_pkts_udp_len_edit(gso_pkts, nb_gso_pkts,
                                                    TAPI_NDN_OUTER_L4));
        }

        CHECK_RC(tapi_ndn_gso_pkts_ip_len_edit(gso_pkts, nb_gso_pkts,
                                               hdrs[TAPI_NDN_INNER_L3],
                                               TAPI_NDN_INNER_L3));

        if (hdrs[TAPI_NDN_INNER_L3] == TE_PROTO_IP4)
        {
            CHECK_RC(tapi_ndn_gso_pkts_ip_id_edit(gso_pkts, nb_gso_pkts,
                                                  TAPI_NDN_INNER_L3));
        }

        CHECK_RC(tapi_ndn_tso_pkts_edit(gso_pkts, nb_gso_pkts));

        TEST_STEP("Prepare a traffic pattern from TSO packet samples");
        CHECK_RC(tapi_ndn_pkts_to_ptrn(gso_pkts, nb_gso_pkts, &ptrn));
        nb_pkts_expected = nb_gso_pkts;
    }
    else
    {
        TEST_STEP("Prepare a traffic pattern from the packet sample");
        CHECK_RC(tapi_ndn_pkts_to_ptrn(&pkt, 1, &ptrn));
        nb_pkts_expected = 1;
    }

    TEST_STEP("Start to capture traffic with the pattern prepared");
    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, rx_csap, ptrn,
                                   TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_SEQ_MATCH | RCF_TRRECV_MISMATCH));

    TEST_STEP("Send the packet");
    if (rpc_rte_eth_tx_burst(iut_rpcs, iut_port->if_index, 0, &m, 1) != 1)
        TEST_VERDICT("Cannot send the packet");

    CHECK_RC(test_rx_await_pkts(tst_host->ta, rx_csap, nb_pkts_expected, 0));
    CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL, &nb_pkts_rx));

    TEST_STEP("Check that no extra packets are received on Tester");
    CHECK_RC(tapi_tad_csap_get_no_match_pkts(tst_host->ta, 0, rx_csap,
                                             &no_match_pkts));
    if (no_match_pkts != 0)
        TEST_VERDICT("%u unmatched packets were received", no_match_pkts);

    TEST_STEP("Verify the number of matching packets received");
    CHECK_MATCHED_PACKETS_NUM(nb_pkts_rx, nb_pkts_expected);

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
