/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-runtime_rx_queue_setup_with_rss Setup Rx queue at runtime
 * @ingroup usecases
 * @{
 *
 * @objective Setup Rx queue when device is started, perform the check
 *            using RSS
 *
 * @param tmpl                      Traffic template
 * @param nb_rxq                    Rx queue count
 * @param rxq_runtime_setup_idx     Index of the queue that will be setup and
 *                                  started at run time
 * @param deferred_start            Does the runtime setup queue need to be
 *                                  marked as deferred
 *
 * @type use case
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * Make sure that it is possible to setup a Rx queue when the Ethernet device
 * is started. Perform the check using RSS RETA.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/runtime_rx_queue_setup_with_rss"

#include "dpdk_pmd_test.h"

static void
receive_and_match(unsigned int nb_rxq, rcf_rpc_server *iut_rpcs,
                  unsigned int if_index, rpc_rte_mbuf_p *mbufs,
                  unsigned int n_mbufs,
                  asn_value *ptrn, unsigned int rxq_runtime_setup_idx)
{
    unsigned int i;
    unsigned int received;
    unsigned int nb_matched;

    for (i = 0; i < nb_rxq; i++)
    {
        if (i == rxq_runtime_setup_idx)
            continue;

        received = test_rx_burst_with_retries(iut_rpcs, if_index, i, mbufs,
                                              n_mbufs, 1);
        if (received == 1)
            break;
    }
    CHECK_PACKETS_NUM(received, 1);

    rpc_rte_mbuf_match_pattern(iut_rpcs, ptrn, mbufs, 1, NULL, &nb_matched);
    CHECK_MATCHED_PACKETS_NUM(nb_matched, 1);

    rpc_rte_pktmbuf_free(iut_rpcs, mbufs[0]);
}

static void
update_reta(rcf_rpc_server *iut_rpcs, unsigned int if_index,
            struct tarpc_rte_eth_rss_reta_entry64 *reta_conf,
            uint64_t reta_size)
{
    te_errno rc;

    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_rss_reta_update(iut_rpcs, if_index,
                                         reta_conf, reta_size);
    switch (-rc)
    {
        case 0:
            break;
        case TE_RC(TE_RPC, TE_EOPNOTSUPP):
            TEST_SKIP("RSS redirection table update not supported");
            break;
        default:
            TEST_VERDICT("RSS redirection table update failed: %s",
                         errno_rpc2str(-rc));
    }
}

static void
edit_tmpl_and_addr(te_bool symmetric, const te_toeplitz_hash_cache *hash_cache,
                   const struct tarpc_rte_eth_rss_conf *rss_conf,
                   uint16_t reta_size,
                   const unsigned int *reta_indxs_q,
                   unsigned int nb_reta_indxs_q,
                   unsigned int addr_size, uint8_t *src_addr, asn_value *tmpl)
{
    te_errno rc;
    uint32_t hash;

    CHECK_RC(test_calc_hash_by_tmpl_and_hf(rss_conf->rss_hf,
                                           rss_conf->rss_key.rss_key_val,
                                           rss_conf->rss_key_len, tmpl,
                                           symmetric ? NULL : &hash,
                                           symmetric ? &hash : NULL));

    rc = test_change_src_addr_by_reta_index(
            hash_cache, hash,
            symmetric ? (src_addr + addr_size - TEST_IP4_ADDR_LEN) : src_addr,
            symmetric ? TEST_IP4_ADDR_LEN : addr_size,
            reta_size, reta_indxs_q, nb_reta_indxs_q);

    if (rc != 0)
    {
        TEST_VERDICT("RSS configuration does not allow to receive a packet "
                     "on the %u queue", nb_reta_indxs_q);
    }

    CHECK_RC(asn_write_value_field(tmpl, src_addr, addr_size,
                                   (addr_size == TEST_IP4_ADDR_LEN) ?
                                   "pdus.1.#ip4.src-addr.#plain" :
                                   "pdus.1.#ip6.src-addr.#plain"));
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server                         *iut_rpcs = NULL;
    rcf_rpc_server                         *tst_rpcs = NULL;
    tapi_env_host                          *tst_host;
    const struct if_nameindex              *iut_port = NULL;
    const struct if_nameindex              *tst_if = NULL;
    const struct sockaddr                  *tst_addr  = NULL;
    struct tarpc_rte_eth_conf               eth_conf;
    struct tarpc_rte_eth_dev_info           dev_info;

    struct tarpc_rte_eth_rxconf             rx_conf;
    const struct tarpc_rte_eth_rss_conf    *rss_conf;
    struct tarpc_rte_eth_rss_reta_entry64  *reta_conf;
    struct tarpc_rte_eth_rss_reta_entry64  *reta_conf_tmp;
    uint64_t                                reta_size;
    unsigned int                           *reta_indxs_q;
    unsigned int                            nb_reta_indxs_q;

    te_toeplitz_hash_cache                 *hash_cache;
    tarpc_rss_hash_protos_t                 hash_functions;
    uint8_t                                *src_addr = NULL;
    unsigned int                            addr_size;

    asn_value                              *tmpl = NULL;
    asn_value                              *tmpl_symm = NULL;
    asn_value                              *ptrn = NULL;
    asn_value                              *ptrn_symm = NULL;

    rpc_rte_mbuf_p                          mbufs[BURST_SIZE] = {};
    unsigned int                            nb_rxq;
    unsigned int                            rxq_runtime_setup_idx;
    te_bool                                 deferred_start;
    unsigned int                            i;
    unsigned int                            nb_matched;
    unsigned int                            j;
    unsigned int                            k;
    unsigned int                            received;

    struct test_ethdev_config               ec;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_UINT_PARAM(nb_rxq);
    TEST_GET_UINT_PARAM(rxq_runtime_setup_idx);
    TEST_GET_BOOL_PARAM(deferred_start);

    TEST_STEP("Configure the Ethernet device");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ec);

    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_INITIALIZED));

    ec.eth_conf = test_rpc_rte_eth_make_eth_conf(iut_rpcs, iut_port->if_index,
                                                 &eth_conf);
    ec.nb_rx_queue = nb_rxq;

    TEST_STEP("Prepare desired RSS hash configuration");
    CHECK_RC(test_get_rss_hf_by_tmpl(tmpl, &hash_functions));
    hash_functions &= ec.dev_info.flow_type_rss_offloads;
    test_rx_mq_rss_prepare(&ec, hash_functions);

    ec.mp = test_rte_pktmbuf_rx_pool_create(iut_rpcs, iut_port->if_index,
                                            &ec.dev_info,
                                            TEST_PKTS_MEMPOOL_NAME,
                                            TEST_RTE_MEMPOOL_DEF_SIZE,
                                            TEST_RTE_MEMPOOL_DEF_CACHE,
                                            TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                            TEST_RTE_MEMPOOL_DEF_DATA_ROOM,
                                            ec.socket_id);

    TEST_STEP("Check for Rx queue runtime setup capability");
    memset(&dev_info, 0, sizeof(dev_info));
    rpc_rte_eth_dev_info_get(iut_rpcs, iut_port->if_index, &dev_info);

    if (!(dev_info.dev_capa &
          (1ULL << TARPC_RTE_ETH_DEV_CAPA_RUNTIME_RX_QUEUE_SETUP_BIT)))
    {
        TEST_SKIP("Runtime Rx queue setup is not supported by the device");
    }


    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_CONFIGURED));


    TEST_STEP("Setup the queues that were not marked for runtime setup");
    for (i = 0; i < nb_rxq; i++)
    {
        if (i != rxq_runtime_setup_idx)
        {
            rpc_rte_eth_rx_queue_setup(ec.rpcs, ec.port_id, i,
                                       ec.dev_info.rx_desc_lim.nb_min,
                                       ec.socket_id, NULL, ec.mp);
        }
    }

    TEST_STEP("Start the device");
    rpc_rte_eth_dev_start(ec.rpcs, ec.port_id);

    TEST_STEP("Stop and start the device again to make sure that the device can be "
              "stopped while some of its queues are not set up");
    rpc_rte_eth_dev_stop(ec.rpcs, ec.port_id);
    rpc_rte_eth_dev_start(ec.rpcs, ec.port_id);

    TEST_STEP("Prepare @p tmpl for test");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));

    TEST_STEP("Get default RETA configuration which implies that "
              "all queues are setup");
    test_get_rss_reta(iut_rpcs, iut_port->if_index, &reta_size, &reta_conf);

    TEST_STEP("Prepare template for packets which would have been received on "
              "@p rxq_runtime_setup_idx queue if RETA had been configured by default");
    reta_indxs_q = tapi_calloc(reta_size, sizeof(*reta_indxs_q));
    for (i = 0, nb_reta_indxs_q = 0; i < reta_size; i++)
    {
        if (reta_conf[i / RPC_RTE_RETA_GROUP_SIZE].reta[
                i % RPC_RTE_RETA_GROUP_SIZE] == rxq_runtime_setup_idx)
            reta_indxs_q[nb_reta_indxs_q++] = i;
    }

    TEST_STEP("Establish effective RSS hash configuration");
    rss_conf = test_rx_mq_rss_establish(&ec, FALSE);

    src_addr = te_sockaddr_get_netaddr(tst_addr);
    addr_size = (unsigned int)te_netaddr_get_size(tst_addr->sa_family);

    hash_cache = te_toeplitz_cache_init_size(rss_conf->rss_key.rss_key_val,
                                             rss_conf->rss_key_len);

    edit_tmpl_and_addr(FALSE, hash_cache, rss_conf, reta_size, reta_indxs_q,
                       nb_reta_indxs_q, addr_size, src_addr, tmpl);

    tmpl_symm = asn_copy_value(tmpl);

    edit_tmpl_and_addr(TRUE, hash_cache, rss_conf, reta_size, reta_indxs_q,
                       nb_reta_indxs_q, addr_size, src_addr, tmpl_symm);

    TEST_STEP("Configure RETA to redirect all packets to only setup queues");
    reta_conf_tmp = tapi_calloc(reta_size / RPC_RTE_RETA_GROUP_SIZE,
                                sizeof(*reta_conf));
    for (i = 0, k = 0; i < reta_size / RPC_RTE_RETA_GROUP_SIZE; i++)
    {
        for (j = 0; j < RPC_RTE_RETA_GROUP_SIZE; j++)
        {
            k += (k % nb_rxq) == rxq_runtime_setup_idx ? 1 : 0;
            reta_conf_tmp[i].reta[j] = k % nb_rxq;
            k++;
        }

        reta_conf_tmp[i].mask = ~0;
    }

    update_reta(iut_rpcs, iut_port->if_index, reta_conf_tmp, reta_size);

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Check that a packet with the prepared template is received on one "
              "of the setup queues");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, tmpl, NULL, &ptrn));

    receive_and_match(nb_rxq, iut_rpcs, iut_port->if_index, mbufs,
                      TE_ARRAY_LEN(mbufs), ptrn, rxq_runtime_setup_idx);
    asn_free_value(ptrn);

    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                tst_if->if_name, tmpl_symm, NULL, &ptrn_symm));

    receive_and_match(nb_rxq, iut_rpcs, iut_port->if_index, mbufs,
                      TE_ARRAY_LEN(mbufs), ptrn_symm, rxq_runtime_setup_idx);
    asn_free_value(ptrn_symm);

    TEST_STEP("Setup the @p rxq_runtime_setup_idx queue");
    memcpy(&rx_conf, &ec.dev_info.default_rxconf, sizeof(rx_conf));
    rx_conf.rx_deferred_start = deferred_start ? 1 : 0;

    rpc_rte_eth_rx_queue_setup(ec.rpcs, ec.port_id, rxq_runtime_setup_idx,
                               ec.dev_info.rx_desc_lim.nb_min,
                               ec.socket_id, &rx_conf, ec.mp);

    TEST_STEP("Restart the device to start all Rx queues except @p rxq_runtime_setup_idx "
              "queue if @p deferred_start is @c TRUE. Also make sure that the current "
              "state of the queues does not cause device start and stop failures");
    rpc_rte_eth_dev_stop(ec.rpcs, ec.port_id);
    rpc_rte_eth_dev_start(ec.rpcs, ec.port_id);

    TEST_STEP("Start the @p rxq_runtime_setup_idx queue if @p deferred_start is @c TRUE");
    if (deferred_start)
        test_start_rx_queue(ec.rpcs, ec.port_id, rxq_runtime_setup_idx);

    TEST_STEP("Reset RETA to default state");
    update_reta(iut_rpcs, iut_port->if_index, reta_conf, reta_size);

    TEST_STEP("Check that a packet with the prepared template is received on the "
              "@p rxq_runtime_setup_idx queue");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, tmpl, NULL, &ptrn));

    received = test_rx_burst_with_retries(iut_rpcs, iut_port->if_index,
                                          rxq_runtime_setup_idx, mbufs,
                                          TE_ARRAY_LEN(mbufs), 1);
    if (received == 1)
    {
        rpc_rte_mbuf_match_pattern(iut_rpcs, ptrn, mbufs, 1, NULL,
                                   &nb_matched);
        CHECK_MATCHED_PACKETS_NUM(nb_matched, 1);
    }
    else
    {
        CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                tst_if->if_name, tmpl_symm, NULL, &ptrn_symm));

        CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index,
                                             rxq_runtime_setup_idx, mbufs,
                                             TE_ARRAY_LEN(mbufs),
                                             1, ptrn_symm, TRUE));
    }
    rpc_rte_pktmbuf_free(iut_rpcs, mbufs[0]);

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
