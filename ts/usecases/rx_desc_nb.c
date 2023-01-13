/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-rx_desc_nb Rx behavior with different descriptors number
 * @ingroup usecases
 * @{
 *
 * @objective Given some descriptor count, verify queue setup and packet reception
 *
 * @param template      Traffic template
 * @param nb_rxd        Rx descriptors number
 *
 * @type use case
 *
 * @author Ivan Ilchenko <Ivan.Ilchenko@oktetlabs.ru>
 *
 * The test sets up Rx queue with specified descriptors number checking
 * reported limits. Tester side transmits a big packets burst to verify
 * the number of packets IUT receives with respect to set up descriptors.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "usecases/rx_desc_nb"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

#define TEST_MAX_NB_RXD 32768
#define TEST_PAYLOAD_LEN 128
#define TEST_PACKETS_TO_DESC_SCALE 4
#define TEST_RXQ 0
#define TEST_TXQ 0

int
main(int argc, char *argv[])
{
    rcf_rpc_server                 *iut_rpcs = NULL;
    rcf_rpc_server                 *tst_rpcs = NULL;
    tapi_env_host                  *tst_host = NULL;
    const struct if_nameindex      *iut_port = NULL;
    const struct if_nameindex      *tst_if   = NULL;

    asn_value                      *template = NULL;
    struct test_ethdev_config       ethdev_config;
    rpc_rte_mempool_p               mp;
    uint16_t                        received = 0;
    unsigned int                    nb_rxd;
    unsigned int                    init_nb_rxd;
    unsigned int                    nb_packets;
    rpc_rte_mbuf_p                 *mbufs = NULL;
    struct tarpc_rte_eth_rxq_info   rx_qinfo;
    struct tarpc_rte_eth_desc_lim  *rx_desc_lim;
    te_bool                         fail_test = FALSE;
    te_bool                         limits_violated;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(template);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_UINT_PARAM(nb_rxd);
    if (nb_rxd > TEST_MAX_NB_RXD)
        TEST_VERDICT("%u exceeded test maximum Rx descriptors %d", nb_rxd,
                     TEST_MAX_NB_RXD);
    init_nb_rxd = nb_rxd;

    TEST_STEP("Prepare default config and mbuf pool");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);
    mp = test_rte_pktmbuf_rx_pool_create(iut_rpcs, iut_port->if_index,
                                         &ethdev_config.dev_info,
                                         TEST_PKTS_MEMPOOL_NAME,
                                         MAX(TEST_MAX_NB_RXD *
                                             TEST_PACKETS_TO_DESC_SCALE,
                                             TEST_RTE_MEMPOOL_DEF_CACHE << 1),
                                         TEST_RTE_MEMPOOL_DEF_CACHE,
                                         TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                         TEST_RTE_MEMPOOL_DEF_DATA_ROOM +
                                         TEST_PAYLOAD_LEN,
                                         ethdev_config.socket_id);
    TEST_STEP("Prepare @c TEST_ETHDEV_CONFIGURED state");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_CONFIGURED));
    rx_desc_lim = &ethdev_config.dev_info.rx_desc_lim;

    TEST_STEP("Set up Rx queue with specified number of Rx descriptors");
    limits_violated = test_desc_nb_violates_limits(nb_rxd, rx_desc_lim);
    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_rx_queue_setup(iut_rpcs, iut_port->if_index, TEST_RXQ,
                                    nb_rxd, ethdev_config.socket_id, NULL, mp);
    if (rc != 0)
    {
        if (limits_violated && rc == -TE_RC(TE_RPC, TE_EINVAL))
            TEST_SKIP("Rx queue set up failed due to violated descriptors limits");
        TEST_VERDICT("Failed to setup Rx queue: %s", errno_rpc2str(-rc));
    }

    TEST_STEP("Check number of descriptor in Rx queue info if supported");
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
            if (limits_violated)
            {
                RING_VERDICT("Requested descriptors number violates limits; "
                             "Driver set up %u descriptors instead",
                             rx_qinfo.nb_desc);
            }
            else
            {
                WARN_VERDICT("Requested descriptors number violates NO limits; "
                             "Driver set up %u descriptors instead",
                             rx_qinfo.nb_desc);
                fail_test = TRUE;
            }
            nb_rxd = rx_qinfo.nb_desc;
            limits_violated = test_desc_nb_violates_limits(nb_rxd, rx_desc_lim);
        }
    }

    if (limits_violated)
    {
        WARN_VERDICT("Setup descriptors number violates reported limits but"
                     " successfully set");
        fail_test = TRUE;
    }

    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    ethdev_config.cur_state = TEST_ETHDEV_RX_SETUP_DONE;
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Prepare @p template for test");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));

    CHECK_RC(asn_write_int32(template, 1, "arg-sets.0.#simple-for.begin"));
    nb_packets = nb_rxd * TEST_PACKETS_TO_DESC_SCALE + 1;
    CHECK_RC(asn_write_int32(template, nb_packets,
                             "arg-sets.0.#simple-for.end"));
    CHECK_RC(tapi_ndn_subst_env(template, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&template, FALSE, NULL,
                                                  TEST_PAYLOAD_LEN));

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Transmit and sniff nb_packets packets from @p tst_if");
    tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0, tst_if->if_name,
                                       template, NULL, NULL);

    te_motivated_usleep(100 * 1000 + 10 * nb_packets,
                        "Wait for packets to be transmitted");

    mbufs = TE_ALLOC(nb_packets * sizeof(*mbufs));
    CHECK_NOT_NULL(mbufs);
    TEST_STEP("Receive packets on @p iut_port");

    /*
     * There are NIC controllers that can queue more received packets then
     * the number of RX descriptors, e.g. QEMU network devices.
     *
     * So that, the only number of packets read at once should be validated.
     */
    received = rpc_rte_eth_rx_burst(iut_rpcs, iut_port->if_index, TEST_RXQ,
                                    mbufs, MIN(nb_packets, UINT16_MAX));

    if (received > nb_rxd)
        TEST_VERDICT("Received %u packets more than setup Rx ring size",
                     received - nb_rxd);
    if (received < init_nb_rxd)
        RING_VERDICT("Received %u packets less than initially requested number of Rx descriptors",
                     init_nb_rxd - received);
    if (received == init_nb_rxd)
    {
        if (nb_rxd != init_nb_rxd)
            RING_VERDICT("%u received packets match initially requested number of Rx descriptors",
                         received);
        else
            RING("%u received packets match initially requested number of Rx descriptors",
                         received);
    }
    if (received > init_nb_rxd)
    {
        if (received == nb_rxd)
            RING_VERDICT("Number of received packets matches effective number of Rx descriptors");
        else
            RING_VERDICT("Received %u packets less than effective number of Rx descriptors",
                         nb_rxd - received);
    }

    if (fail_test)
        TEST_STOP;

    TEST_SUCCESS;

cleanup:
    if (received > 0)
        rpc_rte_pktmbuf_free_array(iut_rpcs, mbufs, received);
    free(mbufs);

    /*
     * Try to read out sent packets to prevent providing them to the following
     * tests.
     */
    test_rx_clean_queue(iut_rpcs, iut_port->if_index, TEST_RXQ);

    TEST_END;
}
