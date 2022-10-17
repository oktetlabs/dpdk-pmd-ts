/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-rss_reta_update Update the Redirection Table of RSS test
 * @ingroup usecases
 * @{
 *
 * @objective Update the Redirection Table of RSS
 *
 * @param tmpl            The template of packet
 * @param nb_rx_queues    The number of RX queues
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Make sure that it is possible to update the RSS RETA.
 * Besides, check that after the port restarting, the information about RSS
 * RETA doesn't change.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/rss_reta_update"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                        *iut_rpcs  = NULL;
    rcf_rpc_server                        *tst_rpcs  = NULL;
    tapi_env_host                         *tst_host  = NULL;
    const struct if_nameindex             *iut_port  = NULL;
    const struct if_nameindex             *tst_if    = NULL;
    const struct sockaddr                 *tst_addr  = NULL;
    const struct sockaddr                 *iut_addr  = NULL;
    asn_value                             *tmpl      = NULL;
    asn_value                             *ptrn      = NULL;

    struct test_ethdev_config              test_ethdev_config;
    struct tarpc_rte_eth_conf              eth_conf;
    uint64_t                               reta_size;
    struct tarpc_rte_eth_rss_reta_entry64 *reta_conf_temp;
    struct tarpc_rte_eth_rss_reta_entry64 *reta_conf;
    tarpc_rss_hash_protos_t                hash_functions;
    rpc_rte_mbuf_p                         mbufs[BURST_SIZE] = {};
    unsigned int                           nb_rx_queues;

    int                                    reta_nb;
    int                                    reta_indx;
    const struct tarpc_rte_eth_rss_conf   *rss_conf;
    uint32_t                               packet_hash;

    int                                    expected_queue;
    unsigned int                           i, j;
    te_bool                                reta_query_supported;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_UINT_PARAM(nb_rx_queues);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_STEP("Initialise the port in order to obtain RSS capabilities");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &test_ethdev_config);
    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Check if required number of Rx queues is supported");
    if (nb_rx_queues > test_ethdev_config.dev_info.max_rx_queues)
    {
        TEST_SKIP("So many Rx queues are not supported");
    }

    TEST_STEP("Setup RX configuration to work in RSS mode");
    test_ethdev_config.eth_conf = test_rpc_rte_eth_make_eth_conf(
                                      iut_rpcs, iut_port->if_index, &eth_conf);
    test_ethdev_config.nb_rx_queue = nb_rx_queues;

    TEST_STEP("Prepare desired RSS hash configuration");
    CHECK_RC(test_get_rss_hf_by_tmpl(tmpl, &hash_functions));
    hash_functions &= test_ethdev_config.dev_info.flow_type_rss_offloads;
    test_rx_mq_rss_prepare(&test_ethdev_config, hash_functions);

    TEST_STEP("Start the Ethernet device");
    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Generate a new RETA randomly");
    reta_size = test_ethdev_config.dev_info.reta_size;
    reta_conf = tapi_calloc(TE_DIV_ROUND_UP(reta_size, RPC_RTE_RETA_GROUP_SIZE),
                            sizeof(*reta_conf));

    for (i = 0; i < TE_DIV_ROUND_UP(reta_size, RPC_RTE_RETA_GROUP_SIZE); i++)
    {
        for (j = 0; j < RPC_RTE_RETA_GROUP_SIZE; j++)
            reta_conf[i].reta[j] = rand_range(0, nb_rx_queues - 1);

        reta_conf[i].mask = ~0;
    }

    /* Update the RETA */
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_rss_reta_update(iut_rpcs, iut_port->if_index,
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

    TEST_STEP("Query the RETA and check that the new RETA was successfully "
              "updated. Skip this step if RETA query operation is not supported");
    reta_conf_temp = tapi_calloc(TE_DIV_ROUND_UP(reta_size,
                                                 RPC_RTE_RETA_GROUP_SIZE),
                                 sizeof(*reta_conf_temp));

    for (i = 0; i < TE_DIV_ROUND_UP(reta_size, RPC_RTE_RETA_GROUP_SIZE); i++)
        reta_conf_temp[i].mask = ~0;

    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_rss_reta_query(iut_rpcs, iut_port->if_index,
                                        reta_conf_temp, reta_size);
    if (rc == 0)
    {
        reta_query_supported = TRUE;
        for (i = 0; i < TE_DIV_ROUND_UP(reta_size, RPC_RTE_RETA_GROUP_SIZE);
             i++)
        {
            if (memcmp(reta_conf[i].reta, reta_conf_temp[i].reta,
                       RPC_RTE_RETA_GROUP_SIZE) != 0)
                TEST_VERDICT("Updating of the RETA was failed");
        }
    }
    else if (-rc == TE_RC(TE_RPC, TE_EOPNOTSUPP))
    {
        reta_query_supported = FALSE;
    }
    else
    {
        TEST_VERDICT("RSS RETA query operation failed");
    }

    TEST_STEP("Prepare a packet by @p tmpl to check the RETA updating "
              "by sending the packet");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));

    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));

    TEST_STEP("Establish effective RSS hash configuration");
    rss_conf = test_rx_mq_rss_establish(&test_ethdev_config, FALSE);

    TEST_STEP("Calculate the packet hash using the Toeplitz function");
    CHECK_RC(test_calc_hash_by_tmpl_and_hf(
                rss_conf->rss_hf, rss_conf->rss_key.rss_key_val,
                rss_conf->rss_key_len, tmpl, &packet_hash, NULL));

    TEST_STEP("Determine the queue index by means of the hash");
    reta_nb = (packet_hash % reta_size) / RPC_RTE_RETA_GROUP_SIZE;
    reta_indx = (packet_hash % reta_size) % RPC_RTE_RETA_GROUP_SIZE;
    expected_queue = reta_conf[reta_nb].reta[reta_indx];

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Transmit the packet from @p tst_if");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, tmpl, NULL, &ptrn));

    TEST_STEP("Try to receive the packet on @p expected_queue "
              "Make sure that the packet received matches the packet sent");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index,
                                         expected_queue, mbufs,
                                         TE_ARRAY_LEN(mbufs), 1, ptrn, TRUE));

    rpc_rte_pktmbuf_free(iut_rpcs, mbufs[0]);

    TEST_STEP("Restart the port @p uit_port");
    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_STOPPED));
    CHECK_RC(test_prepare_ethdev(&test_ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Get the current configuration of RSS RETA "
              "on port @p iut_port. Check that retrieved information "
              "matches information from @p reta_conf "
              "Skip this step if RETA query operation is not supported");
    if (reta_query_supported)
    {
        rpc_rte_eth_dev_rss_reta_query(iut_rpcs, iut_port->if_index,
                                       reta_conf_temp, reta_size);

        for (i = 0; i < TE_DIV_ROUND_UP(reta_size, RPC_RTE_RETA_GROUP_SIZE);
             i++)
        {
            if (memcmp(reta_conf[i].reta, reta_conf_temp[i].reta,
                       RPC_RTE_RETA_GROUP_SIZE) != 0)
                TEST_VERDICT("The RETA was changed after the port restarting");
        }
    }

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
