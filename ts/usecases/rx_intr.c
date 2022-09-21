/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-rx_intr Test Rx interrupts
 * @ingroup usecases
 * @{
 *
 * @objective The test requests Rx queue interrupts on device configuration
 *            then checks that Rx interrupts are triggered when enabled
 *
 * @param tmpl  Traffic template
 *
 * @type use case
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/rx_intr"

#include "dpdk_pmd_test.h"

/**
 * The index of a queue to enable interrupt for
 */
#define TEST_QUEUE 0

/**
 * Maximum amount of time to wait for an interrupt (in milliseconds)
 */
#define TEST_TIMEOUT 5000

/**
 * Repeat traffic transmission and receiving with Rx interrupt enabled
 * this number of times
 */
#define TEST_RX_INTR_REPEAT_COUNT 2

struct scenario_step_context {
    rcf_rpc_server                       *iut_rpcs;
    const struct if_nameindex            *iut_port;
    tapi_env_host                        *tst_host;
    const struct if_nameindex            *tst_if;
    asn_value                            *tmpl;
    asn_value                            *ptrn;
    unsigned int                          pkts_expected;
    te_bool                               intr_enabled;
    te_bool                               intr_maybe_armed;
    te_bool                               intr_expected;
    te_bool                               were_enabled;
};

typedef void (scenario_step_f)(struct scenario_step_context *ctx);

struct scenario_step {
    const char                           *name;
    scenario_step_f                      *step_do;
};

static scenario_step_f step_do_intr_enable;
static scenario_step_f step_do_intr_disable;
static scenario_step_f step_do_send_packet;
static scenario_step_f step_do_receive_packet;
static scenario_step_f step_do_check_intr;

static struct scenario_step scenario_steps[] = {
    { .name = "intr_enable", .step_do = step_do_intr_enable },
    { .name = "intr_disable", .step_do = step_do_intr_disable },
    { .name = "send_packet", .step_do = step_do_send_packet },
    { .name = "receive_packet", .step_do = step_do_receive_packet },
    { .name = "check_intr", .step_do = step_do_check_intr },
    { .name = NULL }
};

int
main(int argc, char *argv[])
{
    rcf_rpc_server                        *iut_rpcs = NULL;
    const struct if_nameindex             *iut_port = NULL;
    tapi_env_host                         *tst_host;
    const struct if_nameindex             *tst_if = NULL;
    char                                 **scenario;
    unsigned int                           scen_args;

    struct test_ethdev_config              ec;
    struct tarpc_rte_eth_conf              eth_conf;

    asn_value                             *tmpl = NULL;

    struct scenario_step_context           ctx;

    unsigned int                           i;
    unsigned int                           j;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_STRING_LIST_PARAM(scenario, scen_args);

    TEST_STEP("Prepare initialized Ethernet device state");
    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ec);

    ec.eth_conf = test_rpc_rte_eth_make_eth_conf(iut_rpcs, iut_port->if_index,
                                                 &eth_conf);

    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_INITIALIZED));

    TEST_STEP("Perform device configuration with Rx queue interrupt enabled");
    eth_conf.intr_conf.rxq = 1;

    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    rc = rpc_rte_eth_dev_configure(iut_rpcs, iut_port->if_index, 1, 1,
                                   &eth_conf);

    if (-rc == TE_RC(TE_RPC, TE_EINVAL))
    {
        TEST_SKIP("Configuration with Rx interrupt enabled is rejected");
    }
    CHECK_RC(rc);

    ec.cur_state = TEST_ETHDEV_CONFIGURED;

    TEST_STEP("Start the device");
    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_STARTED));

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Prepare the packet template");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));
    memset(&ctx, 0, sizeof(ctx));
    ctx.iut_rpcs = iut_rpcs;
    ctx.iut_port = iut_port;
    ctx.tst_host = tst_host;
    ctx.tst_if = tst_if;
    ctx.tmpl = tmpl;
    ctx.ptrn = NULL;
    ctx.intr_enabled = FALSE;
    ctx.intr_maybe_armed = FALSE;
    ctx.were_enabled = FALSE;
    ctx.intr_expected = FALSE;
    ctx.pkts_expected = 0;
    for (i = 0; i < scen_args; i++)
    {
        for (j = 0; scenario_steps[j].name != NULL; j++)
        {
            if (strcmp(scenario[i], scenario_steps[j].name) == 0)
                break;
        }

        if (scenario_steps[j].name == NULL)
            TEST_VERDICT("Unkonown scenario step '%s'", scenario[i]);

        scenario_steps[j].step_do(&ctx);
    }
    TEST_SUCCESS;

cleanup:


    TEST_END;
}

static void
step_do_intr_enable(struct scenario_step_context *ctx)
{
    int rc;

    TEST_SUBSTEP("Enable interrupt on the target Rx queue");

    RPC_AWAIT_IUT_ERROR(ctx->iut_rpcs);
    rc = rpc_rte_eth_dev_rx_intr_enable(ctx->iut_rpcs, ctx->iut_port->if_index,
                                        TEST_QUEUE);

    if (-rc == TE_RC(TE_RPC, TE_EOPNOTSUPP))
    {
        TEST_SKIP("Rx interrupt enable operation is not supported");
    }
    CHECK_RC(rc);

    TEST_SUBSTEP("Bind interrupt caused by the target queue to a per-thread Epoll file descriptor");
    if (!ctx->were_enabled) {
        RPC_AWAIT_IUT_ERROR(ctx->iut_rpcs);
        rc = rpc_rte_eth_dev_rx_intr_ctl_q(ctx->iut_rpcs, ctx->iut_port->if_index,
                                           TEST_QUEUE,
                                           RPC_RTE_EPOLL_PER_THREAD,
                                           TARPC_RTE_INTR_EVENT_ADD, 0);
        if (rc != 0)
            TEST_VERDICT("Cannot bind to Rx queue interrupt: %s",
                         errno_rpc2str(-rc));
        ctx->were_enabled = TRUE;
    }
    ctx->intr_enabled = TRUE;
    if (ctx->pkts_expected > 0)
        ctx->intr_expected = TRUE;
}

static void
step_do_intr_disable(struct scenario_step_context *ctx)
{
    TEST_SUBSTEP("Disable Rx interrupt on the target Rx queue");

    rpc_rte_eth_dev_rx_intr_disable(ctx->iut_rpcs, ctx->iut_port->if_index, TEST_QUEUE);

    if (ctx->intr_enabled)
    {
        ctx->intr_maybe_armed = TRUE;
    }
    ctx->intr_enabled = FALSE;
}

static void
step_do_send_packet(struct scenario_step_context *ctx)
{
    TEST_SUBSTEP("Transmit and sniff a packet");

    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(ctx->tst_host->ta, 0,
                            ctx->tst_if->if_name, ctx->tmpl, NULL, &ctx->ptrn));

    ctx->pkts_expected++;
    if (ctx->intr_enabled || ctx->intr_maybe_armed)
        ctx->intr_expected = TRUE;
}

static void
step_do_receive_packet(struct scenario_step_context *ctx)
{
    rpc_rte_mbuf_p mbuf = RPC_NULL;
    int nb_expected = 0;

    TEST_SUBSTEP("Check that the packet is received on the target queue");

    if (ctx->pkts_expected != 0)
        nb_expected = 1;

    CHECK_RC(test_rx_burst_match_pattern(ctx->iut_rpcs, ctx->iut_port->if_index,
                                         TEST_QUEUE, &mbuf, 1, nb_expected,
                                         ctx->ptrn, TRUE));

    ctx->pkts_expected--;
    rpc_rte_pktmbuf_free(ctx->iut_rpcs, mbuf);
}

static void
step_do_check_intr(struct scenario_step_context *ctx)
{
    int events_received;
    struct tarpc_rte_epoll_event events[BURST_SIZE];

    TEST_SUBSTEP("Check that interrupt was caused or not");

    events_received = rpc_rte_epoll_wait(ctx->iut_rpcs, RPC_RTE_EPOLL_PER_THREAD,
                                         events, TE_ARRAY_LEN(events),
                                         TEST_TIMEOUT);

    if (!ctx->intr_expected)
    {
        if (events_received > 0)
            TEST_VERDICT("Interrupt is not expected, but received");
    }
    else if (ctx->intr_enabled)
    {
        if (events_received == 0)
            TEST_VERDICT("Interrupt is enabled and expected, but not received");
    }
    else if (ctx->intr_maybe_armed)
    {
        if (events_received > 0)
            RING_VERDICT("Interrupt was disabled, kept armed and recevied");

        /* Interrupt is disabled and had a chance to be disarmed */
        ctx->intr_maybe_armed = FALSE;
    }
    else
    {
        TEST_VERDICT("BUG in test: interrupt is disabled, not armed, but exptected");
    }

    /* Always reset interrupt expectation after checking */
    ctx->intr_expected = FALSE;
}
/** @} */
