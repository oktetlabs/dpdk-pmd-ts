/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief one_packet_with_dpdk_rx epilogue
 *
 * DPDK PMD Test Suite
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 */

#ifndef DOXYGEN_TEST_SPEC

#define TE_TEST_NAME "one_packet_with_dpdk_rx_epilogue"

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

    const tapi_env_if *iut_port = NULL;
    rcf_rpc_server    *tst_rpcs = NULL;

    TEST_START_ENV_VARS;
    TEST_START;
    TEST_START_ENV;

    TEST_GET_ENV_IF(iut_port);
    TEST_GET_PCO(tst_rpcs);

    if (test_is_vdev(iut_port->if_info.if_name))
    {
         /* Just for symmetry with 'xmit/one_packet_with_dpdk_rx_prologue'. */
         TEST_SKIP("IUT virtual devices are unsupported");
    }

    CHECK_RC(cfg_set_instance_fmt(CFG_VAL(STRING, ""),
                                  "/agent:%s/rpcprovider:", tst_rpcs->ta));
    CHECK_RC(rcf_rpc_server_restart(tst_rpcs));

    TEST_SUCCESS;

cleanup:

    TEST_END;
}

#endif /* !DOXYGEN_TEST_SPEC */
