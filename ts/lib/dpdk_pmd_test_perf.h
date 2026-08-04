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

#include "tapi_dpdk.h"
#include "tapi_l2fwd.h"

#define TEST_MEAS_MAX_NUM_DATAPOINTS 60
#define TEST_MEAS_MIN_NUM_DATAPOINTS 10
#define TEST_MEAS_ALLOWED_SKIPS 3
#define TEST_MEAS_REQUIRED_CV 0.01
#define TEST_MEAS_DEVIATION_COEFF 0.6
#define TEST_MEAS_INIT_FLAGS                                                \
    (TE_MEAS_STATS_INIT_STAB_REQUIRED | TE_MEAS_STATS_INIT_SUMMARY_REQUIRED |  \
     TE_MEAS_STATS_INIT_IGNORE_ZEROS)

/**
 * Start a pair of testpmd jobs in an order suitable for link reporting.
 *
 * Start @p peer_job and wait until it has attempted to start all its ports,
 * then start @p reporter_job. Only the startup link report from
 * @p reporter_job should be checked.
 *
 * @param peer_job      Job started first to activate peer ports.
 * @param reporter_job  Job started second to produce the link report.
 *
 * @return Status code.
 */
extern te_errno test_start_testpmd_pair_for_link_report(
                                    tapi_dpdk_testpmd_job_t *peer_job,
                                    tapi_dpdk_testpmd_job_t *reporter_job);

/**
 * Start l2fwd and wait until all its ports have been started.
 *
 * @param l2fwd_job     L2fwd job handle.
 * @param n_ports       Expected number of started ports.
 *
 * @return Status code.
 */
extern te_errno test_start_l2fwd_await_ports_started(
                                    tapi_dpdk_l2fwd_job_t *l2fwd_job,
                                    unsigned int n_ports);

/**
 * Initialize measurements statistics with defaults.
 */
static inline te_errno
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
