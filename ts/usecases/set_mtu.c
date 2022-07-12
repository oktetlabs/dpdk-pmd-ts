/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-set_mtu Set mtu test
 * @ingroup usecases
 * @{
 *
 * @objective Set MTU of IUT
 *
 * @param mtu            MTU on IUT
 * @param excess_mtu     The excess of the MTU value
 * @param ethdev_state   The state of Ethernet device
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Make sure that when we set the MTU, the port receives packets
 * with size of no more than MTU, and does not receive others.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/set_mtu"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server             *iut_rpcs = NULL;
    rcf_rpc_server             *tst_rpcs = NULL;
    tapi_env_host              *tst_host;
    const struct if_nameindex  *iut_port = NULL;
    const struct if_nameindex  *tst_if = NULL;

    asn_value                  *tmpl;
    rpc_rte_mbuf_p              mbufs[BURST_SIZE] = {};
    asn_value                  *ptrn_with_mtu_sz        = NULL;
    asn_value                  *ptrn_with_excess_mtu_sz = NULL;
    asn_value                  *tmpl_with_mtu_sz        = NULL;
    asn_value                  *tmpl_with_excess_mtu_sz = NULL;

    uint16_t                    received = 0;
    struct test_ethdev_config   test_ethdev_config;

    test_ethdev_state           ethdev_state;
    uint16_t                    mtu, tmp_mtu, excess_mtu;
    unsigned int                i;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_INT_PARAM(mtu);
    TEST_GET_INT_PARAM(excess_mtu);
    TEST_GET_ETHDEV_STATE(ethdev_state);

    TEST_STEP("Initialize EAL, configure @p iut_port to provide 1 Rx queue "
              "and start the port");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &test_ethdev_config,
                                         TEST_ETHDEV_INITIALIZED));

     if (mtu + excess_mtu > TEST_RTE_MEMPOOL_DEF_DATA_ROOM)
         test_ethdev_config.mp = rpc_rte_pktmbuf_pool_create(
                                     iut_rpcs, TEST_PKTS_MEMPOOL_NAME,
                                     TEST_RTE_MEMPOOL_DEF_SIZE,
                                     TEST_RTE_MEMPOOL_DEF_CACHE,
                                     TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                     TEST_RTE_MEMPOOL_DEF_JUMBO_DATA_ROOM,
                                     test_ethdev_config.socket_id);

    tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                         &test_params, TEST_IUT_PORT_MAC_NAME);

    TEST_STEP("Set @p mtu on @p iut_port in @p ethdev_state");
    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, ethdev_state));
    test_set_mtu(iut_rpcs, iut_port->if_index, mtu, &test_ethdev_config);

    TEST_STEP("If @p ethdev_state is @c TEST_ETHDEV_STARTED, wait one more time "
              "for the link to become ready because in certain cases MTU change "
              "may result in port restart and, thus, some traffic might be lost");
    if (ethdev_state == TEST_ETHDEV_STARTED)
        test_await_link_up(iut_rpcs, iut_port->if_index);

    rpc_rte_eth_dev_get_mtu(iut_rpcs, iut_port->if_index, &tmp_mtu);

    TEST_STEP("Test verdict with the current MTU value");
    if (tmp_mtu != mtu)
        TEST_VERDICT("MTU is %d, but should be %d", tmp_mtu, mtu);

    TEST_STEP("Change @p tst_if MTU to be able to transmit bigger "
              "frames if required");
    CHECK_RC(tapi_cfg_base_if_set_mtu_leastwise(tst_host->ta, tst_if->if_name,
                                                mtu + excess_mtu));

    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));

    TEST_STEP("Start the Ethernet device");
    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_STARTED));

    rpc_rte_eth_dev_get_mtu(iut_rpcs, iut_port->if_index, &tmp_mtu);

    TEST_STEP("Test verdict with the current MTU value after device start");
    if (tmp_mtu != mtu)
        TEST_VERDICT("Current MTU %u differs from set before device start",
                     tmp_mtu);

    tmpl_with_mtu_sz = tmpl;
    tmpl_with_excess_mtu_sz = asn_copy_value(tmpl);
    CHECK_NOT_NULL(tmpl_with_excess_mtu_sz);

    /* Set payload of templates according with @p test_mtu and @p excess_mtu */
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl_with_mtu_sz, FALSE,
                                                  NULL, mtu));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl_with_excess_mtu_sz,
                                                  FALSE, NULL,
                                                  (mtu + excess_mtu)));

    TEST_STEP("Transmit and sniff packet with size equal to the @p test_mtu "
              "from @p tst_if to the @p iut_port");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, tmpl_with_mtu_sz, NULL,
                                        &ptrn_with_mtu_sz));

    TEST_STEP("Transmit and sniff packet with size equal to "
              "the @p test_mtu plus @p excess_mtu "
              "from @p tst_if to the @p iut_port");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                tst_if->if_name, tmpl_with_excess_mtu_sz, NULL,
                                &ptrn_with_excess_mtu_sz));

    CHECK_RC(tapi_tad_concat_patterns(ptrn_with_mtu_sz,
                                      ptrn_with_excess_mtu_sz));

    TEST_STEP("Receive packets on port @p iut_port. "
              "Check that the first received packet matches the first sent and "
              "the second was dropped");
    CHECK_RC(test_rx_burst_match_pattern_custom_verdicts(
                iut_rpcs, iut_port->if_index, 0, mbufs, BURST_SIZE, 1,
                ptrn_with_mtu_sz, TRUE,
                NULL,
                "Packet larger than MTU is unexpectedly received"));

    TEST_SUCCESS;

cleanup:
    for (i = 0; i < received; i++)
        rpc_rte_pktmbuf_free(iut_rpcs, mbufs[i]);

    TEST_END;
}
/** @} */
