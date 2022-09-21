/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-promiscuous_mode Promiscuous mode test
 * @ingroup usecases
 * @{
 *
 * @objective Check correct work of promiscuous mode
 *
 * @param ethdev_state         The state of Ethernet device
 * @param is_promiscuous_mode  Enable promiscuous mode if @c TRUE,
 *                             otherwise disable it
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Checking the correct work of promiscuous mode by sending two packets
 * with different MAC addresses:
 * one of which is the MAC address of the @p iut_port,
 * the other is not
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/promiscuos_mode"

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

    rpc_rte_mbuf_p              mbufs[BURST_SIZE] = {};
    asn_value                  *tmpl                  = NULL;
    asn_value                  *ptrn                  = NULL;
    asn_value                  *ptrn_with_alien_mac   = NULL;
    asn_value                  *tmpl_with_iut_mac     = NULL;
    asn_value                  *tmpl_with_alien_mac   = NULL;
    struct test_ethdev_config   test_ethdev_config;
    test_ethdev_state           ethdev_state;

    te_bool                     is_promiscuous_mode;
    unsigned int                i;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_LINK_ADDR(iut_alien_mac);
    TEST_GET_ETHDEV_STATE(ethdev_state);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_BOOL_PARAM(is_promiscuous_mode);

    TEST_STEP("Initialize EAL, preparing of @p ethdev_state Ethernet device state");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &test_ethdev_config, ethdev_state));

    tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                         &test_params, TEST_IUT_PORT_MAC_NAME);

    TEST_STEP("If the @p is_promiscuous_mode is @c TRUE "
              "then enable promiscuous mode, "
              "otherwise disable it");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    if (is_promiscuous_mode)
        rc = rpc_rte_eth_promiscuous_enable(iut_rpcs, iut_port->if_index);
    else
        rc = rpc_rte_eth_promiscuous_disable(iut_rpcs, iut_port->if_index);

    if (rc == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
    {
        TEST_VERDICT("Promiscuous %s operation is not supported",
                     is_promiscuous_mode ? "enable" : "disable");
    }
    else if (rc != 0)
    {
        TEST_VERDICT("Promiscuous %s operation failed: %r",
                     is_promiscuous_mode ? "enable" : "disable", rc);
    }

    TEST_STEP("Start the Ethernet device");
    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Check that a new mode was saved after start of device");
    if (rpc_rte_eth_promiscuous_get(iut_rpcs, iut_port->if_index) !=
        (int)(is_promiscuous_mode) )
        TEST_VERDICT("Promiscuous mode is not preserved after start");

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));

    tmpl_with_iut_mac = tmpl;
    tmpl_with_alien_mac = asn_copy_value(tmpl);
    CHECK_NOT_NULL(tmpl_with_alien_mac);

    TEST_STEP("Transmit and sniff two packets from @p tst_if: "
              "the first to the @p iut_port  MAC address "
              "and the second to the @p iut_alien_mac");
    CHECK_RC(asn_write_value_field(tmpl_with_alien_mac,
                                   iut_alien_mac->sa_data,
                                   ETHER_ADDR_LEN,
                                   "pdus.0.#eth.dst-addr.#plain"));

    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                    tst_if->if_name, tmpl_with_iut_mac, NULL,
                                    &ptrn));
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                    tst_if->if_name, tmpl_with_alien_mac, NULL,
                                    &ptrn_with_alien_mac));
    if (is_promiscuous_mode)
        CHECK_RC(tapi_tad_concat_patterns(ptrn, ptrn_with_alien_mac));

    TEST_STEP("Receive packets on @p iut_port "
              "Check that the first received packet matches the first sent and "
              "the second was dropped if @p is_promiscuous_mode is @c FALSE");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, 0,
                                         mbufs, TE_ARRAY_LEN(mbufs),
                                         is_promiscuous_mode ? 2 : 1,
                                         ptrn, TRUE));

    TEST_SUCCESS;

cleanup:
    for (i = 0; i < TE_ARRAY_LEN(mbufs); i++)
        rpc_rte_pktmbuf_free(iut_rpcs, mbufs[i]);

    TEST_END;
}
/** @} */
