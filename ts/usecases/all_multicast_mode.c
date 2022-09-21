/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-all_multicast_mode All-multicast mode test
 * @ingroup usecases
 * @{
 *
 * @objective Check correct work of all-multicast mode
 *
 * @param ethdev_state       The state of Ethernet device
 * @param is_all_mcast_mode  Enable all-multicast if @c TRUE,
 *                           otherwise disable it
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Checking the correct work of all-multicast mode by sending several
 * packets with different MAC addresses.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/all_multicast_mode"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server             *iut_rpcs      = NULL;
    rcf_rpc_server             *tst_rpcs      = NULL;
    tapi_env_host              *tst_host      = NULL;
    const struct if_nameindex  *iut_port      = NULL;
    const struct if_nameindex  *tst_if        = NULL;
    const struct sockaddr      *iut_alien_mac = NULL;
    const struct sockaddr      *bcast_addr    = NULL;

    rpc_rte_mbuf_p              mbufs[BURST_SIZE] = {};
    asn_value                  *tmpl                    = NULL;
    asn_value                  *ptrn_with_multicast_mac = NULL;
    asn_value                  *ptrn_with_unicast_mac   = NULL;
    asn_value                  *ptrn                    = NULL;
    asn_value                  *tmpl_with_multicast_mac = NULL;
    asn_value                  *tmpl_with_unicast_mac   = NULL;
    asn_value                  *tmpl_with_bcast_mac     = NULL;
    struct test_ethdev_config   test_ethdev_config;
    test_ethdev_state           ethdev_state;

    te_bool                     is_promiscuous_mode;
    te_bool                     is_all_mcast_mode;
    unsigned int                i;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_LINK_ADDR(iut_alien_mac);
    TEST_GET_LINK_ADDR(bcast_addr);
    TEST_GET_ETHDEV_STATE(ethdev_state);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_BOOL_PARAM(is_all_mcast_mode);

    TEST_STEP("Initialize EAL, preparing of @p ethdev_state Ethernet device state");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &test_ethdev_config, ethdev_state));

    TEST_STEP("Make sure that promiscuous mode is disabled");
    is_promiscuous_mode = rpc_rte_eth_promiscuous_get(iut_rpcs,
                                                      iut_port->if_index);

    if (is_promiscuous_mode)
        test_rte_eth_promiscuous_disable(iut_rpcs, iut_port->if_index,
                                         TEST_OP_REQUIRED);

    TEST_STEP("If the @p is_all_mcast_mode is @c TRUE "
              "then enable all-multicast mode,  otherwise disable it");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    if (is_all_mcast_mode)
        rc = rpc_rte_eth_allmulticast_enable(iut_rpcs, iut_port->if_index);
    else
        rc = rpc_rte_eth_allmulticast_disable(iut_rpcs, iut_port->if_index);

    if (rc == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
    {
        TEST_VERDICT("All multicast %s operation is not supported",
                     is_all_mcast_mode ? "enable" : "disable");
    }
    else if (rc != 0)
    {
        TEST_VERDICT("All multicast %s operation failed: %r",
                     is_all_mcast_mode ? "enable" : "disable", rc);
    }

    TEST_STEP("Start the Ethernet device");
    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Check that a new mode was saved after start of device");
    if (rpc_rte_eth_allmulticast_get(iut_rpcs, iut_port->if_index) !=
        (int)(is_all_mcast_mode) )
        TEST_VERDICT("New all-multicast wasn't saved after start");

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));
    tmpl_with_multicast_mac = tmpl;
    tmpl_with_unicast_mac = asn_copy_value(tmpl);
    CHECK_NOT_NULL(tmpl_with_unicast_mac);
    tmpl_with_bcast_mac = asn_copy_value(tmpl);
    CHECK_NOT_NULL(tmpl_with_bcast_mac);

    TEST_STEP("Transmit three packets from @p tst_if: "
              "the first is broadcast, "
              "the second is multicast and "
              "the third is unicast, with MAC adrress is different from MAC address "
              "of the @p iut_port");
    CHECK_RC(asn_write_value_field(tmpl_with_unicast_mac,
                                   iut_alien_mac->sa_data, ETHER_ADDR_LEN,
                                   "pdus.0.#eth.dst-addr.#plain"));
    CHECK_RC(asn_write_value_field(tmpl_with_bcast_mac,
                                   bcast_addr->sa_data, ETHER_ADDR_LEN,
                                   "pdus.0.#eth.dst-addr.#plain"));

    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                    tst_if->if_name, tmpl_with_bcast_mac, NULL,
                                    &ptrn));
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                tst_if->if_name, tmpl_with_multicast_mac, NULL,
                                &ptrn_with_multicast_mac));
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                tst_if->if_name, tmpl_with_unicast_mac, NULL,
                                &ptrn_with_unicast_mac));
    if (is_all_mcast_mode)
        CHECK_RC(tapi_tad_concat_patterns(ptrn, ptrn_with_multicast_mac));

    TEST_STEP("Receive packets on port @p iut_port. "
              "Check that the first received packet matches the first sent, "
              "the second received packet matches the first sent, "
              "if @p is_all_mcast_mode is @c TRUE, otherwise it was dropped "
              "and the third packet was dropped.");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, 0,
                                         mbufs, TE_ARRAY_LEN(mbufs),
                                         is_all_mcast_mode ? 2 : 1,
                                         ptrn, TRUE));

    TEST_SUCCESS;

cleanup:
    for (i = 0; i < TE_ARRAY_LEN(mbufs); i++)
        rpc_rte_pktmbuf_free(iut_rpcs, mbufs[i]);

    TEST_END;
}
/** @} */
