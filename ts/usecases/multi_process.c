/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-multi_process Simple multi-process usecase
 * @ingroup usecases
 * @{
 *
 * @objective Check multi-process support
 *
 * @param rx_tmpl   Traffic template to be sent from Tester and received on IUT
 * @param tx_tmpl   Traffic template to be sent from IUT and received on Tester
 *
 * @type use case
 *
 * @author Andrew Rybchenko <Andrew.Rybchenko@oktetlabs.ru>
 *
 * Make sure that traffic may be sent from and received in the secondary
 * process.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/multi_process"

#include "dpdk_pmd_test.h"
#include "tapi_rpc_rte_mempool.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server             *iut_rpcs  = NULL;
    rcf_rpc_server             *iut_secondary = NULL;
    rcf_rpc_server             *tst_rpcs  = NULL;
    tapi_env_host              *tst_host  = NULL;
    const struct if_nameindex  *iut_port  = NULL;
    const struct if_nameindex  *tst_if    = NULL;
    const struct sockaddr      *tst_addr  = NULL;
    const struct sockaddr      *iut_addr  = NULL;
    asn_value                  *rx_tmpl   = NULL;
    asn_value                  *tx_tmpl   = NULL;
    asn_value                  *rx_ptrn   = NULL;
    asn_value                  *tx_ptrn   = NULL;

    struct test_ethdev_config   ethdev_config;

    const char                 *secondary_args[2];
    rpc_rte_mbuf_p             *tx_mbufs;
    rpc_rte_mbuf_p              rx_mbufs[BURST_SIZE] = {};
    unsigned int                received;
    unsigned int                pkts;
    csap_handle_t               rx_csap;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(iut_secondary);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(rx_tmpl);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tx_tmpl);

    TEST_STEP("Start the Ethernet device");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);

    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Start RPC server as the secondary EAL process");
    secondary_args[0] = "--proc-type";
    secondary_args[1] = "secondary";
    CHECK_RC(tapi_rte_eal_init(&env, iut_secondary,
                               TE_ARRAY_LEN(secondary_args), secondary_args));

    TEST_STEP("Prepare @p rx_tmpl and @p tx_tmpl for test");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));
    CHECK_RC(tapi_ndn_subst_env(rx_tmpl, &test_params, &env));
    CHECK_RC(tapi_ndn_subst_env(tx_tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&rx_tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tx_tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Transmit and sniff packet from @p tst_if");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                    tst_if->if_name, rx_tmpl, NULL, &rx_ptrn));

    TEST_STEP("Try to receive sent packet in the secondary process and "
              "check that the packet is received and matches the sent one");
    CHECK_RC(test_rx_burst_match_pattern(iut_secondary, iut_port->if_index, 0,
                                         rx_mbufs, TE_ARRAY_LEN(rx_mbufs),
                                         1, rx_ptrn, TRUE));

    rpc_rte_pktmbuf_free(iut_secondary, rx_mbufs[0]);

    TEST_STEP("Prepare mbuf to be sent from the secondary process and create "
              "matching pattern to be used on peer");
    tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_secondary, tx_tmpl,
        rpc_rte_mempool_lookup(iut_secondary, TEST_PKTS_MEMPOOL_NAME),
        NULL, &tx_mbufs, &pkts, &tx_ptrn);
    if (pkts != 1)
    {
        TEST_VERDICT("Unexpected number of prepared mbufs: %d mbufs have "
                     "been produced, but should be %d", pkts, 1);
    }

    TEST_STEP("Create Ethernet-based CSAP on Tester which starts to listen to network "
              "immediately");
    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(tst_host->ta, 0,
                                                tst_if->if_name,
                                                TAD_ETH_RECV_DEF,
                                                tx_tmpl, &rx_csap));
    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, rx_csap, tx_ptrn,
                                   RECEIVE_TIMEOUT_DEF, 0,
                                   RCF_TRRECV_PACKETS | RCF_TRRECV_MISMATCH));

    TEST_STEP("Validate and transmit the packet from the secondary process");
    pkts = test_tx_prepare_and_burst(iut_secondary, iut_port->if_index, 0,
                                     tx_mbufs, 1);

    TEST_STEP("Stop Tester CSAP and check that received packet matches the sent one");
    CHECK_RC(test_rx_await_pkts(tst_host->ta, rx_csap, 1, 0));
    CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL, &received));
    CHECK_PACKETS_NUM(received, 1);

    TEST_STEP("Check that no extra packets are received on Tester");
    CHECK_RC(tapi_tad_csap_get_no_match_pkts(tst_host->ta, 0, rx_csap, &pkts));
    if (pkts != 0)
        TEST_VERDICT("%u not matching packets %s received", pkts,
                     (pkts > 1) ? "were" : "was");

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
