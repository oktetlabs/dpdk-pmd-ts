/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2024 OKTET Labs Ltd. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-vlan_filter VLAN filter offload
 * @ingroup usecases
 * @{
 *
 * @objective Check VLAN filter offload
 *
 * @param tmpl              Traffic template
 * @param nb_vlan_filters   The number of VLAN IDs for filtering
 *
 * @type use case
 *
 * @author Daniil Byshenko <daniil.byshenko@oktetlabs.ru>
 *
 * Check the correctness of VLAN filtering
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/vlan_filter"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

#define VLAN_FILTER_OFFLOAD (1ULL << TARPC_RTE_ETH_RX_OFFLOAD_VLAN_FILTER_BIT)
#define VLAN_RX_OFF 0
#define VLAN_RX_ON 1
#define NB_VLAN_FILTERS_MAX 64
#define NB_PKTS 1

static void
vlan_filter_set(rcf_rpc_server *iut_rpcs, const struct if_nameindex *iut_port,
                unsigned int nb_vlan_filters, int vlan_rx_on)
{
    unsigned int vid;

    for (vid = 1; vid <= nb_vlan_filters; vid++)
        CHECK_RC(rpc_rte_eth_dev_vlan_filter(iut_rpcs, iut_port->if_index, vid,
                                             vlan_rx_on));
}

static asn_value *
get_eth_tagged(const asn_value *tmpl)
{
    asn_value *pdus;
    asn_value *choice;
    asn_value *eth;
    asn_value *tagged;
    te_errno rc;

    pdus = asn_find_descendant(tmpl, &rc, "pdus");
    if (pdus == NULL)
        choice = asn_find_descendant(tmpl, &rc, "");
    else
        choice = asn_find_descendant(pdus, &rc, "");

    eth = asn_find_child_choice_value(choice, TE_PROTO_ETH);
    CHECK_NOT_NULL(eth);

    tagged = asn_find_descendant(eth, &rc, "tagged.#tagged");
    CHECK_NOT_NULL(tagged);

    return tagged;
}

static void
test_vlan_filter(rcf_rpc_server *iut_rpcs, const struct if_nameindex *iut_port,
                 const struct if_nameindex *tst_if, tapi_env_host *tst_host,
                 asn_value *tmpl, asn_value *ptrn,
                 unsigned int nb_vlan_filters, te_bool vlan_rx_on)
{
    unsigned int vid;
    asn_value *tmpl_copy;
    rpc_rte_mbuf_p mbufs[BURST_SIZE] = {};
    unsigned int recv;
    unsigned int nb_matched;

    /** Send 1 to 3 packets to test VLAN filter */
    for (vid = 1; vid <= nb_vlan_filters; vid += (nb_vlan_filters / 3) + 1) {
        tmpl_copy = asn_copy_value(tmpl);
        CHECK_RC(asn_write_uint32(get_eth_tagged(tmpl_copy), vid,
                                  "vlan-id.#plain"));

        TEST_SUBSTEP("Transmit and sniff a match packet from @p tst_if");
        CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                                    tst_if->if_name, tmpl_copy,
                                                    NULL, &ptrn));

        TEST_SUBSTEP("Receive a packet on @p iut_port");
        recv = test_rx_burst_with_retries(iut_rpcs, iut_port->if_index, 0,
                                          mbufs, TE_ARRAY_LEN(mbufs), NB_PKTS);


        if (vlan_rx_on)
        {
            CHECK_PACKETS_NUM(recv, NB_PKTS);

            TEST_STEP("Check received packets");
            rpc_rte_mbuf_match_pattern(iut_rpcs, ptrn, mbufs, recv,
                                       NULL, &nb_matched);
        }
        else
            CHECK_PACKETS_NUM(recv, 0);

        rpc_rte_pktmbuf_free_array(iut_rpcs, mbufs, recv);
    }
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server             *iut_rpcs = NULL;
    tapi_env_host              *tst_host = NULL;
    const struct if_nameindex  *iut_port = NULL;
    const struct if_nameindex  *tst_if   = NULL;

    asn_value                  *tmpl = NULL;
    asn_value                  *ptrn = NULL;

    struct test_ethdev_config   ethdev_config;
    struct tarpc_rte_eth_conf   eth_conf;
    unsigned int                nb_vlan_filters;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_UINT_PARAM(nb_vlan_filters);

    if (nb_vlan_filters > NB_VLAN_FILTERS_MAX)
        TEST_VERDICT("Invalid number of VLAN IDs for filtering");

    TEST_STEP("Initialize EAL");
    (void)test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_INITIALIZED));

    ethdev_config.eth_conf = test_rpc_rte_eth_make_eth_conf(ethdev_config.rpcs,
                                            ethdev_config.port_id, &eth_conf);

    TEST_STEP("Check VLAN filter capability");
    if ((ethdev_config.dev_info.rx_offload_capa & VLAN_FILTER_OFFLOAD) == 0)
        TEST_SKIP("VLAN filtering is not supported");

    eth_conf.rxmode.offloads = VLAN_FILTER_OFFLOAD;

    TEST_STEP("Prepare @p tmpl for test");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));

    TEST_STEP("Start the Ethernet device with VLAN filtering offload enabled");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Send VLAN tagged packets just after device start "
              "when all VLANs are filtered out");
    test_vlan_filter(iut_rpcs, iut_port, tst_if, tst_host,
                     tmpl, ptrn, nb_vlan_filters, VLAN_RX_OFF);

    TEST_STEP("Enable tested VLANs Rx");
    vlan_filter_set(iut_rpcs, iut_port, nb_vlan_filters, VLAN_RX_ON);

    TEST_STEP("Send VLAN tagged packets and expect it to be received now");
    test_vlan_filter(iut_rpcs, iut_port, tst_if, tst_host,
                     tmpl, ptrn, nb_vlan_filters, VLAN_RX_ON);

    TEST_STEP("Disable tested VLANs Rx again");
    vlan_filter_set(iut_rpcs, iut_port, nb_vlan_filters, VLAN_RX_OFF);

    TEST_STEP("Send VLAN tagged packets and expect it to be filtered out");
    test_vlan_filter(iut_rpcs, iut_port, tst_if, tst_host,
                     tmpl, ptrn, nb_vlan_filters, VLAN_RX_OFF);

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
