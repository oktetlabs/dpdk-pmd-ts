/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-set_default_mac_addr default MAC address test
 * @ingroup usecases
 * @{
 *
 * @objective Set the default MAC address
 *
 * @param ethdev_state         The state of Ethernet device
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Make sure that changing the default MAC address really
 * takes effect
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/set_default_mac_addr"

#include "dpdk_pmd_test.h"


int
main(int argc, char *argv[])
{
    rcf_rpc_server             *iut_rpcs = NULL;
    rcf_rpc_server             *tst_rpcs = NULL;
    tapi_env_host              *tst_host;
    const struct sockaddr      *iut_alien_mac = NULL;
    const struct if_nameindex  *iut_port = NULL;
    const struct if_nameindex  *tst_if = NULL;

    asn_value                  *tmpl;
    rpc_rte_mbuf_p              mbufs[BURST_SIZE] = {};
    asn_value                  *ptrn_with_old_mac = NULL;
    asn_value                  *ptrn_with_new_mac = NULL;
    asn_value                  *tmpl_with_old_mac = NULL;
    asn_value                  *tmpl_with_new_mac = NULL;
    struct test_ethdev_config   test_ethdev_config;
    struct tarpc_ether_addr     temp_mac_addr;
    te_bool                     is_promiscuous_mode;
    test_ethdev_state           ethdev_state;
    int                         ret;
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

    TEST_STEP("Initialize EAL, configure @p iut_port to provide 1 Rx queue "
              "and start the port");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &test_ethdev_config,
                                         ethdev_state));

    tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                         &test_params, TEST_IUT_PORT_MAC_NAME);
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));

    TEST_STEP("Make sure that promiscuous mode is disabled.");
    is_promiscuous_mode = rpc_rte_eth_promiscuous_get(iut_rpcs,
                                                      iut_port->if_index);

    if (is_promiscuous_mode)
        test_rte_eth_promiscuous_disable(iut_rpcs, iut_port->if_index,
                                         TEST_OP_REQUIRED);

    tmpl_with_old_mac = tmpl;
    tmpl_with_new_mac = asn_copy_value((const asn_value *)tmpl);
    CHECK_NOT_NULL(tmpl_with_new_mac);

    TEST_STEP("Set the default MAC address equal to @p iut_alien_mac.");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    ret = rpc_rte_eth_dev_default_mac_addr_set(
        iut_rpcs, iut_port->if_index,
        (struct tarpc_ether_addr *)iut_alien_mac->sa_data);

    if (-ret == TE_RC(TE_RPC, TE_EOPNOTSUPP))
        TEST_VERDICT("Changing default MAC address is not supported");
    else if (ret != 0)
        TEST_STOP;

    TEST_STEP("Get MAC address and compare it with @p iut_alien_mac.");
    rpc_rte_eth_macaddr_get(iut_rpcs, iut_port->if_index, &temp_mac_addr);

    if (memcmp(temp_mac_addr.addr_bytes, iut_alien_mac->sa_data,
        ETHER_ADDR_LEN) != 0)
        TEST_VERDICT("MAC address was not change. Should be %Tm but now it %Tm",
                     iut_alien_mac->sa_data, ETHER_ADDR_LEN,
                     temp_mac_addr.addr_bytes, ETHER_ADDR_LEN);

    TEST_STEP("Start the Ethernet device");
    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Transmit and sniff two unicast packets from @p tst_if: "
              "the first to the @p iut_alien_mac "
              "and the second to old IUT MAC.");
    CHECK_RC(asn_write_value_field(tmpl_with_new_mac, temp_mac_addr.addr_bytes,
                                   ETHER_ADDR_LEN, "pdus.0.#eth.dst-addr.#plain"));

    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                    tst_if->if_name, tmpl_with_old_mac, NULL,
                                    &ptrn_with_old_mac));
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                    tst_if->if_name, tmpl_with_new_mac, NULL,
                                    &ptrn_with_new_mac));

    TEST_STEP("Receive packets on @p iut_port. "
              "Check that the first received packet matches the first sent and "
              "the second was dropped.");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, 0,
                                         mbufs, TE_ARRAY_LEN(mbufs),
                                         1, ptrn_with_new_mac, TRUE));
    TEST_SUCCESS;

cleanup:
    for (i = 0; i < TE_ARRAY_LEN(mbufs); i++)
        rpc_rte_pktmbuf_free(iut_rpcs, mbufs[i]);

    TEST_END;
}
/** @} */
