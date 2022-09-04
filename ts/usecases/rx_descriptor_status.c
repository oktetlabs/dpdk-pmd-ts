/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-rx_descriptor_status Check Rx descriptor status correctness
 * @ingroup usecases
 * @{
 *
 * @objective Prove that Rx descriptor status callback readings are consistent
 *
 * @param template Traffic template
 *
 * @type use case
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 *
 * The test is to confirm that Rx descriptor status callback provides accurate
 * readings with respect to certain descriptors in the Rx ring under different
 * circumstances (descriptors made available to the HW for packet reception,
 * descriptors filled by the HW and not yet processed, descriptors captured by
 * Rx burst operation) and in case of reserved entries
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "usecases/rx_descriptor_status"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server              *iut_rpcs = NULL;
    rcf_rpc_server              *tst_rpcs = NULL;
    tapi_env_host               *tst_host = NULL;
    const struct if_nameindex   *iut_port = NULL;
    const struct if_nameindex   *tst_if   = NULL;

    asn_value                   *template = NULL;
    struct test_ethdev_config    ethdev_config;
    uint16_t                     nb_rxd;
    rpc_rte_mempool_p            mp;
    uint16_t                     nb_rxd_eff;
    struct tarpc_rte_eth_rxconf  rxq_config;
    int                          descriptor_status;
    rpc_rte_mbuf_p               mbuf;
    unsigned int                 i;
    struct tarpc_rte_eth_rxq_info rx_qinfo;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(template);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_STEP("Prepare @c TEST_ETHDEV_CONFIGURED state");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);

    /*
     * Set zero Tx queues to be able to not setup Tx queues before starting
     * the device (to find the highest free threshold possible)
     */
    ethdev_config.nb_tx_queue = 0;
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_CONFIGURED));

    TEST_STEP("Setup Rx queue with the highest free threshold possible");
    nb_rxd = ethdev_config.dev_info.rx_desc_lim.nb_min;
    mp = test_rte_pktmbuf_pool_create(iut_rpcs, TEST_PKTS_MEMPOOL_NAME,
                                     MAX(nb_rxd,
                                         TEST_RTE_MEMPOOL_DEF_CACHE << 1),
                                     TEST_RTE_MEMPOOL_DEF_CACHE,
                                     TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                     TEST_RTE_MEMPOOL_DEF_DATA_ROOM,
                                     ethdev_config.socket_id);
    ethdev_config.mp = mp;
    memcpy(&rxq_config, &ethdev_config.dev_info.default_rxconf,
           sizeof(rxq_config));

    for (i = 0, rc = TE_EINVAL; (i < nb_rxd) && (rc != 0); ++i)
    {
        nb_rxd_eff = nb_rxd - i;
        rxq_config.rx_free_thresh = nb_rxd_eff;

        RPC_AWAIT_ERROR(iut_rpcs);
        rc = rpc_rte_eth_rx_queue_setup(iut_rpcs, iut_port->if_index, 0,
                                        nb_rxd, ethdev_config.socket_id,
                                        &rxq_config, ethdev_config.mp);
        if (rc == 0)
        {
            /*
             * The driver could silently set up another descriptors number
             * or free threshold. Try to get the actual values.
             */
            RPC_AWAIT_ERROR(iut_rpcs);
            rc = rpc_rte_eth_rx_queue_info_get(iut_rpcs, iut_port->if_index, 0,
                                               &rx_qinfo);
            if (rc != 0)
            {
                if (rc == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
                    WARN("Get queue info isn't supported by the driver");
                else
                    TEST_VERDICT("Failed to get TxQ %u info: %s", 0,
                                 errno_rpc2str(-rc));
            }
            else
            {
                nb_rxd = rx_qinfo.nb_desc;
                nb_rxd_eff = rx_qinfo.conf.rx_free_thresh;
            }

            RPC_AWAIT_ERROR(iut_rpcs);
            rc = rpc_rte_eth_dev_start(iut_rpcs, iut_port->if_index);
            if (rc == 0)
                rpc_rte_eth_dev_stop(iut_rpcs, iut_port->if_index);
        }
    }
    if (rc != 0)
        TEST_VERDICT("Cannot setup Rx queue");

    ethdev_config.cur_state = TEST_ETHDEV_RX_SETUP_DONE;

    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Check that the first descriptor is exposed to the HW");
    RPC_AWAIT_ERROR(iut_rpcs);
    descriptor_status = rpc_rte_eth_rx_descriptor_status(iut_rpcs,
                                                         iut_port->if_index,
                                                         0, 0);
    if (descriptor_status == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
        TEST_SKIP("Rx descriptor status retrieval is not supported");
    else if (descriptor_status != TARPC_RTE_ETH_RX_DESC_AVAIL)
        TEST_VERDICT("Bad status for the first descriptor, must be AVAIL");

    TEST_STEP("Check that the last usable descriptor is exposed to the HW");
    if (rpc_rte_eth_rx_descriptor_status(iut_rpcs, iut_port->if_index,
                                         0, nb_rxd_eff - 1) !=
        TARPC_RTE_ETH_RX_DESC_AVAIL)
        TEST_VERDICT("Bad status for the last usable descriptor, must be AVAIL");

    TEST_STEP("Verify reserved descriptors (if any)");
    for (i = nb_rxd_eff; i < nb_rxd; ++i)
    {
        if (rpc_rte_eth_rx_descriptor_status(iut_rpcs,
                                             iut_port->if_index, 0, i) !=
            TARPC_RTE_ETH_RX_DESC_UNAVAIL)
            TEST_VERDICT("Bogus status for the reserved descriptor");
    }

    TEST_STEP("Check that non-existing descriptor status retrieval is rejected");
    RPC_AWAIT_ERROR(iut_rpcs);
    if (rpc_rte_eth_rx_descriptor_status(iut_rpcs, iut_port->if_index,
                                         0, nb_rxd) !=
        -TE_RC(TE_RPC, TE_EINVAL))
        WARN_VERDICT("Bogus status for the non-existing descriptor");

    TEST_STEP("Cook traffic template");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));
    CHECK_RC(tapi_ndn_subst_env(template, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&template, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));

    TEST_STEP("Send one packet to IUT");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, template, NULL, NULL));

    MSLEEP(200);

    TEST_STEP("Check that the first descriptor is done");
    if (rpc_rte_eth_rx_descriptor_status(iut_rpcs, iut_port->if_index, 0, 0) !=
        TARPC_RTE_ETH_RX_DESC_DONE)
        TEST_VERDICT("Bad status for the first descriptor, must be DONE");

    TEST_STEP("Grab the packet from the ring");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, 0,
                                         &mbuf, 1, 1, NULL, TRUE));
    rpc_rte_pktmbuf_free(iut_rpcs, mbuf);

    TEST_STEP("Check that the first descriptor is now exposed to the HW");
    if (rpc_rte_eth_rx_descriptor_status(iut_rpcs, iut_port->if_index, 0, 0) !=
        TARPC_RTE_ETH_RX_DESC_AVAIL)
        TEST_VERDICT("Bad status for the first descriptor, must be AVAIL");

    TEST_STEP("Check that the last usable descriptor is now hold by the driver");
    if (rpc_rte_eth_rx_descriptor_status(iut_rpcs, iut_port->if_index,
                                         0, nb_rxd_eff - 1) !=
        TARPC_RTE_ETH_RX_DESC_UNAVAIL)
        TEST_VERDICT("Bad status for the last usable descriptor, must be UNAVAIL");

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
