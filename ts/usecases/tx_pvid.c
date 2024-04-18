/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2024 OKTET Labs. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-tx_pvid Set port based VLAN ID
 * @ingroup usecases
 * @{
 *
 * @objective Check port based VLAN ID insertion support
 *
 * @param tmpl                  Traffic template
 * @param txmode_pvid           VLAN ID to set PVID using ethdev configuration
 * @param txmode_prio           VLAN priority to set using ethdev configuration
 * @param txmode_cfi            VLAN CFI to set using ethdev configuration
 * @param api_pvid              VLAN ID to set PVID using RTE API
 * @param api_prio              VLAN priority field to set using RTE API
 * @param api_cfi               VLAN CFI field to set using RTE API
 *
 * @type use case
 *
 * @author Daniil Byshenko <daniil.byshenko@oktetlabs.ru>
 *
 * Check the correctness of port based VLAN ID insertion
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "usecases/tx_pvid"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

#define NB_PKTS 1
#define PVID_OFF 0
#define PVID_ON 1
#define TEST_TXQ 0

static uint16_t
test_get_pvid(uint16_t pvid, uint8_t prio, te_bool cfi)
{
    return (pvid & 0x0FFF) | (cfi << 12) | ((prio & 0x0F) << 13);
}

static void
test_pattern_add_pvid(asn_value *tmpl, uint16_t pvid, uint16_t prio, uint16_t cfi)
{
    asn_value *pdus;

    CHECK_NOT_NULL(tmpl);
    CHECK_NOT_NULL(pdus = asn_find_descendant(tmpl, NULL, "pdus"));

    CHECK_RC(tapi_ndn_pdus_inject_vlan_tags(pdus, &pvid, &prio, &cfi, 1));
}

static void
test_vlan_pvid(rcf_rpc_server *iut_rpcs, const struct if_nameindex *iut_port,
               const struct if_nameindex *tst_if, tapi_env_host *tst_host,
               rpc_rte_mempool_p mp, asn_value *tmpl, te_bool pvid_on,
               uint16_t pvid, uint16_t prio, uint16_t cfi)
{
    asn_value *tmpl_copy;
    rpc_rte_mbuf_p *mbufs = NULL;
    asn_value *ptrn = NULL;
    unsigned int recv;
    csap_handle_t rx_csap;
    unsigned int sent;
    unsigned int count;
    unsigned int no_match_pkts;

    tmpl_copy = asn_copy_value(tmpl);

    tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, tmpl_copy, mp, NULL,
                                     &mbufs, &count, &ptrn);
    if (count != NB_PKTS)
    {
        TEST_VERDICT("Unexpected number of prepared mbufs: %d mbufs have "
                     "been produced, but should be %d", count, NB_PKTS);
    }

    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(tst_host->ta, 0,
                                                tst_if->if_name,
                                                TAD_ETH_RECV_DEF,
                                                tmpl_copy, &rx_csap));

    if (pvid_on) {
        test_pattern_add_pvid(tmpl_copy, pvid, prio, cfi);
        ptrn = tapi_tad_mk_pattern_from_template(tmpl_copy);
    }

    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, rx_csap, ptrn,
                                   RECEIVE_TIMEOUT_DEF, 0,
                                   RCF_TRRECV_PACKETS | RCF_TRRECV_SEQ_MATCH |
                                   RCF_TRRECV_MISMATCH));

    TEST_STEP("Send the mbufs from @p iut_port");
    sent = test_tx_prepare_and_burst(iut_rpcs, iut_port->if_index, TEST_TXQ,
                                     mbufs, count);
    TEST_STEP("Receive and match the packets");
    CHECK_RC(test_rx_await_pkts(tst_host->ta, rx_csap, sent, 1000 * NB_PKTS));

    CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL, &recv));

    TEST_STEP("Check that no extra packets are received on Tester");
    CHECK_RC(tapi_tad_csap_get_no_match_pkts(tst_host->ta, 0, rx_csap,
                                             &no_match_pkts));
    if (no_match_pkts != 0)
        TEST_VERDICT("%u not matching packets were received", no_match_pkts);
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server                 *iut_rpcs = NULL;
    tapi_env_host                  *tst_host = NULL;
    const struct if_nameindex      *iut_port = NULL;
    const struct if_nameindex      *tst_if = NULL;

    rpc_rte_mempool_p               mp;
    asn_value                      *tmpl = NULL;
    struct test_ethdev_config       ethdev_config;
    struct tarpc_rte_eth_conf       eth_conf;
    uint16_t                        pvid;
    int                             txmode_pvid;
    unsigned int                    txmode_prio;
    unsigned int                    txmode_cfi;
    int                             api_pvid;
    unsigned int                    api_prio;
    unsigned int                    api_cfi;

    TEST_START;
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_VLAN_ID_PARAM(txmode_pvid);
    TEST_GET_UINT_PARAM(txmode_prio);
    TEST_GET_UINT_PARAM(txmode_cfi);
    TEST_GET_VLAN_ID_PARAM(api_pvid);
    TEST_GET_UINT_PARAM(api_prio);
    TEST_GET_UINT_PARAM(api_cfi);

    TEST_STEP("Initialize EAL");
    (void)test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);

    ethdev_config.eth_conf = test_rpc_rte_eth_make_eth_conf(ethdev_config.rpcs,
                                            ethdev_config.port_id, &eth_conf);

    mp = test_rte_pktmbuf_pool_create(iut_rpcs, "tx_pvid",
                                      TEST_RTE_MEMPOOL_DEF_CACHE << 1,
                                      TEST_RTE_MEMPOOL_DEF_CACHE,
                                      TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                      TEST_RTE_MEMPOOL_DEF_DATA_ROOM,
                                      ethdev_config.socket_id);

    TEST_STEP("Prepare mbuf to be sent and pattern to match it by @p tmpl");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));

    if (txmode_pvid >= 0)
    {
        TEST_STEP("Enable Tx PVID directly");
        eth_conf.txmode.flags = 1 <<
                                TARPC_RTE_ETH_TXMODE_HW_VLAN_INSERT_PVID_BIT;
        eth_conf.txmode.pvid = test_get_pvid(txmode_pvid, txmode_prio, txmode_cfi);
    }

    TEST_STEP("Start the Ethernet device with Tx PVID offload enabled");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Check Tx PVID configuration after start");
    test_vlan_pvid(iut_rpcs, iut_port, tst_if, tst_host, mp, tmpl,
                   (txmode_pvid >= 0) ? PVID_ON : PVID_OFF,
                   txmode_pvid, txmode_prio, txmode_cfi);

    TEST_STEP("Set Tx PVID with RTE API");
    pvid = test_get_pvid(api_pvid, api_prio, api_cfi);

    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_set_vlan_pvid(iut_rpcs, ethdev_config.port_id, pvid,
                                       (api_pvid >= 0) ? PVID_ON : PVID_OFF);

    if (rc == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
    {
        if (txmode_pvid >= 0)
        {
            RING_VERDICT("Runtime Tx PVID set is not supported");
            TEST_SUCCESS;
        }
        else
        {
           TEST_SKIP("Runtime Tx PVID set is not supported");
        }
    }
    else if (rc != 0)
        TEST_VERDICT("Failed to set Tx PVID: %r", rc);

    TEST_STEP("Check Tx PVID configuration");
    test_vlan_pvid(iut_rpcs, iut_port, tst_if, tst_host, mp, tmpl,
                   (api_pvid >= 0) ? PVID_ON : PVID_OFF,
                   api_pvid, api_prio, api_cfi);

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
