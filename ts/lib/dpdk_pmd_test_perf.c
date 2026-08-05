/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief DPDK PMD performance test helpers
 *
 * Implementation of common functions for performance tests.
 *
 * Copyright (C) 2026 OKTET Labs Ltd. All rights reserved.
 */

#define TE_LGR_USER "Performance test library"

#include "te_config.h"

#include "dpdk_pmd_test_perf.h"
#include "tapi_dpdk_stats.h"
#include "tapi_test.h"

static void
test_require_usable_rate(const te_meas_stats_t *meas_stats,
                         const char *prefix)
{
    const te_meas_stats_data_t *data;
    const char *rate_name = prefix == NULL ? "Rate" : prefix;

    data = tapi_dpdk_stats_get_rate_data(meas_stats);
    if (data == NULL || data->num_datapoints == 0)
        TEST_VERDICT("%s: no rate measurements available", rate_name);

    if (!isfinite(data->mean))
        TEST_VERDICT("%s: packet rate is not finite", rate_name);

    if (data->mean < 1.0)
        TEST_VERDICT("%s: packet rate is below 1 PPS", rate_name);
}

te_errno
test_report_port_rate(const char *tool, const te_meas_stats_t *meas_stats,
                      unsigned int packet_size, unsigned int link_speed,
                      const char *prefix, unsigned int port, size_t n_ports)
{
    te_string rate_name = TE_STRING_INIT;
    const char *name = prefix;
    te_errno rc;

    if (n_ports > 1)
    {
        te_string_append(&rate_name, "%s%u", prefix, port);
        name = te_string_value(&rate_name);
    }

    test_require_usable_rate(meas_stats, name);
    rc = tapi_dpdk_stats_log_rates(tool, meas_stats, packet_size,
                                   link_speed, name);

    te_string_free(&rate_name);
    return rc;
}

te_errno
test_report_loopback_rate(const te_meas_stats_t *tx_stats,
                          const te_meas_stats_t *rx_stats,
                          unsigned int packet_size,
                          unsigned int link_speed)
{
    test_require_usable_rate(tx_stats, "Tx");
    test_require_usable_rate(rx_stats, "Rx");

    return tapi_dpdk_stats_log_rates(TAPI_DPDK_TESTPMD_NAME, tx_stats,
                                     packet_size, link_speed, NULL);
}

static te_errno
start_testpmd_await_port_start_attempts(tapi_dpdk_testpmd_job_t *job)
{
    tapi_job_buffer_t port_start = TAPI_JOB_BUFFER_INIT;
    tapi_job_channel_t *port_start_filter = NULL;
    te_errno rc;

    if (job == NULL)
        return TE_RC(TE_TAPI, TE_EINVAL);

    rc = tapi_job_attach_filter(
             TAPI_JOB_CHANNEL_SET(job->out_channels[0]),
             "testpmd port-start completion", true, 0, &port_start_filter);
    if (rc != 0)
        goto out;

    rc = tapi_job_filter_add_regexp(port_start_filter,
                                    "^(Checking link statuses\\.\\.\\.|"
                                    "\\*+ Infos for port [0-9]+ +\\*+)$", 0);
    if (rc != 0)
        goto out;

    rc = tapi_dpdk_testpmd_start(job);
    if (rc != 0)
        goto out;

    rc = tapi_job_receive(TAPI_JOB_CHANNEL_SET(port_start_filter),
                          TAPI_DPDK_TESTPMD_RECEIVE_TIMEOUT_MS,
                          &port_start);
    if (rc != 0)
    {
        if (TE_RC_GET_ERROR(rc) == TE_ETIMEDOUT)
            ERROR("testpmd did not complete port-start attempts in time");
        goto out;
    }

    if (port_start.dropped > 0)
    {
        ERROR("Dropped testpmd port-start markers: %u", port_start.dropped);
        rc = TE_RC(TE_TAPI, TE_EOVERFLOW);
    }
    else if (port_start.eos)
    {
        ERROR("testpmd exited before completing port-start attempts");
        rc = TE_RC(TE_TAPI, TE_EFAIL);
    }
    else if (port_start.filter != port_start_filter)
    {
        ERROR("Unexpected filter returned a testpmd port-start marker");
        rc = TE_RC(TE_TAPI, TE_EPROTO);
    }

out:
    te_string_free(&port_start.data);
    return rc;
}

te_errno
test_start_testpmd_pair_for_link_report(
                        tapi_dpdk_testpmd_job_t *peer_job,
                        tapi_dpdk_testpmd_job_t *reporter_job)
{
    te_errno rc;

    if (peer_job == NULL || reporter_job == NULL || peer_job == reporter_job)
        return TE_RC(TE_TAPI, TE_EINVAL);

    rc = start_testpmd_await_port_start_attempts(peer_job);
    if (rc != 0)
        return rc;

    return tapi_dpdk_testpmd_start(reporter_job);
}

te_errno
test_start_l2fwd_await_ports_started(tapi_dpdk_l2fwd_job_t *l2fwd_job,
                                     unsigned int n_ports)
{
    tapi_job_buffer_t port_started = TAPI_JOB_BUFFER_INIT;
    tapi_job_channel_t *port_started_filter = NULL;
    unsigned int port;
    te_errno rc;

    if (l2fwd_job == NULL || n_ports == 0)
        return TE_RC(TE_TAPI, TE_EINVAL);

    rc = tapi_job_attach_filter(
             TAPI_JOB_CHANNEL_SET(l2fwd_job->out_channels[0]),
             "l2fwd port started", true, 0, &port_started_filter);
    if (rc != 0)
        goto out;

    rc = tapi_job_filter_add_regexp(port_started_filter,
                                    "^Port [0-9]+, MAC address:", 0);
    if (rc != 0)
        goto out;

    rc = tapi_dpdk_l2fwd_start(l2fwd_job);
    if (rc != 0)
        goto out;

    for (port = 0; port < n_ports; ++port)
    {
        rc = tapi_job_receive(TAPI_JOB_CHANNEL_SET(port_started_filter),
                              TAPI_DPDK_L2FWD_RECEIVE_TIMEOUT_MS,
                              &port_started);
        if (rc != 0)
        {
            if (TE_RC_GET_ERROR(rc) == TE_ETIMEDOUT)
                ERROR("L2fwd did not start all ports in time");
            goto out;
        }

        if (port_started.dropped > 0)
        {
            ERROR("Dropped l2fwd port-start markers: %u",
                  port_started.dropped);
            rc = TE_RC(TE_TAPI, TE_EOVERFLOW);
            goto out;
        }
        if (port_started.eos)
        {
            ERROR("L2fwd exited before starting all ports");
            rc = TE_RC(TE_TAPI, TE_EFAIL);
            goto out;
        }
        if (port_started.filter != port_started_filter)
        {
            ERROR("Unexpected filter returned an l2fwd port-start marker");
            rc = TE_RC(TE_TAPI, TE_EPROTO);
            goto out;
        }

        te_string_reset(&port_started.data);
    }

out:
    te_string_free(&port_started.data);
    return rc;
}
