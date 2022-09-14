/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-rx_offload_checksum Rx checksum offload sanity check
 * @ingroup usecases
 * @{
 *
 * @objective Make sure that valid Rx checksum flags are put into mbufs
 *
 * @param tmpl        Traffic template
 * @param rx_ol_cksum Rx checksum offload toggle
 *
 * @type use case
 *
 * @author Roman Zhukov <Roman.Zhukov@oktetlabs.ru>
 *
 * The test receives one packet with valid checksums and one with wrong.
 * Then checksum status is verified for the packets by means of peeking
 * at the mbuf checksum flags.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "usecases/rx_offload_checksum"

#include "dpdk_pmd_test.h"
#include "ndn_ipstack.h"

static void
rx_ol_cksum_diff_flags_to_str(uint64_t expected_flags, uint64_t obtained_flags,
                              te_string *expected_flags_res,
                              te_string *obtained_flags_res)
{
    te_bool ip_mismatch = FALSE;

    #define TEST_CKSUM(_flag, _mask) \
        do {                                                                \
            uint64_t tarpc_flag = TARPC_##_flag;                            \
            if ((expected_flags & _mask) == tarpc_flag)                     \
                CHECK_RC(te_string_append(expected_flags_res, #_flag));     \
            if ((obtained_flags & _mask) == tarpc_flag)                     \
                CHECK_RC(te_string_append(obtained_flags_res, #_flag));     \
        } while(0)

    #define TEST_IP_CKSUM(_flag) \
                TEST_CKSUM(_flag, TARPC_RTE_MBUF_F_RX_IP_CKSUM_MASK)

    #define TEST_L4_CKSUM(_flag) \
                TEST_CKSUM(_flag, TARPC_RTE_MBUF_F_RX_L4_CKSUM_MASK)

    if ((expected_flags & TARPC_RTE_MBUF_F_RX_IP_CKSUM_MASK) !=
        (obtained_flags & TARPC_RTE_MBUF_F_RX_IP_CKSUM_MASK))
    {
        ip_mismatch = TRUE;
        TEST_IP_CKSUM(RTE_MBUF_F_RX_IP_CKSUM_NONE);
        TEST_IP_CKSUM(RTE_MBUF_F_RX_IP_CKSUM_BAD);
        TEST_IP_CKSUM(RTE_MBUF_F_RX_IP_CKSUM_GOOD);
        TEST_IP_CKSUM(RTE_MBUF_F_RX_IP_CKSUM_UNKNOWN);
    }

    if ((expected_flags & TARPC_RTE_MBUF_F_RX_L4_CKSUM_MASK) !=
        (obtained_flags & TARPC_RTE_MBUF_F_RX_L4_CKSUM_MASK))
    {
        if (ip_mismatch)
        {
            CHECK_RC(te_string_append(expected_flags_res, ","));
            CHECK_RC(te_string_append(obtained_flags_res, ","));
        }
        TEST_L4_CKSUM(RTE_MBUF_F_RX_L4_CKSUM_NONE);
        TEST_L4_CKSUM(RTE_MBUF_F_RX_L4_CKSUM_BAD);
        TEST_L4_CKSUM(RTE_MBUF_F_RX_L4_CKSUM_GOOD);
        TEST_L4_CKSUM(RTE_MBUF_F_RX_L4_CKSUM_UNKNOWN);
    }
}

static te_bool
is_fatal_malfunction(uint64_t expected_flags, uint64_t obtained_flags,
                     te_bool good_cksum_packet)
{
    te_bool ip_supp = (expected_flags & TARPC_RTE_MBUF_F_RX_IP_CKSUM_MASK) != 0;
    te_bool l4_supp = (expected_flags & TARPC_RTE_MBUF_F_RX_L4_CKSUM_MASK) != 0;

    if (good_cksum_packet)
    {
        if (ip_supp && ((obtained_flags & TARPC_RTE_MBUF_F_RX_IP_CKSUM_MASK) ==
                        TARPC_RTE_MBUF_F_RX_IP_CKSUM_BAD))
            return true;
        if (l4_supp && ((obtained_flags & TARPC_RTE_MBUF_F_RX_L4_CKSUM_MASK) ==
                        TARPC_RTE_MBUF_F_RX_L4_CKSUM_BAD))
            return true;
    }
    else
    {
        if (ip_supp && ((obtained_flags & TARPC_RTE_MBUF_F_RX_IP_CKSUM_MASK) ==
                        TARPC_RTE_MBUF_F_RX_IP_CKSUM_GOOD))
            return true;
        if (l4_supp && ((obtained_flags & TARPC_RTE_MBUF_F_RX_L4_CKSUM_MASK) ==
                        TARPC_RTE_MBUF_F_RX_L4_CKSUM_GOOD))
            return true;
    }

    return false;
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server                        *iut_rpcs = NULL;
    rcf_rpc_server                        *tst_rpcs = NULL;
    const struct if_nameindex             *iut_port = NULL;
    const struct if_nameindex             *tst_if = NULL;
    tapi_env_host                         *tst_host = NULL;
    asn_value                             *tmpl = NULL;
    te_bool                                rx_ol_cksum;

    asn_value                             *tmpl_bc = NULL; /* (Bad Checksums) */
    unsigned int                           nb_pdus_o;
    asn_value                            **pdus_o = NULL;
    unsigned int                           nb_pdus_i;
    asn_value                            **pdus_i = NULL;
    uint64_t                               fgo = 0;    /* Flags Good Outer */
    uint64_t                               fbo = 0;    /* Flags Bad Outer */
    int                                    l4o = -1;   /* L4 outer protocol */
    uint64_t                               fgi = 0;    /* Flags Good Inner */
    uint64_t                               fbi = 0;    /* Flags Bad Inner */
    int                                    l4i = -1;   /* L4 inner protocol */
    uint64_t                               need_hw_cksum = 0;
    struct test_ethdev_config              tec;
    te_bool                                eip_supported = FALSE;
    struct tarpc_rte_eth_conf              eth_conf;
    asn_value                             *pattern = NULL;
    asn_value                             *pattern_bc = NULL;
    rpc_rte_mbuf_p                         burst[BURST_SIZE] = {};
    struct tarpc_rte_pktmbuf_packet_type   pt;
    struct tarpc_rte_pktmbuf_packet_type   pt_bc;
    uint64_t                               m_flags;
    uint64_t                               m_flags_bc;
    te_bool                                fatal = FALSE;
    te_bool                                test_fail = FALSE;
    uint64_t                               fg = 0;    /* Flags Good */
    uint64_t                               fb = 0;    /* Flags Bad */
    uint64_t                               cksum_mask =
                                           TARPC_RTE_MBUF_F_RX_IP_CKSUM_MASK |
                                           TARPC_RTE_MBUF_F_RX_L4_CKSUM_MASK;
    te_string                              expected_flags = TE_STRING_INIT;
    te_string                              obtained_flags = TE_STRING_INIT;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_HOST(tst_host);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_BOOL_PARAM(rx_ol_cksum);

    TEST_STEP("Make a copy of the template to be used for bad checksum testing");
    tmpl_bc = asn_copy_value(tmpl);
    CHECK_NOT_NULL(tmpl_bc);

    TEST_STEP("Inspect the 2nd template to discover outer and inner (if any) PDUs");
    CHECK_RC(tapi_tad_tmpl_relist_outer_inner_pdus(tmpl_bc, &nb_pdus_o, &pdus_o,
                                                   &nb_pdus_i, &pdus_i));
    CHECK_NOT_NULL(pdus_o);

    TEST_STEP("Spoil checksums in the outer PDUs of the 2nd template and make "
              "notes on the standard checksum flags which _could be expected");
    CHECK_RC(test_pdus_detect_and_spoil_cksums(nb_pdus_o, pdus_o,
                                               &fgo, &fbo, &l4o));

    TEST_STEP("Spoil checksums in the inner PDUs of the 2nd template and make "
              "notes on the standard checksum flags which _could be expected");
    if (pdus_i != NULL)
    {
        CHECK_RC(test_pdus_detect_and_spoil_cksums(nb_pdus_i, pdus_i,
                                                   &fgi, &fbi, &l4i));
    }

    TEST_STEP("Prepare @c TEST_ETHDEV_INITIALIZED state");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &tec);
    CHECK_RC(test_prepare_ethdev(&tec, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Check the capabilities and adjust the expectations of checksum flags");
    {
        uint32_t caps = tec.dev_info.rx_offload_capa;
        uint32_t ip = 1UL << TARPC_RTE_ETH_RX_OFFLOAD_IPV4_CKSUM_BIT;
        uint32_t udp = 1UL << TARPC_RTE_ETH_RX_OFFLOAD_UDP_CKSUM_BIT;
        uint32_t tcp = 1UL << TARPC_RTE_ETH_RX_OFFLOAD_TCP_CKSUM_BIT;
        uint32_t eip = 1UL << TARPC_RTE_ETH_RX_OFFLOAD_OUTER_IPV4_CKSUM_BIT;
        te_bool  l4_unsupp_o = FALSE;
        te_bool  l4_unsupp_i = FALSE;

        if (((fgo | fbo | fgi | fbi) &
             (TARPC_RTE_MBUF_F_RX_IP_CKSUM_GOOD |
              TARPC_RTE_MBUF_F_RX_IP_CKSUM_BAD)) != 0)
        {
            if ((caps & ip) == 0)
            {
                fgo &= ~TARPC_RTE_MBUF_F_RX_IP_CKSUM_GOOD;
                fbo &= ~TARPC_RTE_MBUF_F_RX_IP_CKSUM_BAD;
                fgi &= ~TARPC_RTE_MBUF_F_RX_IP_CKSUM_GOOD;
                fbi &= ~TARPC_RTE_MBUF_F_RX_IP_CKSUM_BAD;

                WARN_VERDICT("Rx IP checksum offload is unsupported");
            }
            else
            {
                need_hw_cksum |= ip;
            }
        }

        if ((l4o == TE_PROTO_UDP) || (l4i == TE_PROTO_UDP))
        {
            if ((caps & udp) == 0)
            {
                if (l4o == TE_PROTO_UDP)
                    l4_unsupp_o = TRUE;

                if (l4i == TE_PROTO_UDP)
                    l4_unsupp_i = TRUE;

                WARN_VERDICT("Rx UDP checksum offload is unsupported");
            }
            else
            {
                need_hw_cksum |= udp;
            }
        }

        if ((l4o == TE_PROTO_TCP) || (l4i == TE_PROTO_TCP))
        {
            if ((caps & tcp) == 0)
            {
                if (l4o == TE_PROTO_TCP)
                    l4_unsupp_o = TRUE;

                if (l4i == TE_PROTO_TCP)
                    l4_unsupp_i = TRUE;

                WARN_VERDICT("Rx TCP checksum offload is unsupported");
            }
            else
            {
                need_hw_cksum |= tcp;
            }
        }

        if (l4_unsupp_o)
        {
            fgo &= ~TARPC_RTE_MBUF_F_RX_L4_CKSUM_GOOD;
            fbo &= ~TARPC_RTE_MBUF_F_RX_L4_CKSUM_BAD;
        }

        if (l4_unsupp_i)
        {
            fgi &= ~TARPC_RTE_MBUF_F_RX_L4_CKSUM_GOOD;
            fbi &= ~TARPC_RTE_MBUF_F_RX_L4_CKSUM_BAD;
        }

        if (pdus_i != NULL && (caps & eip) != 0)
        {
            eip_supported = TRUE;
            need_hw_cksum |= eip;
        }
    }

    tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                         &test_params, TEST_IUT_PORT_MAC_NAME);

    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_ndn_subst_env(tmpl_bc, &test_params, &env));

    tec.eth_conf = test_rpc_rte_eth_make_eth_conf(iut_rpcs, iut_port->if_index,
                                                  &eth_conf);

    TEST_STEP("Toggle Rx checksum offload feature based on @p rx_ol_cksum");
    {
        if (rx_ol_cksum)
            tec.eth_conf->rxmode.offloads |= need_hw_cksum;
        else
            tec.eth_conf->rxmode.offloads &= ~need_hw_cksum;

    }
    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    CHECK_RC(test_prepare_ethdev(&tec, TEST_ETHDEV_STARTED));

    TEST_STEP("Configure tunnel UDP port number if need be");
    CHECK_RC(test_add_tunnel_udp_port_from_tmpl(&tec, tmpl, TRUE));

    /*
     * Promicuous mode functionality is out of scope of the test, but
     * enabled promiscuous mode could help in some corner cases to
     * receive traffic.
     */
    TEST_STEP("Enable promiscuous mode if supported");
    test_rte_eth_promiscuous_enable(iut_rpcs, iut_port->if_index,
                                    TEST_OP_OPTIONAL);

    TEST_STEP("Transmit both packets and get the corresponding patterns");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                tst_if->if_name, tmpl, NULL, &pattern));
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                tst_if->if_name, tmpl_bc, NULL, &pattern_bc));
    CHECK_RC(tapi_tad_concat_patterns(pattern, pattern_bc));

    TEST_STEP("Capture and recognise both packets");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, 0,
                                         burst, TE_ARRAY_LEN(burst), 2,
                                         pattern, TRUE));

    TEST_STEP("Make note on the tunnel type value detected by the PMD");
    rpc_rte_pktmbuf_get_packet_type(iut_rpcs, burst[0], &pt);
    rpc_rte_pktmbuf_get_packet_type(iut_rpcs, burst[1], &pt_bc);
    if (pt.tun_type != pt_bc.tun_type)
        TEST_VERDICT("Tunnel type values for the two packets diverge");

    TEST_STEP("Retrieve offload flags from the two mbufs");
    m_flags = rpc_rte_pktmbuf_get_flags(iut_rpcs, burst[0]);
    m_flags_bc = rpc_rte_pktmbuf_get_flags(iut_rpcs, burst[1]);

    TEST_STEP("Make sure that NONE flags are unset for the packets");
    {
        uint64_t ip_none = 0;
        uint64_t l4_none = 0;

        ip_none |= TARPC_RTE_MBUF_F_RX_IP_CKSUM_GOOD;
        ip_none |= TARPC_RTE_MBUF_F_RX_IP_CKSUM_BAD;

        l4_none |= TARPC_RTE_MBUF_F_RX_L4_CKSUM_GOOD;
        l4_none |= TARPC_RTE_MBUF_F_RX_L4_CKSUM_BAD;

        if ((m_flags & ip_none) == ip_none)
            TEST_VERDICT("RTE_MBUF_F_RX_IP_CKSUM_NONE is set for the packet"
                         " with good checksums");

        if ((m_flags & l4_none) == l4_none)
            TEST_VERDICT("RTE_MBUF_F_RX_L4_CKSUM_NONE is set for the packet"
                         " with good checksums");

        if ((m_flags_bc & ip_none) == ip_none)
            TEST_VERDICT("RTE_MBUF_F_RX_IP_CKSUM_NONE is set for the packet"
                         " with bad checksums");

        if ((m_flags_bc & l4_none) == l4_none)
            TEST_VERDICT("RTE_MBUF_F_RX_L4_CKSUM_NONE is set for the packet"
                         " with bad checksums");
    }

    TEST_STEP("Determine what flags (for outer or inner PDUs) must be checked "
              "and verify external IP BAD checksum flag in the case of tunnel");
    if (pt.tun_type != TARPC_RTE_TUNNEL_TYPE_NONE)
    {
        if ((pt.l3_type == TARPC_RTE_PTYPE_L3_IPV4) ||
            (pt.l3_type == TARPC_RTE_PTYPE_L3_IPV4_EXT) ||
            (pt.l3_type == TARPC_RTE_PTYPE_L3_IPV4_EXT_UNKNOWN))
        {
            if (eip_supported)
            {
                if ((m_flags_bc & (1ULL << TARPC_RTE_MBUF_F_RX_OUTER_IP_CKSUM_BAD)) == 0)
                {
                    WARN_VERDICT("RTE_MBUF_F_RX_OUTER_IP_CKSUM_BAD is supported but not set"
                                 " for the packet with bad checksums");
                    fatal = TRUE;
                }
                if ((m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_OUTER_IP_CKSUM_BAD)) != 0)
                {
                    WARN_VERDICT("RTE_MBUF_F_RX_OUTER_IP_CKSUM_BAD is set"
                                 " for the packet with good checksums");
                    fatal = TRUE;
                }
            }
            else
            {
                WARN_VERDICT("Rx external IP checksum offload is unsupported");
            }
        }

        fg = fgi;
        fb = fbi;
    }
    else
    {
        fg = fgo;
        fb = fbo;
    }

    if (fg != 0)
    {
        te_bool g_correct;

        g_correct = ((m_flags & cksum_mask) == fg) ? TRUE : FALSE;

        if (g_correct && !rx_ol_cksum)
        {
            WARN_VERDICT("Rx checksum offload happened unexpectedly"
                         " for the packet with good checksums");
        }
        else if (!g_correct && rx_ol_cksum)
        {
            test_fail = TRUE;
            rx_ol_cksum_diff_flags_to_str(fg, m_flags, &expected_flags,
                                          &obtained_flags);
            WARN_VERDICT("Unexpected checksum offload flags for the packet"
                         " with good checksums: expected %s VS obtained %s",
                         expected_flags.ptr, obtained_flags.ptr);
            te_string_free(&expected_flags);
            te_string_free(&obtained_flags);

            if (is_fatal_malfunction(fg, m_flags, true))
                fatal = TRUE;
        }
    }

    if (fb != 0)
    {
        te_bool b_correct;

        b_correct = ((m_flags_bc & cksum_mask) == fb) ? TRUE : FALSE;

        if (b_correct && !rx_ol_cksum)
        {
            WARN_VERDICT("Rx checksum offload happened unexpectedly"
                         " for the packet with bad checksums");
        }
        else if (!b_correct && rx_ol_cksum)
        {
            test_fail = TRUE;
            rx_ol_cksum_diff_flags_to_str(fb, m_flags_bc, &expected_flags,
                                          &obtained_flags);
            WARN_VERDICT("Unexpected checksum offload flags for the packet"
                         " with bad checksums: expected %s VS obtained %s",
                         expected_flags.ptr, obtained_flags.ptr);

            if (is_fatal_malfunction(fb, m_flags_bc, false))
                fatal = TRUE;
        }
    }

    if (fatal)
        TEST_VERDICT("Rx checksum offload malfunction was detected:"
                     " bad checksum flags set for the packet with good"
                     " checksums or vice versa");
    if (test_fail)
        TEST_STOP;

    TEST_SUCCESS;

cleanup:
    if (burst[0] != RPC_NULL)
        rpc_rte_pktmbuf_free(iut_rpcs, burst[0]);

    if (burst[1] != RPC_NULL)
        rpc_rte_pktmbuf_free(iut_rpcs, burst[1]);

    te_string_free(&expected_flags);
    te_string_free(&obtained_flags);

    TEST_END;
}
/** @} */
