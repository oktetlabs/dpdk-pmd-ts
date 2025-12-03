/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief DPDK PMD Test Suite
 *
 * Macros to be used in tests. The header must be included from test
 * sources only. It is allowed to use the macros only from @b main()
 * function of the test.
 *
 * @author Andrew Rybchenko <Andrew.Rybchenko@oktetlabs.ru>
 */

#ifndef __TS_DPDK_PMD_TEST_H__
#define __TS_DPDK_PMD_TEST_H__

#include "te_config.h"
#include "te_kvpair.h"

#include "tapi_env.h"
#include "tapi_rpc_rte.h"
#include "tapi_rpc_rte_eal.h"
#include "tapi_tad.h"

/** Maximum number of IUT ports supported by multi-port tests */
#define TEST_MAX_IUT_PORTS 2

#ifndef TEST_START_VARS
/**
 * Test suite specific variables of the test @b main() function.
 */
#define TEST_START_VARS                 TEST_START_ENV_VARS \
    te_kvpair_h  test_params = TAILQ_HEAD_INITIALIZER(test_params);
#endif

#ifndef TEST_START_SPECIFIC
static inline te_errno
test_eal_init(tapi_env *env)
{
    rcf_rpc_server *rpcs = NULL;
    te_errno rc;

    /*
     * If the environment does not contain iut_rpcs, EAL
     * initialization is skipped
     */
    rpcs = tapi_env_get_pco(env, "iut_rpcs");
    if (rpcs != NULL)
    {
        rc = tapi_rte_eal_init(env, rpcs, 0, NULL);
        if (rc != 0)
            return rc;
    }

    /*
     * Skip EAL initialisation if the environment does
     * not contain "tst_rpcs", or RPC provider of the
     * corresponging test agent is not "dpdkrpc".
     */
    rpcs = tapi_env_get_pco(env, "tst_rpcs");
    if (rpcs != NULL)
    {
        cfg_val_type cvt = CVT_STRING;
        char *rpcprovider;

        rc = cfg_get_instance_fmt(&cvt, &rpcprovider,
                                  "/agent:%s/rpcprovider:", rpcs->ta);
        if (rc == 0 && strcmp(rpcprovider, "dpdkrpc") == 0)
        {
            static const char *extra_argv[] = { "--no-pci", NULL };
            int                extra_argc = TE_ARRAY_LEN(extra_argv) - 1;

            /*
             * EAL setup will be limited: NO device probing during cold start,
             * NO device reattach in the case of REUSE_PCO.
             */
            rc = tapi_rte_eal_init(env, rpcs, extra_argc, extra_argv);
            if (rc != 0)
                return rc;
        }
    }

    return 0;
}

/**
 * Test suite specific the first actions of the test.
 */
#define TEST_START_SPECIFIC \
    do {                                                  \
        if (!dpdk_reuse_rpcs())                           \
        {                                                 \
            /* Delay to allow IUT RPC server to die */    \
            SLEEP(1);                                     \
        }                                                 \
                                                          \
        TEST_START_ENV;                                   \
        tapi_test_args2kvpairs(argc, argv, &test_params); \
                                                          \
        CHECK_RC(test_eal_init(&env));                    \
    } while (0)
#endif

#ifndef TEST_END_SPECIFIC
/**
 * Test suite specific part of the last action of the test @b main()
 * function.
 */
#define TEST_END_SPECIFIC \
    do {                                             \
        te_kvpair_fini(&test_params);                \
        (void)tapi_tad_csap_destroy_all(0);          \
        TEST_END_ENV;                                \
    } while (0)
#endif

#include "dpdk_pmd_ts.h"
#include "tapi_test.h"
#include "tapi_env.h"
#include "te_sockaddr.h"
#include "tapi_mem.h"
#include "tapi_sockaddr.h"
#include "tapi_rpc_rte.h"
#include "tapi_rpc_rte_eal.h"
#include "tapi_rpc_rte_mbuf.h"
#include "tapi_rpc_rte_mbuf_ndn.h"
#include "tapi_rpc_rte_ethdev.h"
#include "tapi_rpc_rte_flow.h"
#include "rte_flow_ndn.h"
#include "te_ethernet.h"
#include "tapi_rte_mbuf.h"
#include "tapi_ndn.h"
#include "tapi_tad.h"
#include "tapi_eth.h"
#include "tapi_rte_flow.h"
#include "tapi_rpc_dpdk.h"

/**
 * Ethertype for tests
 */
#define DPMD_TS_ETHERTYPE_RAND 0x1111

/**
 * Number of attempts to receive the packets
 */
#define DPDK_TS_RX_CHECKS 10

/**
 * Waiting times of receiving in milliseconds
 */
#define DPMD_TS_RX_ATTEMPT_WAIT_MS 5

/**
 * Check that the number of packets is expected
 */
#define CHECK_PACKETS_NUM(_actual, _expected) \
    do {                                                                        \
        int __actual = (_actual);                                               \
        int __expected = (_expected);                                           \
                                                                                \
        if (__actual != __expected) {                                           \
            if (__actual == 0)                                                  \
                TEST_VERDICT("No expected packets received");                   \
            else                                                                \
                TEST_VERDICT("%d packets have been received, but should be %d", \
                             __actual, __expected);                             \
        }                                                                       \
    } while (0)

/**
 * Check that the number of matched packets is expected
 */
#define CHECK_MATCHED_PACKETS_NUM(_actual, _expected) \
    do {                                                                        \
        int __actual = (_actual);                                               \
        int __expected = (_expected);                                           \
                                                                                \
        if (__actual != __expected) {                                           \
            if (__actual == 0)                                                  \
                TEST_VERDICT("No matching packets received");                   \
            else                                                                \
                TEST_VERDICT("%d packets have been matched, but should be %d",  \
                             __actual, __expected);                             \
        }                                                                       \
    } while (0)

/**
 * Obtain the value of a VLAN ID parameter
 *
 * @param _parameter    The name to denote both the target 'int' variable
 *                      and the VLAN ID parameter of interest
 */
#define TEST_GET_VLAN_ID_PARAM(_parameter) \
    do {                                                                    \
        TEST_GET_INT_PARAM(_parameter);                                     \
                                                                            \
        if ((_parameter < -1) || (_parameter > TEST_MAX_VLAN_ID))           \
            TEST_VERDICT("Wrong VLAN ID parameter value: %d"                \
                         " (it must not be less than %d or greater than %d",\
                         _parameter, -1, TEST_MAX_VLAN_ID);                 \
    } while (0);

/**
 * Get the value of parameter of type 'tarpc_rss_hash_protos_t'
 *
 * @param var_name_  Name of the variable used to get the value of
 *                   "var_name_" parameter (OUT)
 */
#define TEST_GET_RSS_HASH_PROTOS(var_name_)                             \
    do {                                                                \
        const char  *str_val_;                                          \
                                                                        \
        str_val_ = test_get_param(argc, argv, #var_name_);              \
                                                                        \
        (var_name_) = test_rss_hash_protos_str2bitmask(str_val_);       \
    } while (0);

/**
 * Obtain mbuf segmentation pattern from a string representation
 *
 * @param _parameter    The name to denote both the target variable
 *                      of 'struct test_param_seg_ptrn' type
 *                      and the string parameter of interest
 */
#define TEST_GET_MBUF_SEG_PTRN_PARAM(_parameter) \
    test_parse_mbuf_seg_ptrn(test_get_param(argc, argv, #_parameter),      \
                             &_parameter);                                 \

/**
 * Default length of payload
 */
#define DPMD_TS_PAYLOAD_LEN_DEF 64

/**
 * Name of the IUT DPDK port in the environment.
 */
#define TEST_ENV_IUT_PORT "iut_port"

#endif /* !__TS_DPDK_PMD_TEST_H__ */
