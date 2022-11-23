/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-rx_scatter Test of RX scatter function
 * @ingroup usecases
 * @{
 *
 * @objective Test checks work of RX scatter function with
 *            different buffer sizes
 *
 * @param enable_scatter        Test with Rx scatter offload enabled
 * @param rx_buf_size           The size of RTE mbuf data buffer
 * @param payload_size          The size of payload data of packet
 * @param packet_headers_size   Sufficient size for headers of all
 *                              package layers
 * @param max_extra_size        The maximum extra size added to
 *                              packet payload
 *
 * @type use case
 *
 * @author Roman Zhukov <Roman.Zhukov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/rx_scatter"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

/**
 * The number of Rx descriptors that must be added when calculating the
 * minimum required number of them due to Rx descriptors refill limitations
 * (more is better and 15 is required on ARMv8 with 128b cache-line size).
 */
#define TEST_RX_DESC_EXTRA  15

int
main(int argc, char *argv[])
{
    rcf_rpc_server                        *iut_rpcs = NULL;
    rcf_rpc_server                        *tst_rpcs = NULL;
    tapi_env_host                         *tst_host;
    const struct if_nameindex             *iut_port = NULL;
    const struct if_nameindex             *tst_if = NULL;

    asn_value                             *tmpl;
    rpc_rte_mbuf_p                         mbufs[BURST_SIZE] = {};
    asn_value                             *ptrn;
    uint16_t                               pre_rxq_setup_mtu;
    uint16_t                               iut_mtu;
    uint16_t                               packet_headers_size;
    unsigned int                           i;

    struct test_ethdev_config              test_ethdev_config;
    uint64_t                               offload;
    char                                  *offload_name;
    struct tarpc_rte_eth_conf              eth_conf;
    uint16_t                               rx_buf_size;
    uint16_t                               payload_size;
    uint16_t                               extra_size;
    uint16_t                               max_extra_size;
    uint16_t                               max_packet_size;
    int                                    n_segs;
    unsigned int                           header_size;
    uint16_t                               nb_rx_desc;
    te_bool                                enable_scatter;


    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_INT_PARAM(rx_buf_size);
    TEST_GET_INT_PARAM(payload_size);
    TEST_GET_INT_PARAM(max_extra_size);
    TEST_GET_INT_PARAM(packet_headers_size);
    TEST_GET_UINT_PARAM(pre_rxq_setup_mtu);
    TEST_GET_BOOL_PARAM(enable_scatter);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);


    TEST_STEP("Prepare Ethernet device for test using buffer size "
              "of @p rx_buf_size for set up a receive queue. Configure Rx "
              "scatter offload if required by @p enable_scatter");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &test_ethdev_config);

    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_INITIALIZED));

    test_ethdev_config.eth_conf = test_rpc_rte_eth_make_eth_conf(
                                      iut_rpcs, iut_port->if_index, &eth_conf);

    if (enable_scatter)
    {
        TEST_STEP("Carry out a sophisticated check to detect the absence of Rx "
                  "scatter feature in the PMD, if DPDK release supports the "
                  "offload definition.");
        offload = (1ULL << TARPC_RTE_ETH_RX_OFFLOAD_SCATTER_BIT);
        offload_name = rpc_rte_eth_dev_rx_offload_name(iut_rpcs, offload);
        if (offload_name != NULL && strcmp(offload_name, "SCATTER") == 0)
        {
            if ((test_ethdev_config.dev_info.rx_offload_capa & offload) == 0)
                TEST_SKIP("Rx scatter is not supported");
        }

        test_ethdev_config.eth_conf->rxmode.flags |=
            (1 << TARPC_RTE_ETH_RXMODE_ENABLE_SCATTER_BIT);
        test_ethdev_config.eth_conf->rxmode.offloads |=
                (1 << TARPC_RTE_ETH_RX_OFFLOAD_SCATTER_BIT);
    }
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_configure(test_ethdev_config.rpcs,
                                   test_ethdev_config.port_id,
                                   test_ethdev_config.nb_rx_queue,
                                   test_ethdev_config.nb_tx_queue,
                                   test_ethdev_config.eth_conf);
    if (rc == -TE_RC(TE_RPC, TE_EINVAL) && enable_scatter)
        TEST_SKIP("Rx scatter is not supported");
    else if (rc < 0)
        TEST_VERDICT("'rte_eth_dev_configure' operation failed (-%s)",
                     errno_rpc2str(-rc));
    else if (rc > 0)
        TEST_VERDICT("'rte_eth_dev_configure' operation failed (%d)", rc);

    /*
     * Do not use helper functions since we want to control exact size
     * of data room.
     */
    test_ethdev_config.mp =
        rpc_rte_pktmbuf_pool_create(iut_rpcs,
                                    TEST_PKTS_MEMPOOL_NAME,
                                    TEST_RTE_MEMPOOL_DEF_SIZE,
                                    TEST_RTE_MEMPOOL_DEF_CACHE,
                                    TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                    rx_buf_size + TEST_PKTMBUF_HEADROOM,
                                    test_ethdev_config.socket_id);

    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_CONFIGURED));

    TEST_STEP("Set MTU according to @p pre_rxq_setup_mtu");
    rpc_rte_eth_dev_get_mtu(iut_rpcs, iut_port->if_index, &iut_mtu);

    if (iut_mtu != pre_rxq_setup_mtu)
    {
        test_rte_eth_dev_set_mtu_await_link_up(iut_rpcs, iut_port->if_index,
                                        pre_rxq_setup_mtu, &test_ethdev_config);
        iut_mtu = pre_rxq_setup_mtu;
    }

    TEST_STEP("Prepare traffic template");
    tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                         &test_params, TEST_IUT_PORT_MAC_NAME);
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));

    TEST_STEP("Setup Rx queue with sufficient number of Rx descriptors");
    header_size = test_get_template_header_length(iut_rpcs, tmpl,
                                                  test_ethdev_config.mp);
    max_packet_size = header_size + payload_size + max_extra_size;

    TEST_STEP("To handle both zero and non-zero remainder add 1 to the result");
    nb_rx_desc = max_packet_size / (rx_buf_size - packet_headers_size) + 1;

    TEST_STEP("Add extra Rx descriptor number to handle the case when free "
              "descriptors number is insufficient due to previous refill that "
              "freed only aligned number of descriptors.");
    nb_rx_desc += TEST_RX_DESC_EXTRA;

    nb_rx_desc = MAX(TE_ALIGN(nb_rx_desc,
                              test_ethdev_config.dev_info.rx_desc_lim.nb_align),
                     test_ethdev_config.dev_info.rx_desc_lim.nb_min);

    RPC_AWAIT_IUT_ERROR(test_ethdev_config.rpcs);
    rc = rpc_rte_eth_rx_queue_setup(test_ethdev_config.rpcs,
                                    test_ethdev_config.port_id, 0, nb_rx_desc,
                                    test_ethdev_config.socket_id, NULL,
                                    test_ethdev_config.mp);
    if (!enable_scatter && iut_mtu > rx_buf_size)
    {
        if (rc == 0)
        {
            WARN_VERDICT("Rx queue setup succeeded unexpectedly: "
                         "Rx scatter is disabled and Rx buffer size is "
                         "insufficient for MTU");
        }
        else
        {
            TEST_SUCCESS;
        }
    }
    else if (rc == -TE_RC(TE_RPC, TE_EINVAL))
    {
        TEST_VERDICT("Rx queue setup failed with EINVAL");
    }
    else if (rc < 0)
    {
        TEST_VERDICT("rte_eth_rx_queue_setup() failed (-%s)",
                     errno_rpc2str(-rc));
    }
    else if (rc > 0)
    {
        TEST_VERDICT("rte_eth_rx_queue_setup() failed (%d)", rc);
    }

    test_ethdev_config.cur_state = TEST_ETHDEV_RX_SETUP_DONE;
    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Check that mtu on @p iut_port and @p tst_if is enough to "
              "send one packet with size equal to the @p payload_size plus "
              "@p packet_headers_size, otherwise set sufficient MTU.");

    if (iut_mtu < max_packet_size)
    {
        RPC_AWAIT_IUT_ERROR(iut_rpcs);
        rc = rpc_rte_eth_dev_set_mtu(iut_rpcs, iut_port->if_index, max_packet_size);
        if (!enable_scatter && max_packet_size > rx_buf_size)
        {
            if (rc == 0)
            {
                WARN_VERDICT("Set MTU succeeded unexpectedly: "
                             "Rx scatter is disabled and Rx buffer size is "
                             "insufficient for MTU");
            }
            else
            {
                TEST_SUCCESS;
            }
        }
        if (rc == -TE_RC(TE_RPC, TE_EINVAL))
        {
            TEST_VERDICT("Set MTU failed with EINVAL");
        }
        else if (rc < 0)
        {
            TEST_VERDICT("rte_eth_dev_set_mtu() failed (-%s)", errno_rpc2str(-rc));
        }
        else if (rc > 0)
        {
            TEST_VERDICT("rte_eth_dev_set_mtu() failed (%d)", rc);
        }

        test_await_link_up(iut_rpcs, iut_port->if_index);
    }

    CHECK_RC(tapi_cfg_base_if_set_mtu_leastwise(tst_host->ta, tst_if->if_name,
                                                max_packet_size));

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Repeat following steps with extra payload size from @c 0 to "
              "@p max_extra_size inclusive.");
    for (extra_size = 0; extra_size <= max_extra_size; extra_size++)
    {
        TEST_SUBSTEP("Set payload of template according to "
                     "@p payload_size + extra size");
        CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                      payload_size +
                                                      extra_size));

        TEST_SUBSTEP("Transmit and sniff one packet from @p tst_if to "
                     "the @p iut_port");
        CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, tmpl, NULL, &ptrn));

        TEST_SUBSTEP("Receive the packet on port @p iut_port and check that "
                     "the received packet matches the sent one.");
        CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, 0,
                                             mbufs, TE_ARRAY_LEN(mbufs),
                                             1, ptrn, TRUE));

        TEST_SUBSTEP("Get information about the number of segments in "
                     "the mbuf chain.");
        n_segs = rpc_rte_pktmbuf_get_nb_segs(iut_rpcs, mbufs[0]);
        INFO("The number of segments in the mbuf chain is %d", n_segs);

        if (!enable_scatter && (n_segs > 1))
        {
            TEST_VERDICT("Rx scatter offload is disabled, but multiple "
                         "segments are found in the mbuf chain");
        }

        for (i = 0; i < TE_ARRAY_LEN(mbufs); i++)
        {
            rpc_rte_pktmbuf_free(iut_rpcs, mbufs[i]);
            mbufs[i] = RPC_NULL;
        }
        asn_free_value(ptrn);
    }

    TEST_SUCCESS;

cleanup:
    for (i = 0; i < TE_ARRAY_LEN(mbufs); i++)
        rpc_rte_pktmbuf_free(iut_rpcs, mbufs[i]);

    TEST_END;
}
/** @} */
