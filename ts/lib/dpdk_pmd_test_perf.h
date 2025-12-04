/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief DPDK PMD Test Suite
 *
 * Declarations for performance tests.
 *
 * @author Ivan Ilchenko <Ivan.Ilchenko@oktetlabs.ru>
 */

#ifndef __TS_DPDK_PMD_PERF_TEST_H__
#define __TS_DPDK_PMD_PERF_TEST_H__

#define TEST_MEAS_MAX_NUM_DATAPOINTS 60
#define TEST_MEAS_MIN_NUM_DATAPOINTS 10
#define TEST_MEAS_ALLOWED_SKIPS 3
#define TEST_MEAS_REQUIRED_CV 0.01
#define TEST_MEAS_DEVIATION_COEFF 0.6
#define TEST_MEAS_INIT_FLAGS                                                \
    (TE_MEAS_STATS_INIT_STAB_REQUIRED | TE_MEAS_STATS_INIT_SUMMARY_REQUIRED |  \
     TE_MEAS_STATS_INIT_IGNORE_ZEROS)

/**
 * Initialize measurements statistics with defaults.
 */
extern te_errno
test_meas_stats_init(te_meas_stats_t *meas_stats)
{
    return te_meas_stats_init(meas_stats,
                              TEST_MEAS_MAX_NUM_DATAPOINTS,
                              TEST_MEAS_INIT_FLAGS,
                              TEST_MEAS_MIN_NUM_DATAPOINTS,
                              TEST_MEAS_REQUIRED_CV,
                              TEST_MEAS_ALLOWED_SKIPS,
                              TEST_MEAS_DEVIATION_COEFF);
}

#endif
