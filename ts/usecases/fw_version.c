/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-fw_version Retrieve FW version
 * @ingroup usecases
 * @{
 *
 * @objective Make sure that FW version could be retrieved successfully
 *
 * @param ethdev_state         Ethernet device state
 *
 * @type use case
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 *
 * Try to retrieve FW version and verify the output
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/fw_version"

#include <inttypes.h>

#include "dpdk_pmd_test.h"
#include "tapi_rpc_rte_ethdev.h"

#define TEST_FW_VERSION_NUM_MAX 4

int
main(int argc, char *argv[])
{
    rcf_rpc_server             *iut_rpcs = NULL;
    tapi_env_host              *tst_host = NULL;
    const struct if_nameindex  *iut_port = NULL;
    const struct if_nameindex  *tst_if   = NULL;
    struct test_ethdev_config   ethdev_config;
    test_ethdev_state           ethdev_state;

    char                       *fw_version;
    int                         fw_size;
    const char                 *driver_name;
    uint16_t                    ver_numbers[TEST_FW_VERSION_NUM_MAX];
    int                         num_matched;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_ETHDEV_STATE(ethdev_state);

    TEST_STEP("Initialize EAL and start the @p iut_port");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &ethdev_config, ethdev_state));

    TEST_STEP("Allocate an obviously insufficient buffer to store FW version string");
    fw_size = 1;
    fw_version = tapi_malloc(fw_size);

    TEST_STEP("Try to retrieve FW version and, if successful, get the actual length");
    RPC_AWAIT_IUT_ERROR(iut_rpcs);
    fw_size = rpc_rte_eth_dev_fw_version_get(iut_rpcs, iut_port->if_index,
                                             fw_version, fw_size);
    if (fw_size < 0)
    {
        TEST_SKIP("'fw_version_get' operation is not supported");
    }
    else if (fw_size == 0)
    {
        TEST_VERDICT("rte_eth_dev_fw_version_get() returned unexpected value");
    }

    fw_size++;  /* For the terminating character */

    TEST_STEP("Reallocate the buffer sufficient to accommodate the actual string");
    fw_version = tapi_realloc(fw_version, fw_size);

    TEST_STEP("Try to retrieve the full string");
    /*
     * The return code is checked to make sure that fw_size is large enough
     * for firmware version string.
     */
    CHECK_RC(rpc_rte_eth_dev_fw_version_get(iut_rpcs, iut_port->if_index,
                                            fw_version, fw_size));

    TEST_STEP("Verify the output");
    driver_name = ethdev_config.dev_info.driver_name;
    if (strcmp(driver_name, "net_sfc_efx") == 0)
    {
        num_matched = sscanf(fw_version,
                             "%" SCNu16 ".%" SCNu16 ".%" SCNu16 ".%" SCNu16,
                             &ver_numbers[0], &ver_numbers[1],
                             &ver_numbers[2], &ver_numbers[3]);
        if (num_matched != 4)
            TEST_VERDICT("A malformed FW version string was received");
    }
    else if (strcmp(driver_name, "net_i40e") == 0 ||
             strcmp(driver_name, "net_ice") == 0)
    {
        num_matched = sscanf(fw_version,
                             "%" SCNu16 ".%" SCNu16 " ",
                             &ver_numbers[0], &ver_numbers[1]);
        if (num_matched != 2)
            TEST_VERDICT("A malformed FW version string was received");
    }
    else if (strcmp(driver_name, "mlx5_pci") == 0)
    {
        num_matched = sscanf(fw_version,
                             "%" SCNu16 ".%" SCNu16 ".%" SCNu16 " ",
                             &ver_numbers[0], &ver_numbers[1], &ver_numbers[2]);
        if (num_matched != 3)
            TEST_VERDICT("A malformed FW version string was received");
    }

    TEST_SUCCESS;

cleanup:

    TEST_END;
}
/** @} */
