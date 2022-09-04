/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-tx_descriptor_status Check Tx descriptor status correctness
 * @ingroup usecases
 * @{
 *
 * @objective Prove that Tx descriptor status callback readings are consistent
 *
 * @param template Traffic template
 *
 * @type use case
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 *
 * The test is to confirm that Tx descriptor status callback provides accurate
 * readings with respect to certain descriptors in the Tx ring under different
 * circumstances (descriptors added to the Tx queue, descriptors acknowledged
 * by the HW as done) and in case of reserved entries
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "usecases/tx_descriptor_status"

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
    struct tarpc_rte_eth_conf    eth_conf;
    uint16_t                     nb_txd;
    rpc_rte_mempool_p            mp;
    uint16_t                     nb_txd_eff;
    struct tarpc_rte_eth_txconf  txq_config;
    int                          descriptor_status;
    rpc_rte_mbuf_p              *mbufs;
    asn_value                   *pattern;
    unsigned int                 nb_mbufs_prepared = 0;
    uint16_t                     nb_mbufs_processed;
    unsigned int                 i;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(template);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_STEP("Prepare @c TEST_ETHDEV_RX_SETUP_DONE state");
    (void)test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);
    ethdev_config.nb_rx_queue = 1;
    ethdev_config.nb_tx_queue = 1 << 1;
    (void)test_rpc_rte_eth_make_eth_conf(iut_rpcs, iut_port->if_index,
                                         &eth_conf);
    ethdev_config.eth_conf = &eth_conf;
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_RX_SETUP_DONE));

    TEST_STEP("Setup the second Tx queue with default settings. Do it before the first "
              "Tx queue setup to be able to start the device right after the first Tx "
              "queue setup to find the highest free threshold possible");
    memcpy(&txq_config, &ethdev_config.dev_info.default_txconf,
           sizeof(txq_config));
    rpc_rte_eth_tx_queue_setup(iut_rpcs, iut_port->if_index, 1,
                               ethdev_config.dev_info.tx_desc_lim.nb_min,
                               ethdev_config.socket_id, &txq_config);

    TEST_STEP("Setup the first Tx queue with the highest free threshold possible");
    nb_txd = ethdev_config.dev_info.tx_desc_lim.nb_min;
    memcpy(&txq_config, &ethdev_config.dev_info.default_txconf,
           sizeof(txq_config));
    for (i = 0, rc = TE_EINVAL; (i < nb_txd) && (rc != 0); ++i)
    {
        nb_txd_eff = nb_txd - i;
        txq_config.tx_free_thresh = nb_txd_eff;

        RPC_AWAIT_ERROR(iut_rpcs);
        rc = rpc_rte_eth_tx_queue_setup(iut_rpcs, iut_port->if_index, 0,
                                        nb_txd, ethdev_config.socket_id,
                                        &txq_config);
        if (rc == 0)
        {
            RPC_AWAIT_ERROR(iut_rpcs);
            rc = rpc_rte_eth_dev_start(iut_rpcs, iut_port->if_index);
            if (rc == 0)
                rpc_rte_eth_dev_stop(iut_rpcs, iut_port->if_index);
        }
    }
    if (rc != 0)
        TEST_VERDICT("Cannot setup Tx queue");

    ethdev_config.cur_state = TEST_ETHDEV_TX_SETUP_DONE;

    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Check that the first descriptor is unused");
    RPC_AWAIT_ERROR(iut_rpcs);
    descriptor_status = rpc_rte_eth_tx_descriptor_status(iut_rpcs,
                                                         iut_port->if_index,
                                                         1, 0);
    if (descriptor_status == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
        TEST_SKIP("Tx descriptor status retrieval is not supported");
    else if (descriptor_status != TARPC_RTE_ETH_TX_DESC_DONE)
        TEST_VERDICT("Bad status for the first descriptor, must be DONE");

    TEST_STEP("Check that the last usable descriptor is unused");
    if (rpc_rte_eth_tx_descriptor_status(iut_rpcs, iut_port->if_index,
                                         1, nb_txd_eff - 1) !=
        TARPC_RTE_ETH_TX_DESC_DONE)
        TEST_VERDICT("Bad status for the last usable descriptor, must be DONE");

    TEST_STEP("Verify reserved descriptors (if any)");
    for (i = nb_txd_eff; i < nb_txd; ++i)
    {
        if (rpc_rte_eth_tx_descriptor_status(iut_rpcs,
                                             iut_port->if_index, 1, i) !=
            TARPC_RTE_ETH_TX_DESC_UNAVAIL)
            TEST_VERDICT("Bogus status for the reserved descriptor");
    }

    TEST_STEP("Check that non-existing descriptor status retrieval is rejected");
    RPC_AWAIT_ERROR(iut_rpcs);
    if (rpc_rte_eth_tx_descriptor_status(iut_rpcs, iut_port->if_index,
                                         1, nb_txd) !=
        -TE_RC(TE_RPC, TE_EINVAL))
        WARN_VERDICT("Bogus status for the non-existing descriptor");

    TEST_STEP("Prepare sufficient mempool");
    mp = test_rte_pktmbuf_pool_create(iut_rpcs, "tx" TEST_PKTS_MEMPOOL_NAME,
                                     MAX(nb_txd,
                                         TEST_RTE_MEMPOOL_DEF_CACHE << 1),
                                     TEST_RTE_MEMPOOL_DEF_CACHE,
                                     TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                     TEST_RTE_MEMPOOL_DEF_DATA_ROOM,
                                     ethdev_config.socket_id);

    TEST_STEP("Cook traffic template");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));
    CHECK_RC(asn_write_int32(template, 1, "arg-sets.0.#simple-for.begin"));
    CHECK_RC(asn_write_int32(template, nb_txd_eff,
                             "arg-sets.0.#simple-for.end"));
    CHECK_RC(tapi_ndn_subst_env(template, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&template, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF << 3));
    tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, template, mp, NULL, &mbufs,
                                     &nb_mbufs_prepared, &pattern);
    if (nb_mbufs_prepared != nb_txd_eff)
    {
        TEST_VERDICT("Unexpected number of mbufs: %u mbufs have been "
                     "produced, but should be %hu", nb_mbufs_prepared,
                     nb_txd_eff);
    }

    TEST_STEP("Validate and add the packets to the Tx queue on IUT");
    nb_mbufs_processed = test_tx_prepare_and_burst(iut_rpcs,
                                                   iut_port->if_index, 1,
                                                   mbufs, nb_txd_eff);
    if (nb_mbufs_processed == 0)
        TEST_VERDICT("Cannot send any packet");

    TEST_STEP("Check that the first descriptor is either FULL or DONE");
    descriptor_status = rpc_rte_eth_tx_descriptor_status(iut_rpcs,
                                                         iut_port->if_index,
                                                         1, 0);
    if ((descriptor_status != TARPC_RTE_ETH_TX_DESC_DONE) &&
        (descriptor_status != TARPC_RTE_ETH_TX_DESC_FULL))
        TEST_VERDICT("Bogus status for the descriptor in flight");

    if (descriptor_status == TARPC_RTE_ETH_TX_DESC_DONE)
        TEST_SUCCESS;

    TEST_STEP("If the first descriptor isn't DONE, delay "
              "and check if it becomes DONE");
    MSLEEP(200);

    descriptor_status = rpc_rte_eth_tx_descriptor_status(iut_rpcs,
                                                         iut_port->if_index,
                                                         1, 0);
    switch (descriptor_status)
    {
        case TARPC_RTE_ETH_TX_DESC_FULL:
            WARN_VERDICT("Status of the first descriptor is FULL after single "
                         "Tx burst");
            break;
        case TARPC_RTE_ETH_TX_DESC_DONE:
            break;
        default:
            TEST_VERDICT("Bad status for the first descriptor");
            break;
    }

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
