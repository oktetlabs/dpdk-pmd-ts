/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2024 OKTET Labs. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-rx_ring_wrap Rx behavior with number of packets greater than descriptors number
 * @ingroup usecases
 * @{
 *
 * @objective Make sure that driver can correctly refill Rx queue desc ring
 *
 * @param template      Traffic template
 * @param nb_rxd        Rx descriptors number
 * @param nb_wrap       Number of ring refilling
 *
 * @type use case
 *
 * @author Daniil Byshenko <Daniil.Byshenko@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "usecases/rx_ring_wrap"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

#define TEST_PAYLOAD_LEN 128
#define TEST_NB_PKT_MIN 1
#define TEST_NB_PKT_MAX 16
#define TEST_RXQ 0

int
main(int argc, char *argv[])
{
    rcf_rpc_server             *iut_rpcs = NULL;
    rcf_rpc_server             *tst_rpcs = NULL;
    tapi_env_host              *tst_host = NULL;
    const struct if_nameindex  *iut_port = NULL;
    const struct if_nameindex  *tst_if   = NULL;

    asn_value                      *template;
    struct test_ethdev_config       eth_conf;
    unsigned int                    nb_rxd;
    unsigned int                    nb_wrap;
    unsigned int                    nb_exp_pkts;
    unsigned int                    nb_recv_pkts;
    rpc_rte_mbuf_p                  mbufs[TEST_NB_PKT_MAX];
    struct tarpc_rte_eth_rxq_info   rx_qinfo;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(template);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_UINT_PARAM(nb_rxd);
    TEST_GET_UINT_PARAM(nb_wrap);

    TEST_STEP("Prepare default config and mbuf pool");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &eth_conf);

    TEST_STEP("Prepare @c TEST_ETHDEV_INITIALIZED state");
    CHECK_RC(test_prepare_ethdev(&eth_conf, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Check Rx queue number of descriptors limitations");

    if (test_desc_nb_violates_limits(nb_rxd, &eth_conf.dev_info.rx_desc_lim))
        TEST_SKIP("Requested number of Rx descriptors violates limits");

    TEST_SUBSTEP("Set Rx queue number of descriptors");
    eth_conf.min_rx_desc = nb_rxd;

    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    CHECK_RC(test_prepare_ethdev(&eth_conf, TEST_ETHDEV_STARTED));

    TEST_STEP("Check number of descriptors in Rx queue info if supported");
    /*
     * The driver could set up another descriptors number.
     * Try to get the actual value.
     */
    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_rx_queue_info_get(iut_rpcs, iut_port->if_index, TEST_RXQ,
                                       &rx_qinfo);
    if (rc != 0)
    {
        if (rc == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
            RING("Get queue info isn't supported");
        else
            TEST_VERDICT("Failed to get RxQ %u info: %s", TEST_RXQ,
                         errno_rpc2str(-rc));
    }
    else
    {
        if (nb_rxd != rx_qinfo.nb_desc)
        {
            WARN("Requested descriptors number is %u. "
                 "Driver set up %u descriptors instead",
                 nb_rxd, rx_qinfo.nb_desc);

            nb_rxd = rx_qinfo.nb_desc;
        }
    }
    TEST_STEP("Prepare @p template for test");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));

    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&template, FALSE, NULL,
                                                  TEST_PAYLOAD_LEN));
    CHECK_RC(asn_write_int32(template, 1, "arg-sets.0.#simple-for.begin"));
    CHECK_RC(tapi_ndn_subst_env(template, &test_params, &env));

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    nb_exp_pkts = nb_rxd * nb_wrap;
    nb_recv_pkts = 0;
    while (nb_recv_pkts < nb_exp_pkts)
    {
        asn_value *ptrn;
        unsigned int nb_packets;

        nb_packets = rand_range(TEST_NB_PKT_MIN, TEST_NB_PKT_MAX);

        CHECK_RC(asn_write_int32(template, nb_packets,
                                 "arg-sets.0.#simple-for.end"));

        TEST_STEP("Transmit and sniff nb_packets packets from @p tst_if");
        tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0, tst_if->if_name,
                                           template, NULL, &ptrn);

        TEST_STEP("Receive packets on @p iut_port");
        CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index,
                                             TEST_RXQ, mbufs,
                                             TE_ARRAY_LEN(mbufs),
                                             nb_packets, ptrn, TRUE));

        rpc_rte_pktmbuf_free_array(iut_rpcs, mbufs, nb_packets);
        asn_free_value(ptrn);

        nb_recv_pkts += nb_packets;
    }

    TEST_SUCCESS;

cleanup:

    /*
     * Try to read out sent packets to prevent providing them to the following
     * tests.
     */
    test_rx_clean_queue(iut_rpcs, iut_port->if_index, TEST_RXQ);

    TEST_END;
}
