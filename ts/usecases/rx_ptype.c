/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-rx_ptype Check Rx traffic classification
 * @ingroup usecases
 * @{
 *
 * @objective Make sure that traffic classification
 *            is carried out properly by the driver
 *
 * @param tmpl Traffic template
 *
 * @type use case
 *
 * @author Roman Zhukov <Roman.Zhukov@oktetlabs.ru>
 *
 * The test receives a packet, inspects its packet type
 * field and makes sure the results are eligible as per
 * the capabilities advertised by the driver.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "usecases/rx_ptype"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                       *iut_rpcs = NULL;
    rcf_rpc_server                       *tst_rpcs = NULL;
    tapi_env_host                        *tst_host;
    const struct if_nameindex            *iut_port = NULL;
    const struct if_nameindex            *tst_if = NULL;
    asn_value                            *tmpl = NULL;

    struct test_ethdev_config             ethdev_config;
    asn_value                            *pattern = NULL;
    rpc_rte_mbuf_p                        mbufs[BURST_SIZE] = {};
    struct tarpc_rte_pktmbuf_packet_type  pt;
    uint32_t                              pt_mask;
    int                                   supp_count;
    uint32_t                              pt_exp;
    te_bool                               err;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);

    TEST_STEP("Start the port");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &ethdev_config, TEST_ETHDEV_STARTED));

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

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Transmit a packet to the @p iut_port and obtain the pattern");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, tmpl, NULL, &pattern));

    TEST_STEP("Receive the packet on port @p iut_port and check that the "
              "received packet matches the sent one");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, 0,
                                         mbufs, TE_ARRAY_LEN(mbufs),
                                         1, pattern, TRUE));

    TEST_STEP("Get packet type from the received mbuf");
    rpc_rte_pktmbuf_get_packet_type(iut_rpcs, mbufs[0], &pt);

    TEST_STEP("Get supported packet types");
    pt_mask = TARPC_RTE_PTYPE_ALL_MASK;
    supp_count = rpc_rte_eth_dev_get_supported_ptypes(iut_rpcs,
                                                      iut_port->if_index,
                                                      pt_mask, NULL, 0);

    if (supp_count == 0)
        TEST_SKIP("Supported packet types count is zero");

    {
        uint32_t supp[supp_count];

        (void)rpc_rte_eth_dev_get_supported_ptypes(iut_rpcs, iut_port->if_index,
                                                   pt_mask, supp, supp_count);

        pt_exp = test_ptype_from_pattern(pattern, supp, supp_count);
    }

    TEST_STEP("Check that packet type is correct");
    err = FALSE;
    err |= test_check_packet_ptype(pt_exp & TARPC_RTE_PTYPE_L2_MASK,
                                   pt.l2_type, TARPC_RTE_PTYPE_L2_OFFSET);
    err |= test_check_packet_ptype(pt_exp & TARPC_RTE_PTYPE_L3_MASK,
                                   pt.l3_type, TARPC_RTE_PTYPE_L3_OFFSET);
    err |= test_check_packet_ptype(pt_exp & TARPC_RTE_PTYPE_L4_MASK,
                                   pt.l4_type, TARPC_RTE_PTYPE_L4_OFFSET);
    err |= test_check_packet_ptype(pt_exp & TARPC_RTE_PTYPE_TUNNEL_MASK,
                                   pt.tun_type, TARPC_RTE_PTYPE_TUNNEL_OFFSET);
    err |= test_check_packet_ptype(pt_exp & TARPC_RTE_PTYPE_INNER_L2_MASK,
                                   pt.inner_l2_type,
                                   TARPC_RTE_PTYPE_INNER_L2_OFFSET);
    err |= test_check_packet_ptype(pt_exp & TARPC_RTE_PTYPE_INNER_L3_MASK,
                                   pt.inner_l3_type,
                                   TARPC_RTE_PTYPE_INNER_L3_OFFSET);
    err |= test_check_packet_ptype(pt_exp & TARPC_RTE_PTYPE_INNER_L4_MASK,
                                   pt.inner_l4_type,
                                   TARPC_RTE_PTYPE_INNER_L4_OFFSET);
    if (err)
        TEST_VERDICT("Packet type is wrong");

    TEST_SUCCESS;

cleanup:
    if (mbufs[0] != RPC_NULL)
        rpc_rte_pktmbuf_free(iut_rpcs, mbufs[0]);

    TEST_END;
}
/** @} */
