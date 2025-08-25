/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2024 OKTET Labs Ltd. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-stats_reset Test of statistics reset
 * @ingroup usecases
 * @{
 *
 * @objective Check the correctness of statistics reset
 *
 * @param tmpl          Traffic template
 * @param nb_pkts       The number of packets
 * @param payload_len   Payload length
 *
 * @type use case
 *
 * @author Daniil Byshenko <daniil.byshenko@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/stats_reset"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

#include "tapi_rpc_rte_ethdev.h"

static bool
all_stats_are_zero(const struct tarpc_rte_eth_stats *stats)
{
    struct tarpc_rte_eth_stats zero_stats;

    memset(&zero_stats, 0, sizeof(zero_stats));
    if (memcmp(stats, &zero_stats, sizeof(zero_stats)) != 0)
        return false;

    return true;
}

static bool
all_xstats_are_zero(int nb_xstats,
                    const struct tarpc_rte_eth_xstat xstats[static nb_xstats])
{
    int i;

    for (i = 0; i < nb_xstats; i++)
    {
        if (xstats[i].value != 0)
            return false;
    }

    return true;
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server             *iut_rpcs = NULL;
    tapi_env_host              *tst_host;
    const struct if_nameindex  *iut_port = NULL;
    const struct if_nameindex  *tst_if = NULL;

    asn_value                  *tmpl;
    rpc_rte_mbuf_p              mbufs[BURST_SIZE] = {};
    asn_value                  *ptrn;
    int                         nb_xstats;

    struct test_ethdev_config   ethdev_config;
    struct tarpc_rte_eth_xstat *xstats;
    struct tarpc_rte_eth_stats  stats;
    unsigned                    nb_pkts;
    unsigned                    payload_len;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_UINT_PARAM(nb_pkts);
    TEST_GET_UINT_PARAM(payload_len);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);

    TEST_STEP("Initialize EAL");
    (void)test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);
    ethdev_config.min_rx_desc = nb_pkts + 1;
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Start the Ethernet device");
    ethdev_config.required_mtu = payload_len;
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Prepare @p tmpl for test");
    tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                         &test_params, TEST_IUT_PORT_MAC_NAME);

    CHECK_RC(asn_write_int32(tmpl, 1, "arg-sets.0.#simple-for.begin"));
    CHECK_RC(asn_write_int32(tmpl, nb_pkts, "arg-sets.0.#simple-for.end"));
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  payload_len));

    TEST_STEP("Change @p tst_if MTU and @p iut_port MTU to be able to transmit "
              "or receive bigger frames if required");
    CHECK_RC(tapi_cfg_base_if_set_mtu_leastwise(tst_host->ta, tst_if->if_name,
                                                payload_len));

    TEST_STEP("If running in \"speed-up\" mode, await stats reappearance "
              "holding numbers valid for the previous (test) iteration "
              "in order to pick those numbers (if any) as initial ones "
              "(instead of getting zeroes) to calculate the difference");
    if (dpdk_reuse_rpcs())
        MSLEEP(TEST_STATS_UPDATE_WAIT_MS);

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Transmit and sniff @p nb_pkts packets from @p tst_if");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, tmpl, NULL, &ptrn));

    TEST_STEP("Receive packets on @p iut_port and check that they match the sent ones");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, 0,
                                         mbufs, TE_ARRAY_LEN(mbufs),
                                         nb_pkts, ptrn, TRUE));

    TEST_STEP("Wait statistics update");
    MSLEEP(TEST_STATS_UPDATE_WAIT_MS);

    TEST_STEP("Check that general statistics are correct");
    rpc_rte_eth_stats_get(iut_rpcs, iut_port->if_index, &stats);

    if (all_stats_are_zero(&stats))
        TEST_VERDICT("All general statistics are zero");

    TEST_STEP("Check that extended statistics are correct");
    nb_xstats = rpc_rte_eth_xstats_get_names(iut_rpcs, iut_port->if_index,
                                             NULL, 0);

    xstats = tapi_malloc(sizeof(xstats[0]) * nb_xstats);
    rpc_rte_eth_xstats_get(iut_rpcs, iut_port->if_index, xstats, nb_xstats);

    if (all_xstats_are_zero(nb_xstats, xstats))
        TEST_VERDICT("All extended statistics are zero");

    TEST_STEP("Reset general stats statistics");
    rpc_rte_eth_stats_reset(iut_rpcs, iut_port->if_index);

    TEST_STEP("Wait statistics update after reset");
    MSLEEP(TEST_STATS_UPDATE_WAIT_MS);

    TEST_STEP("Check that general statistics are correct");
    rpc_rte_eth_stats_get(iut_rpcs, iut_port->if_index, &stats);

    if (!all_stats_are_zero(&stats))
        TEST_VERDICT("Non-zero general statistic after reset");

    TEST_STEP("Reset extended stats statistics");
    rpc_rte_eth_xstats_reset(iut_rpcs, iut_port->if_index);

    TEST_STEP("Check that extended statistics are correct");
    rpc_rte_eth_xstats_get(iut_rpcs, iut_port->if_index, xstats, nb_xstats);

    if (!all_xstats_are_zero(nb_xstats, xstats))
        TEST_VERDICT("Non-zero extended statistics after reset");

    TEST_SUCCESS;

cleanup:

    TEST_END;
}
/** @} */
