/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-xstats_by_id Check extended statistics retrieval by IDs
 * @ingroup usecases
 * @{
 *
 * @objective Verify that xstat names and values could be retrieved by IDs
 *
 * @param template Traffic template
 *
 * @type use case
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 *
 * The test makes sure that extended statistics retrieval by ID is correct
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "usecases/xstats_by_id"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server                  *iut_rpcs = NULL;
    rcf_rpc_server                  *tst_rpcs = NULL;
    tapi_env_host                   *tst_host = NULL;
    const struct if_nameindex       *iut_port = NULL;
    const struct if_nameindex       *tst_if   = NULL;

    asn_value                       *template = NULL;
    asn_value                       *ptrn;
    rpc_rte_mbuf_p                   mbufs[BURST_SIZE] = {};
    struct test_ethdev_config        ethdev_config;
    unsigned int                     nb_pkts = 1 << 1;
    int                              nb_xstats_all;
    struct tarpc_rte_eth_xstat_name *xstat_names_all;
    struct tarpc_rte_eth_xstat      *xstats_all;
    int                              nb_xstats_nz;
    unsigned int                    *xstat_nz_keys;
    uint64_t                        *xstat_nz_ids;
    int                              ret;
    int                              i;
    int                              j;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(template);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs,
                                         iut_port, &ethdev_config,
                                         TEST_ETHDEV_STARTED));

    TEST_STEP("Cook traffic template");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));
    CHECK_RC(asn_write_int32(template, 1, "arg-sets.0.#simple-for.begin"));
    CHECK_RC(asn_write_int32(template, nb_pkts, "arg-sets.0.#simple-for.end"));
    CHECK_RC(tapi_ndn_subst_env(template, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&template, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));

    TEST_STEP("Sent a couple of packets to IUT in order to guarantee "
              "at least a handful of non-zero xstat figures");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                    tst_if->if_name, template, NULL, &ptrn));

    TEST_STEP("Receive packets on @p iut_port and check that they match the sent ones");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, 0,
                                         mbufs, BURST_SIZE, nb_pkts, ptrn,
                                         TRUE));

    MSLEEP(TEST_STATS_UPDATE_WAIT_MS);

    TEST_STEP("Obtain trustworthy xstats for comparison by means of the old API");
    nb_xstats_all = rpc_rte_eth_xstats_get_names(iut_rpcs, iut_port->if_index,
                                                 NULL, 0);
    if (nb_xstats_all == 0)
        TEST_VERDICT("Zero number of xstats reported");

    xstat_names_all = TE_ALLOC(nb_xstats_all * sizeof(*xstat_names_all));
    CHECK_NOT_NULL(xstat_names_all);

    ret = rpc_rte_eth_xstats_get_names(iut_rpcs, iut_port->if_index,
                                       xstat_names_all, nb_xstats_all);
    if (ret != nb_xstats_all)
        TEST_VERDICT("Bad number of all xstat names obtained: %d; must be %d",
                     ret, nb_xstats_all);

    xstats_all = TE_ALLOC(nb_xstats_all * sizeof(*xstats_all));
    CHECK_NOT_NULL(xstats_all);

    ret = rpc_rte_eth_xstats_get(iut_rpcs, iut_port->if_index,
                                 xstats_all, nb_xstats_all);
    if (ret != nb_xstats_all)
        TEST_VERDICT("Bad number of all xstats obtained: %d; must be %d",
                     ret, nb_xstats_all);

    TEST_STEP("Find non-zero xstats, remember their indices and IDs");
    for (i = 0, nb_xstats_nz = 0;
         i < nb_xstats_all;
         nb_xstats_nz += (xstats_all[i++].value != 0));

    if (nb_xstats_nz == 0)
        TEST_VERDICT("All xstats are zero figures");

    xstat_nz_keys = TE_ALLOC(nb_xstats_nz * sizeof(*xstat_nz_keys));
    CHECK_NOT_NULL(xstat_nz_keys);

    xstat_nz_ids = TE_ALLOC(nb_xstats_nz * sizeof(*xstat_nz_ids));
    CHECK_NOT_NULL(xstat_nz_ids);

    for (i = 0, j = 0; i < nb_xstats_all; ++i)
    {
        if (xstats_all[i].value == 0)
            continue;

        xstat_nz_keys[j] = i;
        xstat_nz_ids[j++] = xstats_all[i].id;
    }

    TEST_STEP("Check that correct number of xstat names could be obtained "
              "if neither IDs are specified nor output buffer is provided");
    ret = rpc_rte_eth_xstats_get_names_by_id(iut_rpcs, iut_port->if_index,
                                             NULL, 0, NULL);
    if (ret != nb_xstats_all)
        TEST_VERDICT("Bad number of all xstat names reported: %d; must be %d",
                     ret, nb_xstats_all);

    TEST_STEP("Check that the same list of xstat names could be retrieved "
              "if no exact IDs are specified");
    {
        struct tarpc_rte_eth_xstat_name xstat_names_by_id_all[nb_xstats_all];

        ret = rpc_rte_eth_xstats_get_names_by_id(iut_rpcs, iut_port->if_index,
                                                 xstat_names_by_id_all,
                                                 nb_xstats_all, NULL);
        if (ret != nb_xstats_all)
            TEST_VERDICT("Bad number of all xstat names reported: %d; must be %d",
                         ret, nb_xstats_all);

        for (i = 0; i < nb_xstats_all; ++i)
        {
            char *name_by_id = xstat_names_by_id_all[i].name;

            if (name_by_id[TARPC_RTE_ETH_XSTATS_NAME_SIZE - 1] != '\0')
                TEST_VERDICT("Xstat name (index = %d) is not null-terminated", i);

            if (strcmp(name_by_id, xstat_names_all[i].name))
                TEST_VERDICT("Got bad xstat name (index = %d): '%s'; must be '%s'",
                             i, name_by_id, xstat_names_all[i].name);
        }
    }

    TEST_STEP("Check that xstat names queried by IDs of non-zero xstats "
              "are the same as the corresponding names from the trustworthy list");
    {
        struct tarpc_rte_eth_xstat_name xstat_names_by_id[nb_xstats_nz];

        ret = rpc_rte_eth_xstats_get_names_by_id(iut_rpcs, iut_port->if_index,
                                                 xstat_names_by_id,
                                                 nb_xstats_nz, xstat_nz_ids);
        if (ret != nb_xstats_nz)
           TEST_VERDICT("Bad number of xstat names reported for non-zero "
                        "xstats: %d; must be %d", ret, nb_xstats_nz);

        for (i = 0; i < nb_xstats_nz; ++i)
        {
            /* NB: Check for null-termination is omitted here */
            if (strcmp(xstat_names_by_id[i].name,
                       xstat_names_all[xstat_nz_keys[i]].name) == 0)
                continue;

            TEST_VERDICT("Got bad xstat name (index = %d): '%s'; must be '%s'",
                         i, xstat_names_all[xstat_nz_keys[i]].name,
                         xstat_names_all[i].name);
        }
    }

    TEST_STEP("Check that correct number of xstat values could be obtained "
              "if neither IDs are specified nor output buffer is provided");
    ret = rpc_rte_eth_xstats_get_by_id(iut_rpcs, iut_port->if_index,
                                       NULL, NULL, 0);
    if (ret != nb_xstats_all)
        TEST_VERDICT("Bad number of all xstat values reported: %d; must be %d",
                     ret, nb_xstats_all);

    TEST_STEP("Check that the same list of xstat values could be retrieved "
              "if no exact IDs are specified");
    {
        uint64_t xstat_values_by_id_all[nb_xstats_all];

        ret = rpc_rte_eth_xstats_get_by_id(iut_rpcs, iut_port->if_index,
                                           NULL, xstat_values_by_id_all,
                                           nb_xstats_all);
        if (ret != nb_xstats_all)
            TEST_VERDICT("Bad number of all xstat values reported: %d; must be %d",
                         ret, nb_xstats_all);

        for (i = 0; i < nb_xstats_all; ++i)
        {
            if (xstat_values_by_id_all[i] == xstats_all[i].value)
                continue;

            TEST_VERDICT("Bad value of '%s': %" PRIu64 "; must be %" PRIu64,
                         xstat_names_all[i].name, xstat_values_by_id_all[i],
                         xstats_all[i].value);
        }
    }

    TEST_STEP("Check that xstat values queried by IDs of non-zero xstats "
              "are the same as the corresponding values from the trustworthy list");
    {
        uint64_t xstat_values_by_id[nb_xstats_nz];

        ret = rpc_rte_eth_xstats_get_by_id(iut_rpcs, iut_port->if_index,
                                           xstat_nz_ids, xstat_values_by_id,
                                           nb_xstats_nz);
        if (ret != nb_xstats_nz)
           TEST_VERDICT("Bad number of xstat values reported for non-zero "
                        "xstats: %d; must be %d", ret, nb_xstats_nz);

        for (i = 0; i < nb_xstats_nz; ++i)
        {
            if (xstat_values_by_id[i] == xstats_all[xstat_nz_keys[i]].value)
                continue;

            TEST_VERDICT("Bad value of '%s': %" PRIu64 "; must be %" PRIu64,
                         xstat_names_all[xstat_nz_keys[i]].name,
                         xstat_values_by_id[i],
                         xstats_all[xstat_nz_keys[i]].value);
        }
    }

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
