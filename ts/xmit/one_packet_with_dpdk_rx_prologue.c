/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief one_packet_with_dpdk_rx prologue
 *
 * DPDK PMD Test Suite
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 */

#ifndef DOXYGEN_TEST_SPEC

#define TE_TEST_NAME "one_packet_with_dpdk_rx_prologue"

#include "dpdk_pmd_test.h"

#include "tapi_test.h"

/**
 * @retval EXIT_SUCCESS success
 * @retval EXIT_FAILURE failure
 */
int
main(int argc, char **argv)
{
#undef TEST_START_VARS
#define TEST_START_VARS
#undef TEST_START_SPECIFIC
#define TEST_START_SPECIFIC
#undef TEST_END_SPECIFIC
#define TEST_END_SPECIFIC

    cfg_val_type               cvt = CVT_INTEGER;
    const tapi_env_if         *iut_port = NULL;
    rcf_rpc_server            *tst_rpcs = NULL;
    const struct if_nameindex *tst_if = NULL;
    int                        cv_cur;
    int                        cv_max;

    TEST_START_ENV_VARS;
    TEST_START;
    TEST_START_ENV;

    TEST_GET_ENV_IF(iut_port);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(tst_if);

    if (test_is_vdev(iut_port->if_info.if_name))
    {
         /*
          * Passthrough for some queue parameters is broken,
          * Passthrough for Tx prepare invocation is absent.
          */
         TEST_SKIP("IUT virtual devices are unsupported");
    }

    CHECK_RC(cfg_set_instance_fmt(CFG_VAL(STRING, "dpdkrpc"),
                                  "/agent:%s/rpcprovider:", tst_rpcs->ta));
    CHECK_RC(rcf_rpc_server_restart(tst_rpcs));

    CHECK_RC(cfg_get_instance_fmt(&cvt, &cv_max,
                                  "/agent:%s/interface:%s/ring:/rx:/max:",
                                  tst_rpcs->ta, tst_if->if_name));

    CHECK_RC(cfg_get_instance_fmt(&cvt, &cv_cur,
                                  "/agent:%s/interface:%s/ring:/rx:/current:",
                                  tst_rpcs->ta, tst_if->if_name));

    if (cv_max != -1 && cv_cur != -1 && cv_max > cv_cur)
    {
        CHECK_RC(cfg_set_instance_fmt(CFG_VAL(INTEGER, cv_max),
                                      "/agent:%s/interface:%s"
                                      "/ring:/rx:/current:",
                                      tst_rpcs->ta, tst_if->if_name));
    }

    CFG_WAIT_CHANGES;

    TEST_SUCCESS;

cleanup:

    TEST_END;
}

#endif /* !DOXYGEN_TEST_SPEC */
