/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright (c) 2025 Advanced Micro Devices, Inc. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-fec Validate FEC mode configuration
 * @ingroup usecases
 * @{
 *
 * @objective Verify setting FEC mode and link transitions associated with that
 *
 * @param env          Network environment configuration (@c env.peer2peer)
 * @param ethdev_state Ethdev state when FEC should be configured
 * @param link_mode    Speed and duplex to configure
 * @param fec_mode     Target mode to configure
 * @param tmpl         Traffic template
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@arknetworks.am>
 * @author Ivan Malov <Ivan.Malov@arknetworks.am>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "usecases/fec"

#include <strings.h>
#include <net/if.h>
#include <stdint.h>
#include <stdlib.h>

#include "dpdk_pmd_test.h"
#include "dpdk_pmd_ts.h"

#include "te_errno.h"
#include "te_enum.h"

#include "tapi_rpc_rte_mbuf_ndn.h"
#include "tapi_rpc_rte_ethdev.h"
#include "tapi_rpc_rte_mbuf.h"
#include "tapi_test_log.h"
#include "tapi_rpc_rte.h"
#include "tapi_test.h"
#include "log_bufs.h"
#include "tapi_env.h"
#include "tapi_eth.h"
#include "tapi_mem.h"
#include "tapi_ndn.h"
#include "rcf_rpc.h"
#include "asn_usr.h"

#define TEST_BIT2MASK(_bit) (1UL << (_bit))

static const te_bit2str map_str_fec[] = {
#define MAP_FEC_MODE(_bit) { TARPC_RTE_ETH_FEC_##_bit##_BIT, #_bit }
    MAP_FEC_MODE(BASER),
    MAP_FEC_MODE(NOFEC),
    MAP_FEC_MODE(AUTO),
    MAP_FEC_MODE(RS),
#undef MAP_FEC_MODE
};

static const te_bit2str map_str_link_mode[] = {
#define MAP_LINK_MODE(_bit) { TARPC_RTE_ETH_LINK_SPEED_##_bit##_BIT, #_bit }
    MAP_LINK_MODE(100G),
    MAP_LINK_MODE(100M),
    MAP_LINK_MODE(2_5G),
    MAP_LINK_MODE(56G),
    MAP_LINK_MODE(50G),
    MAP_LINK_MODE(40G),
    MAP_LINK_MODE(25G),
    MAP_LINK_MODE(20G),
    MAP_LINK_MODE(10G),
    MAP_LINK_MODE(10M),
    MAP_LINK_MODE(5G),
    MAP_LINK_MODE(1G),
#undef MAP_LINK_MODE
    { 0, NULL }
};

const te_enum_trn trn_link_speed[] = {
#define TRN_LINK_SPEED(_bit) \
    {                                              \
      .from = TARPC_RTE_ETH_SPEED_NUM_##_bit,      \
      .to = TARPC_RTE_ETH_LINK_SPEED_##_bit##_BIT, \
    }

    TRN_LINK_SPEED(100G),
    TRN_LINK_SPEED(100M),
    TRN_LINK_SPEED(2_5G),
    TRN_LINK_SPEED(56G),
    TRN_LINK_SPEED(50G),
    TRN_LINK_SPEED(40G),
    TRN_LINK_SPEED(25G),
    TRN_LINK_SPEED(20G),
    TRN_LINK_SPEED(10G),
    TRN_LINK_SPEED(10M),
    TRN_LINK_SPEED(5G),
    TRN_LINK_SPEED(1G),
#undef TRN_LINK_SPEED
    TE_ENUM_TRN_END
};

static uint32_t
test_str2link(const char *str)
{
    const te_bit2str *map = map_str_link_mode;

    for (; map->str != NULL; ++map)
    {
        if (strcasecmp(map->str, str) != 0)
            continue;

        return TEST_BIT2MASK(TARPC_RTE_ETH_LINK_SPEED_FIXED_BIT) |
               TEST_BIT2MASK(map->bit);
    }

    TEST_FAIL("Unknown link mode");
    return 0;
}

static uint32_t
test_str2fec(const char *str)
{
    const te_bit2str *map = map_str_fec;

    for (; map->str != NULL; ++map)
    {
        if (strcasecmp(map->str, str) != 0)
            continue;

        return TEST_BIT2MASK(map->bit);
    }

    TEST_FAIL("Unknown FEC mode");
    return 0;
}

static uint32_t
test_speed2link(uint32_t speed)
{
    int bit;

    bit = te_enum_translate(trn_link_speed, speed, FALSE,
                            TARPC_RTE_ETH_LINK_SPEED__UNKNOWN_BIT);
    if (bit == TARPC_RTE_ETH_LINK_SPEED__UNKNOWN_BIT)
        TEST_FAIL("Unknown link speed");

    return TEST_BIT2MASK(TARPC_RTE_ETH_LINK_SPEED_FIXED_BIT) |
           TEST_BIT2MASK(bit);
}

int
main(int argc, char *argv[])
{
    const struct if_nameindex *iut_port;
    const struct if_nameindex *tst_if;
    test_ethdev_state ethdev_state;
    rcf_rpc_server *iut_rpcs;
    tapi_env_host *tst_host;
    const char *link_mode;
    const char *fec_mode;
    asn_value *tmpl;

    uint32_t mask_fec_sup = TEST_BIT2MASK(TARPC_RTE_ETH_FEC_AUTO_BIT);
    struct tarpc_rte_eth_fec_capa *caps = NULL;
    struct tarpc_rte_eth_conf eth_conf;
    struct tarpc_rte_eth_link el = {0};
    struct test_ethdev_config ec;
    rpc_rte_mbuf_p m = RPC_NULL;
    uint32_t mask_fec_sod;
    uint32_t mask_fec_cur;
    uint32_t mask_fec_req;
    unsigned int nb_caps;
    asn_value *ptrn;
    unsigned int i;
    long long ret;


    TEST_START;

    TEST_GET_HOST(tst_host);
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_ETHDEV_STATE(ethdev_state);
    TEST_GET_STRING_PARAM(link_mode);
    TEST_GET_STRING_PARAM(fec_mode);


    test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ec);

    TEST_STEP("Prepare state TEST_ETHDEV_INITIALIZED");
    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_INITIALIZED));

    test_rpc_rte_eth_make_eth_conf(iut_rpcs, iut_port->if_index, &eth_conf);
    ec.eth_conf = &eth_conf;

    if (strcasecmp(link_mode, "AUTO") != 0)
        ec.eth_conf->link_speeds = test_str2link(link_mode);
    else
        ec.eth_conf->link_speeds = 0;

    TEST_STEP("Prepare state TEST_ETHDEV_STARTED");
    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_STARTED));

    TEST_STEP("Query current (start-of-day) FEC mode");
    RPC_AWAIT_ERROR(iut_rpcs);
    ret = rpc_rte_eth_fec_get(iut_rpcs, iut_port->if_index, &mask_fec_sod);
    if (ret == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
        TEST_SKIP("No support for querying current FEC mode");
    else if (ret != 0)
        TEST_FAIL("Failed to handle current FEC mode query: %r", -ret);

    if (__builtin_popcount(mask_fec_sod) != 1 ||
        mask_fec_sod == TEST_BIT2MASK(TARPC_RTE_ETH_FEC_AUTO_BIT))
        TEST_VERDICT("Invalid start-of-day FEC mode indication");

    mask_fec_req = test_str2fec(fec_mode);

    TEST_STEP("Inspect speed FEC capabilites");
    RPC_AWAIT_ERROR(iut_rpcs);
    ret = rpc_rte_eth_fec_get_capability(iut_rpcs, iut_port->if_index, NULL, 0);
    if (ret == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
        TEST_SKIP("No support for querying speed FEC capabilities");
    else if (ret < 0)
        TEST_FAIL("Failed to handle the speed FEC capability query: %r", -ret);
    else if (ret == 0)
        TEST_VERDICT("No speed FEC capabilities were listed");
    else
        nb_caps = ret;

    caps = tapi_calloc(nb_caps, sizeof(*caps));

    ret = rpc_rte_eth_fec_get_capability(iut_rpcs, iut_port->if_index,
                                         caps, nb_caps);
    if (ret != nb_caps)
        TEST_FAIL("Failed to handle the speed FEC capability query: %r", -ret);

    for (i = 0; i < nb_caps; ++i)
    {
        uint32_t mask_link = test_speed2link(caps[i].speed);

        mask_fec_sup |= caps[i].capa;

        if (mask_link == ec.eth_conf->link_speeds)
        {
            if ((mask_fec_req & caps[i].capa) != mask_fec_req)
                TEST_SKIP("No support for the target link + FEC mode");

            break;
        }
    }

    if (i == nb_caps)
    {
        if (ec.eth_conf->link_speeds == 0 /* AUTO */)
        {
            if ((mask_fec_req & mask_fec_sup) != mask_fec_req)
                TEST_SKIP("No support for the target FEC mode at all");
        }
        else
        {
            TEST_SKIP("No FEC capabilities for the target link mode");
        }
    }

    ec.skip_link_up_check = TRUE;

    TEST_STEP("Prepare state @p ethdev_state");
    CHECK_RC(test_prepare_ethdev(&ec, ethdev_state));

    TEST_STEP("Set the target FEC mode");
    RPC_AWAIT_ERROR(iut_rpcs);
    ret = rpc_rte_eth_fec_set(iut_rpcs, iut_port->if_index, mask_fec_req);
    if (ret != 0)
        TEST_FAIL("Failed to set the target FEC mode: %r", -ret);

    TEST_STEP("Prepare state TEST_ETHDEV_STARTED");
    CHECK_RC(test_prepare_ethdev(&ec, TEST_ETHDEV_STARTED));

    TEST_STEP("Check whether link is up");
    RPC_AWAIT_ERROR(iut_rpcs);
    ret = rpc_dpdk_eth_await_link_up(iut_rpcs, iut_port->if_index,
                                     TEST_LINK_UP_MAX_CHECKS,
                                     TEST_LINK_UP_WAIT_MS,
                                     TEST_POST_LINK_UP_TIMEOUT);

    rpc_rte_eth_link_get_nowait(iut_rpcs, iut_port->if_index, &el);

    if (ret == -TE_RC(TE_RPC, TE_ETIMEDOUT) && el.link_status == 0)
    {
        WARN_VERDICT("Failed to bring link up (taget FEC mode)");

        TEST_SUBSTEP("Check if FEC mode can be set in STARTED/link-down state");
        RPC_AWAIT_ERROR(iut_rpcs);
        ret = rpc_rte_eth_fec_set(iut_rpcs, iut_port->if_index, mask_fec_sod);
        if (ret != 0)
            WARN_VERDICT("Failed to set FEC mode in STARTED/link-down state");

        TEST_STOP;
    }

    TEST_STEP("Validate resulting FEC mode");
    RPC_AWAIT_ERROR(iut_rpcs);
    ret = rpc_rte_eth_fec_get(iut_rpcs, iut_port->if_index, &mask_fec_cur);
    if (ret != 0)
        TEST_FAIL("Failed to handle current FEC mode query: %r", -ret);

    if (mask_fec_req != TEST_BIT2MASK(TARPC_RTE_ETH_FEC_AUTO_BIT) &&
        mask_fec_cur != mask_fec_req)
        TEST_VERDICT("Failed to enter the target FEC mode");

    TEST_STEP("Prepare the traffic template");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));

    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));

    TEST_STEP("Transmit and sniff the packet on Tester");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                                tst_if->if_name, tmpl,
                                                NULL, &ptrn));

    TEST_STEP("Receive the packet");
    ret = test_rx_burst_with_retries(iut_rpcs, iut_port->if_index, 0, &m, 1, 1);
    CHECK_PACKETS_NUM(ret, 1);

    TEST_STEP("Validate the packet");
    rpc_rte_mbuf_match_pattern(iut_rpcs, ptrn, &m, 1, NULL, (unsigned *)&ret);
    CHECK_MATCHED_PACKETS_NUM(ret, 1);

    TEST_SUCCESS;

cleanup:
    if (m != RPC_NULL)
        rpc_rte_pktmbuf_free(iut_rpcs, m);

    free(caps);
    TEST_END;
}
/** @} */
