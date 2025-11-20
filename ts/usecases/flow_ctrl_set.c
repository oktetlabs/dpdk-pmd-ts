/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2024 OKTET Labs Ltd. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-flow_ctrl_set Set new status of the Ethernet link flow
 *                                   control test
 * @ingroup usecases
 * @{
 *
 * @objective Set new status of the Ethernet link flow control for
 *            Ethernet device
 *
 * @param ethdev_state      The state of Ethernet device
 * @param fc_mode           The Flow Control mode of Ethernet device
 * @param fc_conf_elem      The Flow Control configuration element of
 *                          Ethernet device to iterate
 *
 * @type use case
 *
 * @author Daniil Byshenko <daniil.byshenko@oktetlabs.ru>
 *
 * Make sure that it is possible to set new status of the Ethernet link flow
 * control
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "usecases/flow_ctrl_set"

#include "tapi_cfg_if_flow_control.h"
#include "dpdk_pmd_test.h"

#define TEST_FC_CONF_SET_PARAM(dst_, cmp_val_, src_) \
    do {                                             \
        typeof(src_) src = (src_);                   \
                                                     \
        if (src != (cmp_val_))                       \
            dst_ = src;                              \
    } while (0)

#define TEST_CHECK_UINT64_IS_IN_RANGE(int64_val_, beg_, end_) \
    do {                                                                \
        typeof(int64_val_) val = (int64_val_);                          \
        typeof(int64_val_) beg = (beg_);                                \
        typeof(int64_val_) end = (end_);                                \
                                                                        \
        _Static_assert(_Generic((int64_val_), int64_t: 1, default: 0),  \
                       #int64_val_ " is not of type int64_t");          \
        if (val < beg || val > end)                                     \
        {                                                               \
            TEST_VERDICT(#int64_val_ " isn't in range of "              \
                         "[%lld, %lld]", beg, end);                     \
        }                                                               \
    } while (0)

#define TEST_CHECK_UINT64_IS_IN_UNDEF_UINT32_RANGE(int64_val_) \
    TEST_CHECK_UINT64_IS_IN_RANGE(int64_val_, -1, UINT32_MAX)

#define TEST_CHECK_UINT64_IS_IN_UNDEF_UINT16_RANGE(int64_val_) \
    TEST_CHECK_UINT64_IS_IN_RANGE(int64_val_, -1, UINT16_MAX)

#define TEST_CHECK_UINT64_IS_IN_BOOL3_RANGE(int64_val_) \
    TEST_CHECK_UINT64_IS_IN_RANGE(int64_val_, -1, TRUE)

/**
 * The list of values allowed for parameter of type @p tarpc_rte_eth_fc_mode.
 */
#define TARPC_RTE_ETH_FC_MODE_TYPE_MAPPING_LIST \
    { "NONE", TARPC_RTE_FC_NONE },         \
    { "RX_PAUSE", TARPC_RTE_FC_RX_PAUSE }, \
    { "TX_PAUSE", TARPC_RTE_FC_TX_PAUSE }, \
    { "FULL", TARPC_RTE_FC_FULL }

/**
 * Get the value of parameter of type @p tarpc_rte_eth_fc_mode.
 *
 * @param var_name_  Name of the variable used to get the value of
 *                   "var_name_" parameter of type @p tarpc_rte_eth_fc_mode.
 */
#define TEST_GET_TARPC_RTE_ETH_FC_MODE_TYPE_PARAM(var_name_) \
    TEST_GET_ENUM_PARAM(var_name_, TARPC_RTE_ETH_FC_MODE_TYPE_MAPPING_LIST)

static const char *
get_string_from_enum(int val)
{
    const struct param_map_entry map[] = {
        TARPC_RTE_ETH_FC_MODE_TYPE_MAPPING_LIST, { NULL, 0 }
    };
    size_t i = 0;

    while (map[i].str_val != NULL || map[i].num_val != 0)
    {
        if (map[i].num_val == val)
            return map[i].str_val;

        i += 1;
    }

    return NULL;
}

static te_errno
test_compare_fc_conf(const struct tarpc_rte_eth_fc_conf *cur,
                     const struct tarpc_rte_eth_fc_conf *cmp,
                     te_bool expect_std_autoneg_outcome)
{
    struct tarpc_rte_eth_fc_conf exp_s = *cmp;
    struct tarpc_rte_eth_fc_conf *exp = &exp_s;
    te_errno rc = 0;

    if (exp->autoneg != 0 && expect_std_autoneg_outcome)
    {
        /*
         * Assume the link partner has set 'autoneg=on, rx=on, tx=on', meaning
         * it advertises only 'PAUSE' bit via the IEEE 802.3 Link Code Word, and
         * adjust the expectation of the autonegotiation outcome accordingly.
         */

        if (exp->mode == TARPC_RTE_FC_RX_PAUSE)
        {
            /*
             * Local NIC is set to advertise 'PAUSE | ASYM_DIR', meaning that
             * both ends have 'PAUSE' bit set. This prioritises 'FULL' mode.
             */
            exp->mode = TARPC_RTE_FC_FULL;
        }

        if (exp->mode == TARPC_RTE_FC_TX_PAUSE)
        {
            /* Local NIC is set to advertise 'ASYM_DIR'. Mismatch => 'NONE'. */
            exp->mode = TARPC_RTE_FC_NONE;
        }
    }

    if (cur->mode != exp->mode)
    {
        rc = TE_EINVAL;
        ERROR_VERDICT("The current mode is %s vs the expected %s",
                      get_string_from_enum(cur->mode),
                      get_string_from_enum(exp->mode));
    }

    if (cur->mode == TARPC_RTE_FC_NONE)
    {
        RING("The current mode is %s. Other params are irrelevant",
             get_string_from_enum(cur->mode));
        return rc;
    }

    /* autoneg is a boolean parameter */
    if (!cur->autoneg != !exp->autoneg)
    {
        rc = TE_EINVAL;
        ERROR_VERDICT("The current autoneg is %u vs the expected %u",
                      cur->autoneg, exp->autoneg);
    }

    if (cur->mode == TARPC_RTE_FC_TX_PAUSE || cur->mode == TARPC_RTE_FC_FULL)
    {
        if (exp->high_water != 0 && cur->high_water != exp->high_water)
        {
            rc = TE_EINVAL;
            ERROR_VERDICT("The current high_water is %u vs the expected %u",
                          cur->high_water, exp->high_water);
        }

        if (exp->low_water != 0 && cur->low_water != exp->low_water)
        {
            rc = TE_EINVAL;
            ERROR_VERDICT("The current low_water is %u vs the expected %u",
                          cur->low_water, exp->low_water);
        }

        if (cur->pause_time != exp->pause_time)
        {
            rc = TE_EINVAL;
            ERROR_VERDICT("The current pause_time is %u "
                          "vs the expected %u",
                          cur->pause_time, exp->pause_time);
        }

        /* send_xon is a boolean parameter */
        if (!cur->send_xon != !exp->send_xon)
        {
            rc = TE_EINVAL;
            ERROR_VERDICT("The current send_xon is %u vs the expected %u",
                          cur->send_xon, exp->send_xon);
        }
    }

    /* mac_ctrl_frame_fwd is a boolean parameter */
    if (!cur->mac_ctrl_frame_fwd != !exp->mac_ctrl_frame_fwd)
    {
        rc = TE_EINVAL;
        ERROR_VERDICT("The current mac_ctrl_frame_fwd is %u vs the expected %u",
                      cur->mac_ctrl_frame_fwd, exp->mac_ctrl_frame_fwd);
    }

    return rc;
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server                 *iut_rpcs = NULL;
    const struct if_nameindex      *iut_port = NULL;

    struct test_ethdev_config       ethdev_config;
    int ret;
    struct tarpc_rte_eth_fc_conf    fc_conf_read;
    struct tarpc_rte_eth_fc_conf    fc_conf_write;
    struct tarpc_rte_eth_fc_conf    fc_conf_read_after_start;
    test_ethdev_state               ethdev_state;
    tarpc_rte_eth_fc_mode           mode;
    int64_t                         high_water;
    int64_t                         low_water;
    int64_t                         pause_time;
    int64_t                         send_xon;
    int64_t                         autoneg;
    int64_t                         mac_ctrl_frame_fwd;
    tapi_env_host                   *tst_host = NULL;
    const struct if_nameindex       *tst_if = NULL;
    te_bool                         expect_std_autoneg_outcome;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);
    TEST_GET_ETHDEV_STATE(ethdev_state);
    TEST_GET_TARPC_RTE_ETH_FC_MODE_TYPE_PARAM(mode);
    TEST_GET_INT64_PARAM(high_water);
    TEST_GET_INT64_PARAM(low_water);
    TEST_GET_INT64_PARAM(pause_time);
    TEST_GET_INT64_PARAM(send_xon);
    TEST_GET_INT64_PARAM(autoneg);
    TEST_GET_INT64_PARAM(mac_ctrl_frame_fwd);

    TEST_CHECK_UINT64_IS_IN_UNDEF_UINT32_RANGE(high_water);
    TEST_CHECK_UINT64_IS_IN_UNDEF_UINT32_RANGE(low_water);
    TEST_CHECK_UINT64_IS_IN_UNDEF_UINT16_RANGE(pause_time);
    TEST_CHECK_UINT64_IS_IN_BOOL3_RANGE(send_xon);
    TEST_CHECK_UINT64_IS_IN_BOOL3_RANGE(mac_ctrl_frame_fwd);
    TEST_CHECK_UINT64_IS_IN_BOOL3_RANGE(autoneg);

    TEST_STEP("Set flow control on the partner to 'autoneg=on, rx=on, tx=on'");
    CHECK_RC(tapi_cfg_if_fc_autoneg_set_local(tst_host->ta,
                                              tst_if->if_name, 1));

    CHECK_RC(tapi_cfg_if_fc_rx_set_local(tst_host->ta, tst_if->if_name, 1));
    CHECK_RC(tapi_cfg_if_fc_tx_set_local(tst_host->ta, tst_if->if_name, 1));

    ret = tapi_cfg_if_fc_commit(tst_host->ta, tst_if->if_name);
    if (ret != 0)
    {
        /* Not every NIC vendor supports this, so do not fail the test. */
        ERROR("Failed to initialise flow control on the partner");
    }

    TEST_STEP("Initialize EAL, prepare @p ethdev_state Ethernet device state");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &ethdev_config, ethdev_state));

    TEST_STEP("Get current flow control settings on port @p iut_port");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    ret = rpc_rte_eth_dev_flow_ctrl_get(iut_rpcs, iut_port->if_index,
                                        &fc_conf_read);
    if (-ret == TE_RC(TE_RPC, TE_EOPNOTSUPP))
        TEST_SKIP("Flow control get operation is not supported");

    if (ret != 0)
        TEST_VERDICT("rte_eth_dev_flow_ctrl_get() failed: %r", -ret);

    TEST_STEP("Set new flow control settings on port @p iut_port");

    fc_conf_write = fc_conf_read;
    fc_conf_write.mode = mode;

    TEST_FC_CONF_SET_PARAM(fc_conf_write.high_water, -1, high_water);
    TEST_FC_CONF_SET_PARAM(fc_conf_write.low_water, -1, low_water);
    TEST_FC_CONF_SET_PARAM(fc_conf_write.pause_time, -1, pause_time);
    TEST_FC_CONF_SET_PARAM(fc_conf_write.send_xon, -1, send_xon);
    TEST_FC_CONF_SET_PARAM(fc_conf_write.mac_ctrl_frame_fwd, -1,
                           mac_ctrl_frame_fwd);
    TEST_FC_CONF_SET_PARAM(fc_conf_write.autoneg, -1, autoneg);

    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    ret = rpc_rte_eth_dev_flow_ctrl_set(iut_rpcs, iut_port->if_index,
                                        &fc_conf_write);

    if (-ret == TE_RC(TE_RPC, TE_EOPNOTSUPP))
        TEST_SKIP("Flow control set operation is not supported");

    if (ret != 0)
        TEST_VERDICT("rte_eth_dev_flow_ctrl_set() failed: %r", -ret);

    if (ethdev_state == TEST_ETHDEV_STARTED)
    {
        TEST_STEP("Await link renegotiation");
        test_await_link_up(iut_rpcs, iut_port->if_index);

        expect_std_autoneg_outcome = TRUE;
    }
    else
    {
        expect_std_autoneg_outcome = FALSE;
    }

    TEST_STEP("Get flow control settings");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    ret = rpc_rte_eth_dev_flow_ctrl_get(iut_rpcs, iut_port->if_index,
                                        &fc_conf_read);
    if (ret != 0)
        TEST_VERDICT("Failed to get flow control after set: %r", -ret);

    TEST_STEP("Compare flow control settings with written ones");
    CHECK_RC(test_compare_fc_conf(&fc_conf_read, &fc_conf_write,
                                  expect_std_autoneg_outcome));

    TEST_STEP("If device is already started, stop it");
    if (ethdev_state == TEST_ETHDEV_STARTED)
        CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STOPPED));

    TEST_STEP("Start the  device to check settings once again");
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Get flow control settings once again");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    ret = rpc_rte_eth_dev_flow_ctrl_get(iut_rpcs, iut_port->if_index,
                                        &fc_conf_read_after_start);
    if (ret != 0)
        TEST_VERDICT("Failed to get flow control after start: %r", -ret);

    TEST_STEP("Compare obtained flow control settings vs previously obtained");
    if (ethdev_state == TEST_ETHDEV_STARTED)
    {
        /*
         * No change between 'fc_conf_read_after_start' and 'fc_conf_read' is
         * expected; 'fc_conf_read' already reflects autonegotiation outcome,
         * as it was read in STARTED state. Port restart shouldn't change it.
         */
        expect_std_autoneg_outcome = FALSE;
    }
    else
    {
        expect_std_autoneg_outcome = TRUE;
    }

    CHECK_RC(test_compare_fc_conf(&fc_conf_read_after_start,
                                  &fc_conf_read, expect_std_autoneg_outcome));

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
