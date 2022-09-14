/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Transmit functionality
 */

/** @defgroup xmit-vlan_on_port_restart Prove that VLAN ID is kept on port restart
 * @ingroup xmit
 * @{
 *
 * @objective Make sure that PMD does not loose previously configured VLAN TCI
 *            if a port restart takes place between two packet bursts with the
 *            same VLAN TCI set in the mbufs before and after the port restart
 *
 * @param template              Traffic template
 * @param vlan_id               VLAN ID or @c -1 (disable)
 * @param deferred_txq_start    Use deferred start for the TxQ
 *
 * @type conformance
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 *
 * The test is to verify that PMD is able to preserve VLAN TCI established once
 * at the beginning of a packet burst in case if the port restart is done after
 * the burst is sent and before the next one is prepared with the same VLAN TCI
 *
 * The task is fulfilled by sending a couple of packets with the same VLAN TCI
 * set in the mbufs: one before and one after restart is performed on the port
 *
 * @par Scenario:
 */

#define TE_TEST_NAME                   "xmit/vlan_on_port_restart"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

#include "tapi_ndn.h"
#include "tapi_tad.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                     *iut_rpcs = NULL;
    tapi_env_host                      *tst_host = NULL;
    const struct if_nameindex          *iut_port = NULL;
    const struct if_nameindex          *tst_if = NULL;

    asn_value                          *template;
    int                                 vlan_id;
    te_bool                             deferred_txq_start = FALSE;

    struct test_ethdev_config           ethdev_config;
    struct tarpc_rte_eth_conf           eth_conf;
    struct tarpc_rte_eth_txconf         txconf;
    unsigned int                        payload_len = DPMD_TS_PAYLOAD_LEN_DEF;
    send_transform                      cond;
    rpc_rte_mbuf_p                     *mbufs = NULL;
    rpc_rte_mbuf_p                     *burst = NULL;
    unsigned int                        n_mbufs = 0;
    asn_value                          *pattern;
    csap_handle_t                       rx_csap = CSAP_INVALID_HANDLE;
    unsigned int                        packets_received;

    TEST_START;

    TEST_GET_PCO(iut_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_GET_NDN_TRAFFIC_TEMPLATE(template);
    TEST_GET_VLAN_ID_PARAM(vlan_id);
    TEST_GET_BOOL_PARAM(deferred_txq_start);

    TEST_STEP("Prepare @c TEST_ETHDEV_CONFIGURED state");
    (void)test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);

    ethdev_config.nb_rx_queue = 1;
    ethdev_config.nb_tx_queue = 1;

    (void)test_rpc_rte_eth_make_eth_conf(iut_rpcs, iut_port->if_index,
                                         &eth_conf);
    ethdev_config.eth_conf = &eth_conf;

    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_CONFIGURED));

    TEST_STEP("Verify the configuration requested");
    if ((vlan_id >= 0) &&
        ((ethdev_config.dev_info.tx_offload_capa &
        (1U << TARPC_RTE_ETH_TX_OFFLOAD_VLAN_INSERT_BIT)) == 0))
        TEST_SKIP("TX VLAN insertion is not available");

    TEST_STEP("Adjust TxQ settings (if need be)");
    ethdev_config.tx_confs = tapi_calloc(1,
                                         sizeof(struct tarpc_rte_eth_txconf *));
    ethdev_config.tx_confs[0] = &txconf;
    memcpy(&txconf, &ethdev_config.dev_info.default_txconf, sizeof(txconf));
    if (deferred_txq_start)
        txconf.tx_deferred_start = 1;

    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Start the TxQ (if need be)");
    if (deferred_txq_start)
        test_start_tx_queue(iut_rpcs, iut_port->if_index, 0);

    TEST_STEP("Obtain the source Ethernet address");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params, "iut_mac"));

    TEST_STEP("Adjust the traffic template");
    CHECK_RC(tapi_ndn_subst_env(template, &test_params, &env));

    asn_write_value_field(template, &payload_len, sizeof(payload_len),
                          "payload.#length");

    (void)asn_free_child(template, PRIVATE, NDN_TMPL_ARGS);
    CHECK_RC(asn_write_int32(template, 1, "arg-sets.0.#simple-for.begin"));
    CHECK_RC(asn_write_int32(template, 2, "arg-sets.0.#simple-for.end"));

    TEST_STEP("Adjust so-called 'send transformations' for the packets");
    memset(&cond, 0, sizeof(cond));

    if (vlan_id >= 0)
    {
        cond.hw_flags |= SEND_COND_HW_OFFL_VLAN;
        cond.vlan_tci = (uint16_t)vlan_id;
    }

    TEST_STEP("Generate mbufs and the corresponding pattern for the packets");
    tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, template, ethdev_config.mp,
                                     &cond, &mbufs, &n_mbufs, &pattern);

    burst = mbufs;

    if (n_mbufs != 2)
        TEST_VERDICT("Wrong number of mbufs made: %u (should be %u)", n_mbufs, 2);

    TEST_STEP("Create an RX CSAP on the TST host according to the template");
    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(tst_host->ta, 0,
                                                tst_if->if_name,
                                                TAD_ETH_RECV_DEF,
                                                template, &rx_csap));

    TEST_STEP("Start to capture traffic with the pattern prepared");
    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, rx_csap, pattern,
                                   TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_PACKETS | RCF_TRRECV_MISMATCH));

    TEST_STEP("Validate and send the first packet");
    if (test_tx_prepare_and_burst(iut_rpcs, iut_port->if_index, 0,
                                  burst++, 1) != 1)
        TEST_VERDICT("Cannot send the packet");

    TEST_STEP("Stop the port, wait for a small amount of time, start the port");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STOPPED));
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Start the TxQ (if need be)");
    if (deferred_txq_start)
        test_start_tx_queue(iut_rpcs, iut_port->if_index, 0);

    TEST_STEP("Validate and send the second packet");
    if (test_tx_prepare_and_burst(iut_rpcs, iut_port->if_index, 0,
                                  burst++, 1) != 1)
        TEST_VERDICT("Cannot send the packet");

    CHECK_RC(test_rx_await_pkts(tst_host->ta, rx_csap, n_mbufs, 0));
    CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL,
                                  &packets_received));

    TEST_STEP("Verify the number of matching packets received");
    CHECK_MATCHED_PACKETS_NUM(packets_received, n_mbufs);

    TEST_SUCCESS;

cleanup:
    while ((mbufs != NULL) && (burst != NULL) && (burst != (mbufs + n_mbufs)))
        rpc_rte_pktmbuf_free(iut_rpcs, *(burst++));

    TEST_END;
}
/** @} */
