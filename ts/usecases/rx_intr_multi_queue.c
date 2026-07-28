/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2026 OKTET Labs. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-rx_intr_multi_queue Test Rx queue interrupts
 * @ingroup usecases
 * @{
 *
 * @objective Check that Rx interrupts can be controlled independently
 *            for different Rx queues
 *
 * @param tmpl Traffic template
 *
 * @type use case
 *
 * @author Denis Pryazhennikov <denis.pryazhennikov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/rx_intr_multi_queue"

#include "dpdk_pmd_test.h"

/** Number of Rx queues required to check independent interrupt control. */
#define RX_INTR_QUEUE_COUNT 2

/** Data stored in an epoll event. */
#define RX_INTR_EVENT_DATA 1

/** Maximum time to wait for an expected interrupt, in milliseconds. */
#define RX_INTR_WAIT_TIMEOUT 5000

/** Time to wait while checking that no interrupt occurs, in milliseconds. */
#define RX_INTR_NO_EVENT_TIMEOUT 1000

static void
reta_target_queue(rcf_rpc_server *iut_rpcs, uint16_t port_id,
                  struct tarpc_rte_eth_rss_reta_entry64 *reta_conf,
                  uint64_t reta_size, uint16_t queue_id)
{
    unsigned int i;
    unsigned int j;
    int rc;

    for (i = 0;
         i < TE_DIV_ROUND_UP(reta_size, RPC_RTE_RETA_GROUP_SIZE);
         i++)
    {
        reta_conf[i].mask = ~0;
        for (j = 0; j < RPC_RTE_RETA_GROUP_SIZE; j++)
            reta_conf[i].reta[j] = queue_id;
    }

    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_rss_reta_update(iut_rpcs, port_id, reta_conf,
                                         reta_size);
    if (-rc == TE_RC(TE_RPC, TE_EOPNOTSUPP))
        TEST_SKIP("RSS redirection table update is not supported");
    if (rc != 0)
        TEST_VERDICT("RSS redirection table update failed: %s",
                     errno_rpc2str(-rc));
}

static void
intr_enable(rcf_rpc_server *iut_rpcs, uint16_t port_id,
            uint16_t queue_id)
{
    int rc;

    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_rx_intr_enable(iut_rpcs, port_id, queue_id);
    if (-rc == TE_RC(TE_RPC, TE_EOPNOTSUPP))
        TEST_SKIP("Rx interrupt enable operation is not supported");
    CHECK_RC(rc);
}

static void
intr_disable(rcf_rpc_server *iut_rpcs, uint16_t port_id,
             uint16_t queue_id)
{
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    CHECK_RC(rpc_rte_eth_dev_rx_intr_disable(iut_rpcs, port_id, queue_id));
}

static asn_value *
send_packet(tapi_env_host *tst_host, const struct if_nameindex *tst_if,
            asn_value *tmpl)
{
    asn_value *ptrn = NULL;

    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(
                 tst_host->ta, 0, tst_if->if_name, tmpl, NULL, &ptrn));

    return ptrn;
}

static void
receive_packet(rcf_rpc_server *iut_rpcs, uint16_t port_id,
               uint16_t queue_id, asn_value *ptrn)
{
    rpc_rte_mbuf_p mbufs[BURST_SIZE] = {};

    CHECK_RC(test_rx_burst_match_pattern(
                 iut_rpcs, port_id, queue_id, mbufs, TE_ARRAY_LEN(mbufs),
                 1, ptrn, TRUE));
    rpc_rte_pktmbuf_free_array(iut_rpcs, mbufs, 1);
    asn_free_value(ptrn);
}

static void
wait_for_interrupt(rcf_rpc_server *iut_rpcs, uint16_t queue_id)
{
    struct tarpc_rte_epoll_event events[RX_INTR_QUEUE_COUNT];
    int events_received;

    events_received = rpc_rte_epoll_wait(
                          iut_rpcs, RPC_RTE_EPOLL_PER_THREAD, events,
                          TE_ARRAY_LEN(events),
                          RX_INTR_WAIT_TIMEOUT);
    if (events_received < 0)
    {
        TEST_VERDICT("Failed to wait for an Rx interrupt from queue %u: %s",
                     queue_id, errno_rpc2str(-events_received));
    }
    if (events_received != 1)
    {
        TEST_VERDICT("Expected one Rx interrupt event from queue %u, got %d",
                     queue_id, events_received);
    }
    if (events[0].epdata.data != RX_INTR_EVENT_DATA)
    {
        TEST_VERDICT("Rx queue %u event contains data %" PRIu64
                     " instead of %u", queue_id, events[0].epdata.data,
                     RX_INTR_EVENT_DATA);
    }
}

static void
check_no_interrupt(rcf_rpc_server *iut_rpcs, uint16_t queue_id)
{
    struct tarpc_rte_epoll_event events[RX_INTR_QUEUE_COUNT];
    int events_received;

    events_received = rpc_rte_epoll_wait(
                          iut_rpcs, RPC_RTE_EPOLL_PER_THREAD, events,
                          TE_ARRAY_LEN(events),
                          RX_INTR_NO_EVENT_TIMEOUT);
    if (events_received < 0)
    {
        TEST_VERDICT("Failed to check for unexpected Rx interrupts: %s",
                     errno_rpc2str(-events_received));
    }
    if (events_received > 0)
    {
        TEST_VERDICT("Received %d Rx interrupt event(s) from disabled "
                     "queue %u", events_received, queue_id);
    }
}

static void
register_interrupts(rcf_rpc_server *iut_rpcs, uint16_t port_id)
{
    unsigned int queue_id;
    int rc;

    for (queue_id = 0; queue_id < RX_INTR_QUEUE_COUNT; queue_id++)
    {
        RPC_AWAIT_IUT_ERROR(iut_rpcs);
        rc = rpc_rte_eth_dev_rx_intr_ctl_q(
                 iut_rpcs, port_id, queue_id,
                 RPC_RTE_EPOLL_PER_THREAD, TARPC_RTE_INTR_EVENT_ADD,
                 RX_INTR_EVENT_DATA);
        if (rc != 0)
        {
            TEST_VERDICT("Cannot bind Rx queue %u interrupt: %s",
                         queue_id, errno_rpc2str(-rc));
        }
    }
}

int
main(int argc, char *argv[])
{
    const struct if_nameindex *iut_port = NULL;
    const struct if_nameindex *tst_if = NULL;
    rcf_rpc_server *iut_rpcs = NULL;
    tapi_env_host *tst_host;

    struct tarpc_rte_eth_rss_reta_entry64 *reta_conf = NULL;
    tarpc_rss_hash_protos_t hash_functions;
    struct tarpc_rte_eth_conf eth_conf;
    struct test_ethdev_config ec;
    uint16_t other_queue_id;
    uint16_t port_id;
    uint64_t reta_size;
    uint16_t queue_id;

    asn_value *tmpl = NULL;
    asn_value *ptrn;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);

    port_id = iut_port->if_index;

    TEST_STEP("Prepare initialized Ethernet device state.");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ec);
    ec.nb_rx_queue = RX_INTR_QUEUE_COUNT;
    ec.eth_conf = test_rpc_rte_eth_make_eth_conf(iut_rpcs, port_id,
                                                 &eth_conf);
    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_INITIALIZED));

    if (ec.dev_info.max_rx_queues < RX_INTR_QUEUE_COUNT)
        TEST_SKIP("Two Rx queues are not supported");

    CHECK_RC(test_get_rss_hf_by_tmpl(tmpl, &hash_functions));
    hash_functions &= ec.dev_info.flow_type_rss_offloads;
    if (hash_functions == 0)
        TEST_SKIP("RSS is required to target Rx queues");

    eth_conf.rxmode.mq_mode = TARPC_ETH_MQ_RX_RSS;
    test_setup_rss_configuration(
        hash_functions, ec.dev_info.hash_key_size, TRUE,
        &eth_conf.rx_adv_conf.rss_conf);
    eth_conf.intr_conf.rxq = 1;

    TEST_STEP("Configure the device with two Rx queues and Rx interrupts.");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_configure(iut_rpcs, port_id, RX_INTR_QUEUE_COUNT,
                                   1, &eth_conf);
    if (-rc == TE_RC(TE_RPC, TE_EINVAL))
        TEST_SKIP("Configuration with Rx interrupts is rejected");
    if (rc != 0)
        TEST_VERDICT("Failed to configure device: %r", -rc);
    ec.cur_state = TEST_ETHDEV_CONFIGURED;

    TEST_STEP("Start the device.");
    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_STARTED));

    reta_size = ec.dev_info.reta_size;
    if (reta_size == 0)
        TEST_SKIP("RSS redirection table is unavailable");
    reta_conf = tapi_calloc(
                    TE_DIV_ROUND_UP(reta_size, RPC_RTE_RETA_GROUP_SIZE),
                    sizeof(*reta_conf));

    TEST_STEP("Ensure that interface is UP on Tester side.");
    CHECK_RC(tapi_cfg_base_if_await_link_up(
                 tst_host->ta, tst_if->if_name, TEST_LINK_UP_MAX_CHECKS,
                 TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Prepare the packet template.");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(
                 iut_rpcs, port_id, &test_params, TEST_IUT_PORT_MAC_NAME));
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(
                 &tmpl, FALSE, NULL, DPMD_TS_PAYLOAD_LEN_DEF));

    TEST_STEP("Register interrupts from both Rx queues.");
    register_interrupts(iut_rpcs, port_id);

    TEST_STEP("Check an Rx interrupt from each queue.");
    for (queue_id = 0; queue_id < RX_INTR_QUEUE_COUNT; queue_id++)
    {
        TEST_SUBSTEP("Enable the interrupt on the target Rx queue.");
        intr_enable(iut_rpcs, port_id, queue_id);

        TEST_SUBSTEP("Send a packet to the target Rx queue.");
        reta_target_queue(iut_rpcs, port_id, reta_conf, reta_size, queue_id);
        ptrn = send_packet(tst_host, tst_if, tmpl);

        TEST_SUBSTEP("Wait for an Rx interrupt.");
        wait_for_interrupt(iut_rpcs, queue_id);

        TEST_SUBSTEP("Disable the interrupt on the target Rx queue.");
        intr_disable(iut_rpcs, port_id, queue_id);

        TEST_SUBSTEP("Receive the packet from the target Rx queue.");
        receive_packet(iut_rpcs, port_id, queue_id, ptrn);
    }

    TEST_STEP("Check independent interrupt control for Rx queues.");
    for (queue_id = 0; queue_id < RX_INTR_QUEUE_COUNT; queue_id++)
    {
        other_queue_id = (queue_id + 1) % RX_INTR_QUEUE_COUNT;

        TEST_SUBSTEP("Enable interrupts on both Rx queues.");
        intr_enable(iut_rpcs, port_id, queue_id);
        intr_enable(iut_rpcs, port_id, other_queue_id);

        TEST_SUBSTEP("Disable the interrupt on the target Rx queue.");
        intr_disable(iut_rpcs, port_id, queue_id);

        TEST_SUBSTEP("Send a packet to the Rx queue with disabled interrupt.");
        reta_target_queue(iut_rpcs, port_id, reta_conf, reta_size, queue_id);
        ptrn = send_packet(tst_host, tst_if, tmpl);

        TEST_SUBSTEP("Check that no Rx interrupt is received.");
        check_no_interrupt(iut_rpcs, queue_id);

        TEST_SUBSTEP("Receive the packet from the Rx queue with disabled "
                     "interrupt.");
        receive_packet(iut_rpcs, port_id, queue_id, ptrn);

        TEST_SUBSTEP("Send a packet to the Rx queue with enabled interrupt.");
        reta_target_queue(iut_rpcs, port_id, reta_conf, reta_size,
                          other_queue_id);
        ptrn = send_packet(tst_host, tst_if, tmpl);

        TEST_SUBSTEP("Wait for an Rx interrupt.");
        wait_for_interrupt(iut_rpcs, other_queue_id);

        TEST_SUBSTEP("Disable the interrupt on the Rx queue.");
        intr_disable(iut_rpcs, port_id, other_queue_id);

        TEST_SUBSTEP("Receive the packet from the Rx queue.");
        receive_packet(iut_rpcs, port_id, other_queue_id, ptrn);
    }

    TEST_SUCCESS;

cleanup:

    free(reta_conf);
    TEST_END;
}

/** @} */
