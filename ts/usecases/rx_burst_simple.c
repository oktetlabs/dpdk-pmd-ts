/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-rx_burst_simple RX burst simple test
 * @ingroup usecases
 * @{
 *
 * @objective Receive a burst of packets
 *
 * @param tmpl         Traffic template
 * @param nb_pkts      The number of packets
 * @param payload_len  Payload length
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 *
 * Make sure the RX queue able to successfully receive some burst of packets
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/rx_burst_simple"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server             *iut_rpcs = NULL;
    rcf_rpc_server             *tst_rpcs = NULL;
    tapi_env_host              *tst_host = NULL;
    const struct if_nameindex  *iut_port = NULL;
    const struct if_nameindex  *tst_if   = NULL;

    asn_value                  *tmpl = NULL;
    asn_value                  *ptrn = NULL;
    rpc_rte_mbuf_p              mbufs[BURST_SIZE] = {};

    uint16_t                    received = 0;
    struct test_ethdev_config   ethdev_config;

    unsigned int                payload_len;
    unsigned int                nb_pkts;
    unsigned int                nb_matched = 0;
    unsigned int                i;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_UINT_PARAM(nb_pkts);
    TEST_GET_UINT_PARAM(payload_len);

    TEST_STEP("Initialize EAL");
    (void)test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);
    ethdev_config.min_rx_desc = nb_pkts;
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Prepare @p tmpl for test");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));

    CHECK_RC(asn_write_int32(tmpl, 1, "arg-sets.0.#simple-for.begin"));
    CHECK_RC(asn_write_int32(tmpl, nb_pkts, "arg-sets.0.#simple-for.end"));
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  payload_len));

    TEST_STEP("Start the Ethernet device");
    ethdev_config.required_mtu = payload_len;
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Configure UDP tunnel port number if need be");
    CHECK_RC(test_add_tunnel_udp_port_from_tmpl(&ethdev_config, tmpl, TRUE));

    TEST_STEP("Change @p tst_if MTU and @p iut_port MTU to be able to transmit "
              "or receive bigger frames if required");
    CHECK_RC(tapi_cfg_base_if_set_mtu_leastwise(tst_host->ta, tst_if->if_name,
                                                payload_len));

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Transmit and sniff @p nb_pkts packets from @p tst_if");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                        tst_if->if_name, tmpl, NULL, &ptrn));

    TEST_STEP("Receive packets on @p iut_port");
    received = test_rx_burst_with_retries(iut_rpcs, iut_port->if_index, 0,
                                          mbufs, TE_ARRAY_LEN(mbufs), nb_pkts);

    if (received > 0)
    {
        TEST_STEP("Check received packets");
        rpc_rte_mbuf_match_pattern(iut_rpcs, ptrn, mbufs, received,
                                   NULL, &nb_matched);
    }

    TEST_STEP("Check the number of received and match packets");
    CHECK_PACKETS_NUM(received, nb_pkts);
    CHECK_MATCHED_PACKETS_NUM(nb_matched, nb_pkts);

    TEST_SUCCESS;

cleanup:
    for (i = 0; i < received; i++)
        rpc_rte_pktmbuf_free(iut_rpcs, mbufs[i]);

    TEST_END;
}
/** @} */
