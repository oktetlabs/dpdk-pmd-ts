/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-vlan_strip   VLAN strip offload
 * @ingroup usecases
 * @{
 *
 * @objective Check VLAN strip offload
 *
 * @param vlan_strip            Test with VLAN strip offload enabled
 * @param qinq_strip            Test with QinQ strip offload enabled
 * @param tmpl                  Traffic template
 *
 * @type use case
 *
 * @author Dilshod Urazov <Dilshod.Urazov@oktetlabs.ru>
 *
 * Check the successful VLAN header stripping
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/vlan_strip"

#include "dpdk_pmd_test.h"

static te_errno
is_double_tagged_frame(asn_value *tmpl, te_bool *res)
{
    asn_value *pdus = NULL;
    te_errno rc;
    asn_child_desc_t *eth_hdrs = NULL;
    asn_value *eth = NULL;
    unsigned int n_hdrs;

    pdus = asn_find_descendant(tmpl, &rc, "pdus");
    if (rc != 0)
    {
        ERROR("Failed to get PDU sequence");
        return rc;
    }

    rc = asn_find_child_choice_values(pdus, TE_PROTO_ETH,
                                      &eth_hdrs, &n_hdrs);
    if (rc != 0 || n_hdrs < 1)
    {
        ERROR("Failed to get eth PDU");
        return rc;
    }

    eth = eth_hdrs[n_hdrs - 1].value;

    *res = FALSE;
    asn_find_descendant(eth, &rc, "tagged.#double-tagged");
    if (rc == 0)
        *res = TRUE;

    return 0;
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server                        *iut_rpcs = NULL;
    rcf_rpc_server                        *tst_rpcs = NULL;
    tapi_env_host                         *tst_host;
    const struct if_nameindex             *iut_port = NULL;
    const struct if_nameindex             *tst_if = NULL;
    asn_value                             *tmpl = NULL;

    struct test_ethdev_config              ethdev_config;
    struct tarpc_rte_eth_conf              eth_conf;
    asn_value                             *pattern = NULL;
    receive_transform                      rx_transform = {0};
    rpc_rte_mbuf_p                         m = RPC_NULL;
    uint64_t                               m_flags;
    uint16_t                               vlan_tci = 0;
    uint16_t                               outer_vlan_tci = 0;
    te_bool                                vlan_strip;
    te_bool                                qinq_strip;
    te_bool                                vlan_strip_sup = TRUE;
    te_bool                                qinq_strip_sup = TRUE;
    te_bool                                failed = FALSE;
    te_bool                                is_double_tagged;
    te_bool                                actual_vlan_strip;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_BOOL_PARAM(vlan_strip);
    TEST_GET_BOOL_PARAM(qinq_strip);

    TEST_STEP("Initialize EAL");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);

    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_INITIALIZED));

    ethdev_config.eth_conf = test_rpc_rte_eth_make_eth_conf(
                                      iut_rpcs, iut_port->if_index, &eth_conf);

    TEST_STEP("Check VLAN strip capability");
    if ((ethdev_config.dev_info.rx_offload_capa &
        (1U << TARPC_RTE_DEV_RX_OFFLOAD_VLAN_STRIP_BIT)) == 0)
    {
        if (vlan_strip)
            TEST_SKIP("VLAN strip is requested but not available");

        vlan_strip_sup = FALSE;
    }

    if (vlan_strip)
    {
        TEST_STEP("Enable VLAN strip offload if @p vlan_strip is @c TRUE");
        ethdev_config.eth_conf->rxmode.flags |=
                (1ULL << TARPC_RTE_ETH_RXMODE_HW_VLAN_STRIP_BIT);
        ethdev_config.eth_conf->rxmode.offloads |=
                (1ULL << TARPC_RTE_DEV_RX_OFFLOAD_VLAN_STRIP_BIT);

    }

    CHECK_RC(is_double_tagged_frame(tmpl, &is_double_tagged));

    /*
     * If frame is double-tagged, VLAN strip is requested, but QinQ is not,
     * then inner VLAN tag must not be stripped. See docs for RTE_MBUF_F_RX_VLAN,
     * that requiers 802.1q frame, i.e. not double-tagged.
     */
    actual_vlan_strip = vlan_strip && !(is_double_tagged && !qinq_strip);
    if (actual_vlan_strip)
        rx_transform.hw_flags |= RX_XFRM_HW_OFFL_VLAN_STRIP;

    TEST_STEP("Check QinQ strip capability if @p qinq_strip is @c TRUE");
    if ((ethdev_config.dev_info.rx_offload_capa &
         (1U << TARPC_RTE_DEV_RX_OFFLOAD_QINQ_STRIP_BIT)) == 0)
    {
        if (qinq_strip)
            TEST_SKIP("QinQ strip is requested but not available");

        qinq_strip_sup = FALSE;
    }

    if (qinq_strip)
    {
        TEST_STEP("Enable QinQ strip offload if @p qinq_strip is @c TRUE");
        ethdev_config.eth_conf->rxmode.flags |=
                (1ULL << TARPC_RTE_ETH_RXMODE_HW_VLAN_STRIP_BIT);
        ethdev_config.eth_conf->rxmode.offloads |=
                (1ULL << TARPC_RTE_DEV_RX_OFFLOAD_QINQ_STRIP_BIT);

        rx_transform.hw_flags |= RX_XFRM_HW_OFFL_QINQ_STRIP;
    }

    TEST_STEP("Configure and start the Ethernet device");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                         &test_params, TEST_IUT_PORT_MAC_NAME);
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));

    TEST_STEP("Configure UDP tunnel port number if need be");
    CHECK_RC(test_add_tunnel_udp_port_from_tmpl(&ethdev_config, tmpl, TRUE));

    /*
     * Promicuous mode functionality is out of scope of the test, but
     * enabled promiscuous mode could help in some corner cases to
     * receive traffic.
     */
    TEST_STEP("Enable promiscuous mode if supported");
    test_rte_eth_promiscuous_enable(iut_rpcs, iut_port->if_index,
                                    TEST_OP_OPTIONAL);

    TEST_STEP("Transmit a packet from @p tst_if towards @p iut_port");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, tmpl, NULL, &pattern));

    TEST_STEP("Transform the pattern due to expected offloads");
    CHECK_RC(tapi_eth_transform_ptrn_on_rx(&rx_transform, &pattern));

    TEST_STEP("Receive packets on @p iut_port and match the pattern");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, 0,
                                         &m, BURST_SIZE, 1, pattern, TRUE));

    TEST_STEP("Obtain mbuf flags and check them");
    m_flags = rpc_rte_pktmbuf_get_flags(iut_rpcs, m);

#define PENDING_FAIL(_msg)   \
    do {                     \
        ERROR_VERDICT(_msg); \
        failed = TRUE;       \
    } while (0)

    if (m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_VLAN_STRIPPED))
    {
        if (~m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_VLAN))
        {
            PENDING_FAIL("RTE_MBUF_F_RX_VLAN_STRIPPED flag is set "
                         "but RTE_MBUF_F_RX_VLAN flag is not set");
        }
    }

    if (m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_QINQ_STRIPPED))
    {
        if (~m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_QINQ))
        {
            PENDING_FAIL("RTE_MBUF_F_RX_QINQ_STRIPPED flag is set "
                         "but RTE_MBUF_F_RX_QINQ flag is not set");
        }

        if (~m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_VLAN))
        {
            PENDING_FAIL("RTE_MBUF_F_RX_QINQ_STRIPPED flag is set "
                         "but RTE_MBUF_F_RX_VLAN flag is not set");
        }
    }

    if (m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_VLAN))
    {
        if (rx_transform.effects & RX_XFRM_EFFECT_VLAN_TCI)
        {
            vlan_tci = rpc_rte_pktmbuf_get_vlan_tci(iut_rpcs, m);
            if (rx_transform.vlan_tci != vlan_tci)
                PENDING_FAIL("Extracted VLAN TCI is invalid");
        }
        else
        {
            PENDING_FAIL("VLAN TCI extracted from non-VLAN packet");
        }
    }

    if (m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_QINQ))
    {
        if (rx_transform.effects & RX_XFRM_EFFECT_OUTER_VLAN_TCI)
        {
            outer_vlan_tci = rpc_rte_pktmbuf_get_vlan_tci_outer(iut_rpcs, m);
            if (rx_transform.outer_vlan_tci != outer_vlan_tci)
                PENDING_FAIL("Extracted outer VLAN TCI is invalid");
        }
        else
        {
            PENDING_FAIL("Outer VLAN TCI extracted from non-QinQ packet");
        }
    }

    if (rx_transform.effects & RX_XFRM_EFFECT_OUTER_VLAN_TCI)
    {
        if (~m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_VLAN))
        {
            if (vlan_strip_sup)
            {
                RING_VERDICT("RTE_MBUF_F_RX_VLAN flag is not set for "
                             "double-tagged packet when VLAN "
                             "stripping is supported");
            }
            else
            {
                RING("RTE_MBUF_F_RX_VLAN flag is not set "
                     "for double-tagged packet");
            }
        }
        else
        {
            RING("RTE_MBUF_F_RX_VLAN flag is set "
                 "for double-tagged packet");
        }

        if (~m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_QINQ))
        {
            if (qinq_strip_sup)
            {
                PENDING_FAIL("RTE_MBUF_F_RX_QINQ flag is not set for "
                             "double-tagged packet when QinQ "
                             "stripping is supported");
            }
            else
            {
                RING("RTE_MBUF_F_RX_QINQ flag is not set "
                     "for double-tagged packet");
            }
        }
        else
        {
            RING("RTE_MBUF_F_RX_QINQ flag is set "
                 "for double-tagged packet");
        }

        if (actual_vlan_strip &&
            (~m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_VLAN_STRIPPED)))
        {
            PENDING_FAIL("VLAN strip offload is requested but "
                         "RTE_MBUF_F_RX_VLAN_STRIPPED flag is not set "
                         "for double-tagged packet");
        }

        if (qinq_strip && (~m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_QINQ_STRIPPED)))
        {
            PENDING_FAIL("QinQ strip offload is requested but "
                         "RTE_MBUF_F_RX_QINQ_STRIPPED flag is not set "
                         "for double-tagged packet");
        }
    }
    else if (rx_transform.effects & RX_XFRM_EFFECT_VLAN_TCI)
    {
        if (~m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_VLAN))
        {
            if (vlan_strip_sup)
            {
                RING_VERDICT("RTE_MBUF_F_RX_VLAN flag is not set for "
                             "tagged packet when VLAN "
                             "stripping is supported");
            }
            else
            {
                RING("RTE_MBUF_F_RX_VLAN flag is not set for tagged packet");
            }
        }
        else
        {
            RING("RTE_MBUF_F_RX_VLAN is set for tagged packet");
        }

        if (actual_vlan_strip &&
            (~m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_VLAN_STRIPPED)))
        {
            PENDING_FAIL("VLAN strip offload is requested but "
                         "RTE_MBUF_F_RX_VLAN_STRIPPED flag is not set "
                         "for tagged packet");
        }
    }
    else
    {
        if (m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_VLAN))
        {
            PENDING_FAIL("RTE_MBUF_F_RX_VLAN flag is set "
                         "for packet without VLAN headers");
        }

        if (m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_QINQ))
        {
            PENDING_FAIL("RTE_MBUF_F_RX_QINQ flag is set "
                         "for packet without VLAN headers");
        }

        if (m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_VLAN_STRIPPED))
        {
            PENDING_FAIL("RTE_MBUF_F_RX_VLAN_STRIPPED flag is set "
                         "for packet without VLAN headers");
        }

        if (m_flags & (1ULL << TARPC_RTE_MBUF_F_RX_QINQ_STRIPPED))
        {
            PENDING_FAIL("RTE_MBUF_F_RX_QINQ_STRIPPED flag is set  "
                         "for packet without VLAN headers");
        }
    }

#undef PENDING_FAIL

    if (failed)
        TEST_STOP;
    else
        TEST_SUCCESS;

cleanup:
    if (m != RPC_NULL)
        rpc_rte_pktmbuf_free(iut_rpcs, m);

    TEST_END;
}
/** @} */
