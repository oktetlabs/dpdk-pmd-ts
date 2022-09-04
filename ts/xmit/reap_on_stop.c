/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Transmit functionality
 */

/** @defgroup xmit-reap_on_stop Prove that reap is done correctly on port stop
 * @ingroup xmit
 * @{
 *
 * @objective Make sure that PMD is able to free all remaining mbufs
 *            connected with any of descriptors pending on port stop
 *
 * @param template             Traffic template
 * @param multi_mempool        If @c TRUE, multiple mempools will be
 *                             created for building mbuf chains from
 *                             randomly sized segments pulled out of
 *                             those pools provided that the feature
 *                             is supported by the transmit datapath
 * @param update_refcnt        Pktmbuf reference counter toggle used
 *                             to activate reap operation check with
 *                             respect to packets which shall not be
 *                             released upon a Tx burst or port stop
 * @param ring_size_multiplier An integer multiplier of Tx ring size
 *                             used to compute the number of packets
 *
 * @type conformance
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 *
 * The test is to verify that PMD is able to free mbufs
 * involved in transmission in case if port stop occurs
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "xmit/reap_on_stop"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

#include "tapi_ndn.h"
#include "tapi_tad.h"
#include "tapi_rpc_rte_mempool.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server            *iut_rpcs = NULL;
    tapi_env_host             *tst_host = NULL;
    const struct if_nameindex *iut_port = NULL;
    const struct if_nameindex *tst_if = NULL;
    asn_value                 *template;
    te_bool                    multi_mempool;
    te_bool                    update_refcnt;
    unsigned int               ring_size_multiplier;

    asn_value                 *template_copy;
    struct test_ethdev_config  ethdev_config;
    uint16_t                   nb_txd;
    unsigned int               nb_packets_to_send;
    te_bool                    multiseg_allowed;
    unsigned int               nb_mp;
    unsigned int               mp_size;
    rpc_rte_mempool_p          mp[1 << 1 << 1];
    unsigned int               mp_objs_in_use;
    unsigned int               mp_objs_in_use_est;
    te_bool                    prev_send_failed = FALSE;
    unsigned int               i;

    TEST_START;

    TEST_GET_PCO(iut_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(template);
    TEST_GET_BOOL_PARAM(multi_mempool);
    TEST_GET_BOOL_PARAM(update_refcnt);
    TEST_GET_UINT_PARAM(ring_size_multiplier);

    template_copy = asn_copy_value(template);
    CHECK_NOT_NULL(template_copy);

    TEST_STEP("Obtain the source Ethernet address");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params, "iut_mac"));

    TEST_STEP("Adjust the traffic template");
    CHECK_RC(tapi_ndn_subst_env(template_copy, &test_params, &env));
    rc = asn_free_child(template_copy, PRIVATE, NDN_TMPL_ARGS);
    CHECK_RC((rc == TE_EASNINCOMPLVAL) ? 0 : rc);

    TEST_STEP("Prepare @c TEST_ETHDEV_RX_SETUP_DONE state");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &ethdev_config,
                                         TEST_ETHDEV_RX_SETUP_DONE));

    TEST_STEP("Prepare @c TEST_ETHDEV_TX_SETUP_DONE state");
    nb_txd = TE_ALIGN(BURST_SIZE, ethdev_config.dev_info.tx_desc_lim.nb_align);
    nb_txd = MAX(nb_txd, ethdev_config.dev_info.tx_desc_lim.nb_min);
    rpc_rte_eth_tx_queue_setup(iut_rpcs, iut_port->if_index, 0,
                               nb_txd, ethdev_config.socket_id, NULL);
    ethdev_config.cur_state = TEST_ETHDEV_TX_SETUP_DONE;

    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    nb_packets_to_send = ring_size_multiplier * nb_txd;
    multiseg_allowed = ((ethdev_config.dev_info.tx_offload_capa &
                         (1ULL << TARPC_RTE_DEV_TX_OFFLOAD_MULTI_SEGS_BIT)) ==
                        (1ULL << TARPC_RTE_DEV_TX_OFFLOAD_MULTI_SEGS_BIT));

    if ((multi_mempool == TRUE) &&
        ((ethdev_config.dev_info.tx_offload_capa &
          (1ULL << TARPC_RTE_DEV_TX_OFFLOAD_MBUF_FAST_FREE_BIT)) ==
         (1ULL << TARPC_RTE_DEV_TX_OFFLOAD_MBUF_FAST_FREE_BIT)))
    {
        TEST_SKIP("Multi-mempool transmit is prohibited");
    }

    nb_mp = (multi_mempool == TRUE) ? TE_ARRAY_LEN(mp) : 1;

    if ((update_refcnt == TRUE) &&
        ((ethdev_config.dev_info.tx_offload_capa &
          (1ULL << TARPC_RTE_DEV_TX_OFFLOAD_MBUF_FAST_FREE_BIT)) ==
         (1ULL << TARPC_RTE_DEV_TX_OFFLOAD_MBUF_FAST_FREE_BIT)))
    {
        TEST_SKIP("It is prohibited to update reference counters");
    }

    TEST_STEP("Set up a mempool (or multiple mempool instances)");
    mp_size = TEST_RTE_MEMPOOL_DEF_SIZE;
    while (mp_size <= nb_packets_to_send)
        mp_size = mp_size << 1;

    if (update_refcnt)
        mp_size = mp_size << 1 << 1;

    for (i = 0; i < nb_mp; ++i)
    {
        char mp_name[1 << 1];

        mp_name[0] = 'a' + i;
        mp_name[1] = '\0';

        mp[i] = test_rte_pktmbuf_pool_create(iut_rpcs, mp_name, mp_size,
                                            TEST_RTE_MEMPOOL_DEF_CACHE,
                                            TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                            TEST_RTE_MEMPOOL_DEF_DATA_ROOM,
                                            ethdev_config.socket_id);
    }

    mp_objs_in_use_est = 0;

    TEST_STEP("Generate packets and send them in bursts");
    while (nb_packets_to_send > 0)
    {
        uint16_t       burst_size = MIN(BURST_SIZE, nb_packets_to_send);
        rpc_rte_mbuf_p burst[burst_size];
        uint16_t       nb_packets_sent;

        for (i = 0; i < burst_size; ++i)
        {
            unsigned int    payload_len;
            rpc_rte_mbuf_p *mbufs_by_template = NULL;
            unsigned int    nb_mbufs_by_template = 0;

            payload_len = rand_range(1, TEST_RTE_MEMPOOL_DEF_DATA_ROOM >> 1);
            rc = asn_free_child(template_copy, PRIVATE, NDN_TMPL_PAYLOAD);
            CHECK_RC((rc == TE_EASNINCOMPLVAL) ? 0 : rc);

            asn_write_value_field(template_copy, &payload_len,
                                  sizeof(payload_len), "payload.#length");

            tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, template_copy, mp[0],
                                             NULL, &mbufs_by_template,
                                             &nb_mbufs_by_template, NULL);

            if (nb_mbufs_by_template == 0)
                TEST_VERDICT("Failed to prepare mbuf(s) from template");

            burst[i] = mbufs_by_template[0];
            free(mbufs_by_template);
        }

        if (multiseg_allowed)
        {
            tapi_rte_pktmbuf_random_redist(iut_rpcs, mp, nb_mp,
                                           burst, burst_size);
        }

        if (update_refcnt)
        {
            for (i = 0; i < burst_size; ++i)
            {
                mp_objs_in_use_est += rpc_rte_pktmbuf_get_nb_segs(iut_rpcs,
                                                                  burst[i]);
                rpc_rte_pktmbuf_refcnt_update(iut_rpcs, burst[i], 1);
            }
        }

        nb_packets_sent = test_tx_prepare_and_burst(iut_rpcs,
                                                    iut_port->if_index, 0,
                                                    burst, burst_size);

        for (i = nb_packets_sent; i < burst_size; ++i)
        {
            if (update_refcnt)
            {
                rpc_rte_pktmbuf_refcnt_update(iut_rpcs, burst[i], -1);
                mp_objs_in_use_est -= rpc_rte_pktmbuf_get_nb_segs(iut_rpcs,
                                                                  burst[i]);
            }

            rpc_rte_pktmbuf_free(iut_rpcs, burst[i]);
        }

        if (nb_packets_sent == 0)
        {
            if (prev_send_failed)
                TEST_VERDICT("Cannot send packets");
            else
                prev_send_failed = TRUE;
        }
        else
        {
            prev_send_failed = FALSE;
        }

        nb_packets_to_send -= nb_packets_sent;
    }

    TEST_STEP("Prepare @c TEST_ETHDEV_STOPPED state");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STOPPED));

    TEST_STEP("Make sure that all the objects within the mempool are free");
    mp_objs_in_use = 0;
    for (i = 0; i < nb_mp; ++i)
        mp_objs_in_use += rpc_rte_mempool_in_use_count(iut_rpcs, mp[i]);

    if (mp_objs_in_use != mp_objs_in_use_est)
        TEST_VERDICT("Wrong number of residual mbufs: %u; must be %u",
                     mp_objs_in_use, mp_objs_in_use_est);

    TEST_SUCCESS;

cleanup:
    if (rpc_rte_eth_dev_is_valid_port(iut_rpcs, iut_port->if_index))
    {
        rpc_rte_eth_dev_stop(iut_rpcs, iut_port->if_index);
        rpc_rte_eth_dev_close(iut_rpcs, iut_port->if_index);
    }

    rpc_rte_mempool_free_all(iut_rpcs);

    TEST_END;
}
/** @} */
