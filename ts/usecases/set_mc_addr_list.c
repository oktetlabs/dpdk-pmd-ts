/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-set_mc_addr_list Set the list of multicast addresses to filter on @p iut_port port
 * @ingroup usecases
 * @{
 *
 * @objective Set the list of multicast addresses to filter on
 *            @p iut_port port
 *
 * @param tmpl              Traffic template
 * @param add_mc_addr_list  List of multicast MAC addresses to add
 *                          (format: comma-separated list of MACs)
 *
 * @type use case
 *
 * @author Roman Zhukov <Roman.Zhukov@oktetlabs.ru>
 *
 * Set the list of multicast addresses to filter on @p iut_port port and
 * send a multicast packets to check the work of whitelist filtering
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/set_mc_addr_list"

#include "dpdk_pmd_test.h"

static asn_value **
get_tmpls_with_altered_addrs(const asn_value *tmpl_orig, unsigned int count,
                             uint8_t **addr_list)
{
    asn_value *pdus = NULL;
    asn_value *eth_pdu = NULL;
    asn_value **tmpls = NULL;

    unsigned int i;

    tmpls = tapi_calloc(count, sizeof(tmpl_orig));
    CHECK_NOT_NULL(tmpls);

    for (i = 0; i < count; i++)
    {
        tmpls[i] = asn_copy_value(tmpl_orig);
        CHECK_NOT_NULL(tmpls[i]);

        CHECK_RC(asn_get_subvalue(tmpls[i], &pdus, "pdus"));
        eth_pdu = asn_find_child_choice_value(pdus, TE_PROTO_ETH);
        CHECK_NOT_NULL(eth_pdu);
        CHECK_RC(asn_write_value_field(eth_pdu, addr_list[i],
                                       ETHER_ADDR_LEN, "dst-addr.#plain"));
    }

    return tmpls;
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server                        *iut_rpcs = NULL;
    rcf_rpc_server                        *tst_rpcs = NULL;
    tapi_env_host                         *tst_host;
    const struct if_nameindex             *iut_port = NULL;
    const struct if_nameindex             *tst_if = NULL;

    asn_value                             *tmpl;
    asn_value                            **tmpls = NULL;
    asn_value                            **mismatch_tmpls = NULL;
    rpc_rte_mbuf_p                         mbufs[BURST_SIZE] = {};
    asn_value                             *ptrn;
    asn_value                            **ptrns = NULL;
    asn_value                            **mismatch_ptrns = NULL;
    unsigned int                           i;

    struct test_ethdev_config              ethdev_config;
    test_ethdev_state                      ethdev_state;
    struct tarpc_ether_addr               *mc_addr_set;
    unsigned int                           nb_mc_addr;
    uint8_t                              **mc_addr_list;
    unsigned int                           nb_mismatch_addr;
    uint8_t                              **mismatch_addr_list;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);
    TEST_GET_ETHDEV_STATE(ethdev_state);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_OCTET_STRING_LIST_PARAM(mc_addr_list, nb_mc_addr, ETHER_ADDR_LEN);
    TEST_GET_OCTET_STRING_LIST_PARAM(mismatch_addr_list, nb_mismatch_addr,
                                     ETHER_ADDR_LEN);

    TEST_STEP("Prepare @p ethdev_state Ethernet device state");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port,
                               &ethdev_config);

    CHECK_RC(test_prepare_ethdev(&ethdev_config, ethdev_state));

    TEST_STEP("Add @p mc_addr_list to the multicast whitelist");
    mc_addr_set = tapi_calloc(nb_mc_addr, sizeof(struct tarpc_ether_addr));
    for (i = 0; i < nb_mc_addr; i++)
        mempcpy(mc_addr_set[i].addr_bytes, mc_addr_list[i], ETHER_ADDR_LEN);

    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_set_mc_addr_list(iut_rpcs, iut_port->if_index,
                                          mc_addr_set, nb_mc_addr);
    switch (-rc)
    {
        case 0:
            break;
        case TE_RC(TE_RPC, TE_EOPNOTSUPP):
            TEST_SKIP("Set list of multicast addresses operation is not supported");
            break;
        default:
            TEST_VERDICT("Set list of multicast addresses operation "
                         "failed: %s", errno_rpc2str(-rc));
    }

    TEST_STEP("Start the port");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Disable promiscuous mode and allmulticast mode ");
    test_rte_eth_promiscuous_disable(iut_rpcs, iut_port->if_index,
                                     TEST_OP_REQUIRED);
    test_rte_eth_allmulticast_disable(iut_rpcs, iut_port->if_index,
                                      TEST_OP_REQUIRED);

    TEST_STEP("Prepare array of @p tmpl copies with different dst addr "
              "from @p mc_addr_list");
    tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                         &test_params, TEST_IUT_PORT_MAC_NAME);

    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));

    tmpls = get_tmpls_with_altered_addrs(tmpl, nb_mc_addr, mc_addr_list);

    TEST_STEP("Transmit and sniff different multicast packets from @p tst_if");
    ptrns = tapi_calloc(nb_mc_addr, sizeof(ptrn));
    CHECK_NOT_NULL(ptrns);

    for (i = 0; i < nb_mc_addr; i++)
        CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                tst_if->if_name, tmpls[i], NULL, &ptrns[i]));

    TEST_STEP("Receive packets on @p iut_port and check that the packets received "
              "match the packets sent from @p tst_if");
    CHECK_RC(tapi_tad_aggregate_patterns(ptrns, nb_mc_addr, &ptrn));

    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, 0,
                                         mbufs, TE_ARRAY_LEN(mbufs), nb_mc_addr,
                                         ptrn, TRUE));

    for (i = 0; i < TE_ARRAY_LEN(mbufs); i++)
    {
        rpc_rte_pktmbuf_free(iut_rpcs, mbufs[i]);
        mbufs[i] = RPC_NULL;
    }

    TEST_STEP("Prepare @p tmpl copies with different dst addr from "
              "@p mismatch_addr_list");
    mismatch_tmpls = get_tmpls_with_altered_addrs(tmpl, nb_mismatch_addr,
                                                  mismatch_addr_list);

    TEST_STEP("Transmit and sniff packets with @p mismatch_addr_list dst addr "
              "from @p tst_if");
    mismatch_ptrns = tapi_calloc(nb_mismatch_addr, sizeof(ptrn));
    CHECK_NOT_NULL(mismatch_ptrns);

    for (i = 0; i < nb_mismatch_addr; i++)
    {
        CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                    tst_if->if_name, mismatch_tmpls[i], NULL,
                                    &mismatch_ptrns[i]));
    }

    TEST_STEP("Receive packets on @p iut_port and check that the packets were dropped");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, 0,
                                         mbufs, TE_ARRAY_LEN(mbufs),
                                         0, NULL, TRUE));

    TEST_STEP("Flush the set of filtered addresses");
    rpc_rte_eth_dev_set_mc_addr_list(iut_rpcs, iut_port->if_index,
                                     NULL, 0);

    TEST_STEP("Transmit and sniff packets with @p mc_addr_list dst addr from "
              "@p tst_if");
    for (i = 0; i < nb_mc_addr; i++)
        CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                tst_if->if_name, tmpls[i], NULL, &ptrns[i]));

    TEST_STEP("Receive packets on @p iut_port and check that the packets were dropped");
    CHECK_RC(test_rx_burst_match_pattern(iut_rpcs, iut_port->if_index, 0,
                                         mbufs, TE_ARRAY_LEN(mbufs),
                                         0, NULL, TRUE));

    TEST_SUCCESS;

cleanup:
    for (i = 0; i < TE_ARRAY_LEN(mbufs); i++)
        rpc_rte_pktmbuf_free(iut_rpcs, mbufs[i]);

    TEST_END;
}
/** @} */
