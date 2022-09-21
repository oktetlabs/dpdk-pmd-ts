/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-xstats_dev_state Read xstats in different device states
 * @ingroup usecases
 * @{
 *
 * @objective Examine xstats values in different device state
 *
 * @param ethdev_state              The state of Ethernet device
 * @param payload_len               Payload length
 * @param template                  Traffic template
 * @param traffic_direction_rx      Traffic direction
 *
 * @type use case
 *
 * @author Ivan Ilchenko <Ivan.Ilchenko@oktetlabs.ru>
 *
 * The test sets device to the given state and reads per-queue packets xstat.
 * If the state is STARTED, the test starts receiving/sending traffic and
 * examine how corresponding xstats change.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/xstats_dev_state"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

#define TEST_TXQ                0
#define TEST_PACKETS_NUM        1
#define TEST_INVALID_XSTAT      UINT64_MAX

enum traffic_direction {
    RX_TYPE,
    TX_TYPE
};

static uint64_t
find_xstat_id_by_name(int nb_xstats_all,
                      struct tarpc_rte_eth_xstat_name *xstat_names_all,
                      const char *xstat)
{
    int i;

    for (i = 0; i < nb_xstats_all; i++)
    {
        if (strcmp(xstat_names_all[i].name, xstat) == 0)
            return i;
    }

    return TEST_INVALID_XSTAT;
}

static uint64_t
find_xstat_id(int nb_xstats_all,
              struct tarpc_rte_eth_xstat_name *xstat_names_all, int type)
{
    uint64_t xstat_id;
    char *rx_xstats[] = {"rx_q0_packets", "rx_good_packets"};
    char *tx_xstats[] = {"tx_q0_packets", "tx_good_packets"};
    char **xstat;

    switch (type)
    {
        case RX_TYPE:
            xstat = rx_xstats;
            break;
        case TX_TYPE:
            xstat = tx_xstats;
            break;
        default:
            TEST_VERDICT("%s: Wrong type (%d) passed", __FUNCTION__, type);
    }

    xstat_id = find_xstat_id_by_name(nb_xstats_all, xstat_names_all, xstat[0]);
    if (xstat_id == TEST_INVALID_XSTAT)
    {
        RING("%s is not found: fallback to %s", xstat[0], xstat[1]);
        xstat_id = find_xstat_id_by_name(nb_xstats_all, xstat_names_all,
                                         xstat[1]);
        if (xstat_id == TEST_INVALID_XSTAT)
            TEST_VERDICT("Neither %s or %s are found", xstat[0], xstat[1]);
    }

    return xstat_id;
}

static void
check_xstat_init_value(rcf_rpc_server *rpcs, uint16_t port_id, uint64_t id)
{
    uint64_t value;
    int ret;

    ret = rpc_rte_eth_xstats_get_by_id(rpcs, port_id, &id, &value, 1);
    if (ret < 0)
        TEST_VERDICT("rte_eth_xstats_get_by_id() failed (-%s)",
                     errno_rpc2str(-ret));
    if (ret != 1)
    {
        ERROR("Bad number of all xstat values obtained: %d; must be 1", ret);
        TEST_VERDICT("Number of xstats filled in does not match requested");
    }

    if (value != 0)
        TEST_VERDICT("Packets xstat value is %"PRIu64", expected 0", value);
}

static void
get_xstat_nb_names(rcf_rpc_server *rpcs, uint16_t port_id,
                   struct tarpc_rte_eth_xstat_name **names, int *nb)
{
    int ret;

    *nb = rpc_rte_eth_xstats_get_names(rpcs, port_id, NULL, 0);
    if (*nb == 0)
        TEST_SKIP("Zero number of xstats is reported");
    if (*nb < 0)
        TEST_VERDICT("rte_eth_xstats_get_names() failed (-%s)",
                     errno_rpc2str(-*nb));

    *names = TE_ALLOC(*nb * sizeof(**names));
    CHECK_NOT_NULL(*names);
    ret = rpc_rte_eth_xstats_get_names(rpcs, port_id, *names, *nb);
    if (ret < 0)
        TEST_VERDICT("rte_eth_xstats_get_names() failed (-%s)",
                     errno_rpc2str(-ret));
    if (ret != *nb)
    {
        ERROR("Bad number of all xstat names obtained: %d; must be %d",
              ret, *nb);
        TEST_VERDICT("Number of xstat names does not match total number of xstats");
    }
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server                     *iut_rpcs = NULL;
    const struct if_nameindex          *iut_port = NULL;
    tapi_env_host                      *tst_host = NULL;
    const struct if_nameindex          *tst_if = NULL;
    asn_value                          *template  = NULL;
    te_bool                             traffic_direction_rx;
    test_ethdev_state                   ethdev_state;
    unsigned int                        payload_len;
    struct test_ethdev_config           ethdev_config;
    struct tarpc_rte_eth_xstat_name    *xstat_names_all = NULL;
    struct tarpc_rte_eth_xstat_name    *xstat_names_all_in_stopped = NULL;
    int                                 nb_xstats_all;
    int                                 nb_xstats_all_in_stopped;
    uint64_t                            xstat_id;
    uint64_t                            xstat_value;
    uint64_t                            xstat_value_in_stopped;
    te_bool                             fail_test = FALSE;
    int                                 ret;
    int                                 i;

    TEST_START;
    TEST_GET_ETHDEV_STATE(ethdev_state);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(template);
    TEST_GET_BOOL_PARAM(traffic_direction_rx);
    TEST_GET_UINT_PARAM(payload_len);
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);

    TEST_STEP("Prepare IUT ethdev port @p iut_port in requested state");
    (void)test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);
    if (ethdev_state == TEST_ETHDEV_STOPPED)
        CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));
    else
        CHECK_RC(test_prepare_ethdev(&ethdev_config, ethdev_state));

    TEST_STEP("Obtain number of all xstats and their names");
    get_xstat_nb_names(iut_rpcs, iut_port->if_index, &xstat_names_all,
                       &nb_xstats_all);

    if (ethdev_state == TEST_ETHDEV_STOPPED)
    {
        TEST_STEP("Check consistency of xstats number and names reporting"
                  " in stopped state");
        CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STOPPED));
        get_xstat_nb_names(iut_rpcs, iut_port->if_index,
                           &xstat_names_all_in_stopped,
                           &nb_xstats_all_in_stopped);
        if (nb_xstats_all != nb_xstats_all_in_stopped)
        {
            ERROR("Reported xstats number in stopped state is %d"
                  " VS %d in started", nb_xstats_all_in_stopped, nb_xstats_all);
            TEST_VERDICT("Different number of xstats in started and stopped states");
        }

        for (i = 0; i < nb_xstats_all; i++)
        {
            if (strcmp(xstat_names_all[i].name,
                       xstat_names_all_in_stopped[i].name) != 0)
            {
                ERROR("%u xstat differ in started in stopped: %s VS %s", i,
                   xstat_names_all[i].name, xstat_names_all_in_stopped[i].name);
                fail_test = TRUE;
            }
        }

        if (fail_test)
            TEST_VERDICT("Different xstats names in started and stopped states");
        CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));
    }


    TEST_STEP("Looking for xstat ID");
    xstat_id = find_xstat_id(nb_xstats_all, xstat_names_all,
                             traffic_direction_rx ? RX_TYPE : TX_TYPE);

    TEST_STEP("Check initial value of xstat");
    check_xstat_init_value(iut_rpcs, iut_port->if_index, xstat_id);

    if (ethdev_state == TEST_ETHDEV_STARTED ||
        ethdev_state == TEST_ETHDEV_STOPPED)
    {
        struct test_default_tmpl_prepare prep = {
            .rpcs = iut_rpcs,
            .port_id = iut_port->if_index,
            .params = &test_params,
            .mac_name = TEST_IUT_PORT_MAC_NAME,
            .packets_num = TEST_PACKETS_NUM,
            .env = &env,
            .payload_len = payload_len,
            .template = template
        };
        struct test_transceiver rx;
        struct test_transceiver tx;

        memset(&rx, 0, sizeof(rx));
        memset(&tx, 0, sizeof(rx));

        TEST_STEP("Ensure that interface is UP on Tester side");
        CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                                TEST_LINK_UP_MAX_CHECKS,
                                                TEST_LINK_UP_WAIT_MS, 0));

        if (traffic_direction_rx)
        {
            TEST_STEP("See if sending packet burst changes the xstat");
            rx.type = TEST_TRANSCEIVER_DPDK;
            rx.trsc.dpdk.rpcs = iut_rpcs;
            rx.trsc.dpdk.port_id = iut_port->if_index;
            rx.trsc.dpdk.mp = ethdev_config.mp;

            tx.type = TEST_TRANSCEIVER_NET;
            tx.trsc.net.ta = tst_host->ta;
            tx.trsc.net.if_name = tst_if->if_name;
            tx.trsc.net.rx_csap = CSAP_INVALID_HANDLE;
        }
        else
        {
            TEST_STEP("See if receiving packet burst changes the xstat");
            rx.type = TEST_TRANSCEIVER_NET;
            rx.trsc.net.ta = tst_host->ta;
            rx.trsc.net.if_name = tst_if->if_name;

            tx.type = TEST_TRANSCEIVER_DPDK;
            tx.trsc.dpdk.rpcs = iut_rpcs;
            tx.trsc.dpdk.port_id = iut_port->if_index;
            tx.trsc.dpdk.mp = ethdev_config.mp;
        }

        test_default_template_prepare(&prep);
        CHECK_RC(tapi_eth_based_csap_create_by_tmpl(tst_host->ta, 0,
                                                    tst_if->if_name,
                                                    TAD_ETH_RECV_DEF,
                                                    template,
                                                    &rx.trsc.net.rx_csap));

        test_transciever_simple_exchange_commit(template, &tx,
                                                TEST_PACKETS_NUM, 0, &rx,
                                                TEST_PACKETS_NUM, 0, NULL,
                                                NULL);

        test_wait_stats_update();
        ret = rpc_rte_eth_xstats_get_by_id(iut_rpcs, iut_port->if_index,
                                           &xstat_id, &xstat_value, 1);
        if (ret < 0)
            TEST_VERDICT("rte_eth_xstats_get_by_id() failed (-%s)",
                         errno_rpc2str(-ret));
        if (ret != 1)
        {
            ERROR("Bad number of all xstat values obtained: %d; must be 1", ret);
            TEST_VERDICT("Number of xstats filled in does not match requested");
        }

        if (xstat_value != TEST_PACKETS_NUM)
            TEST_VERDICT("Packets xstat value is %"PRIu64", expected %d",
                         xstat_value, TEST_PACKETS_NUM);

    }

    if (ethdev_state == TEST_ETHDEV_STOPPED)
    {
        TEST_STEP("Check xstat value is not changed in stopped state");
        CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STOPPED));

        ret = rpc_rte_eth_xstats_get_by_id(iut_rpcs, iut_port->if_index,
                                         &xstat_id, &xstat_value_in_stopped, 1);
        if (ret < 0)
            TEST_VERDICT("rte_eth_xstats_get_by_id() failed (-%s)",
                         errno_rpc2str(-ret));
        if (ret != 1)
        {
            ERROR("Bad number of all xstat values obtained: %d; must be 1", ret);
            TEST_VERDICT("Number of xstats filled in does not match requested");
        }

        if (xstat_value != xstat_value_in_stopped)
        {
            ERROR("Reported xstat value in stopped state is %"PRIu64
                  " VS %"PRIu64" in started", xstat_value_in_stopped,
                  xstat_value);
            TEST_VERDICT("Different xstat value in started and stopped states");
        }
    }

    TEST_SUCCESS;

cleanup:
    free(xstat_names_all);
    free(xstat_names_all_in_stopped);
    TEST_END;
}
