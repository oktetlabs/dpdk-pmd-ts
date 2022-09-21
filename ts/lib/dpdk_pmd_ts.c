/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief DPDK PMD Test Suite
 *
 * Implementation of common functions.
 *
 * @author Andrew Rybchenko <Andrew.Rybchenko@oktetlabs.ru>
 */

/** User name of Socket API test suite library */
#define TE_LGR_USER     "Library"

#include "te_config.h"
#include "te_toeplitz.h"
#include "te_alloc.h"

#if  HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#include <linux/if_packet.h>
#include <netinet/ip_icmp.h>
#include <netinet/if_ether.h>
#include <string.h>

#include "dpdk_pmd_ts.h"
#include "dpdk_pmd_test.h"

#include "tapi_test.h"
#include "tapi_eth.h"
#include "tapi_ndn.h"
#include "ndn_ipstack.h"
#include "ndn_gre.h"
#include "ndn_vxlan.h"
#include "ndn_geneve.h"
#include "tapi_cfg_phy.h"
#include "tapi_cfg_pci.h"
#include "tapi_cfg_cpu.h"
#include "tapi_dpdk.h"

#include "tapi_rpc_rte.h"
#include "tapi_rpc_rte_eal.h"
#include "tapi_rpc_rte_ethdev.h"
#include "rpc_dpdk_offloads.h"

/** Use up to 8 transmit queues by default */
#define TEST_PERF_TXQ_MAX   8

te_bool
test_is_vdev(const char *device_name)
{
    const char vdev_prefix[] = "net_";

    if (strncmp(device_name, vdev_prefix, strlen(vdev_prefix)) == 0)
        return TRUE;
    else
        return FALSE;

}

static int
rx_burst_match_one_packet(rcf_rpc_server  *rpcs,
                          rpc_rte_mbuf_p   pkt,
                          asn_value       *pattern_copy,
                          unsigned int     pattern_unit_idx,
                          te_bool         *reorder,
                          unsigned int    *reorder_idx)
{
    unsigned int  i;
    asn_value     p;

    /* NB: no argument checks here */

    memset(&p, 0, sizeof(p));
    p.asn_type = ndn_traffic_pattern;
    p.syntax = ndn_traffic_pattern->syntax;
    p.tag = ndn_traffic_pattern->tag;
    p.txt_len = -1;
    p.data.integer = 0;
    p.len = 1;

    for (i = pattern_unit_idx;
         i < (unsigned int)asn_get_length(pattern_copy, ""); ++i)
    {
        asn_value    *u = pattern_copy->data.array[pattern_unit_idx];
        unsigned int  ret;

        p.data.array = &u;

        rpc_rte_mbuf_match_pattern(rpcs, &p, &pkt, 1, NULL, &ret);
        if (ret == 1)
        {
            asn_value    *um = pattern_copy->data.array[i];
            unsigned int  j;

            if (i == pattern_unit_idx)
                return 0;

            for (j = i - 1; j >= pattern_unit_idx; --j)
                pattern_copy->data.array[j + 1] = pattern_copy->data.array[j];

            pattern_copy->data.array[pattern_unit_idx] = um;

            *reorder = TRUE;
            *reorder_idx = i;

            return -1;
        }
    }

    return -1;
}

static te_errno
test_print_one_mbuf(rcf_rpc_server  *rpcs,
                    rpc_rte_mbuf_p   pkt,
                    char           **pkt_txt_out)
{
    asn_value      pdus;
    asn_value     *pdusptr;
    asn_value      u;
    asn_value     *uptr;
    asn_value      p;
    asn_value    **pktp;
    size_t         pkt_txt_len;
    char          *pkt_txt;
    unsigned int   ret;

    /* NB: no argument checks here */

    memset(&pdus, 0, sizeof(pdus));
    pdus.asn_type = ndn_generic_pdu_sequence;
    pdus.syntax = ndn_generic_pdu_sequence->syntax;
    pdus.tag = ndn_generic_pdu_sequence->tag;
    pdus.txt_len = -1;
    pdus.data.integer = 0;
    pdus.name = "pdus";

    memset(&u, 0, sizeof(u));
    u.asn_type = ndn_traffic_pattern_unit;
    u.syntax = ndn_traffic_pattern_unit->syntax;
    u.tag = ndn_traffic_pattern_unit->tag;
    u.txt_len = -1;
    u.data.integer = 0;
    pdusptr = &pdus;
    u.data.array = &pdusptr;
    u.len = 1;

    memset(&p, 0, sizeof(p));
    p.asn_type = ndn_traffic_pattern;
    p.syntax = ndn_traffic_pattern->syntax;
    p.tag = ndn_traffic_pattern->tag;
    p.txt_len = -1;
    p.data.integer = 0;
    uptr = &u;
    p.data.array = &uptr;
    p.len = 1;

    rpcs->silent = rpcs->silent_default = TRUE;
    rpc_rte_mbuf_match_pattern(rpcs, &p, &pkt, 1, &pktp, &ret);
    rpcs->silent = rpcs->silent_default = FALSE;
    if (ret != 1)
        return TE_EINVAL;

    pkt_txt_len = asn_count_txt_len(pktp[0], 0) + 1;

    pkt_txt = TE_ALLOC(pkt_txt_len);
    if (pkt_txt == NULL)
        return TE_ENOMEM;

    if (asn_sprint_value(pktp[0], pkt_txt, pkt_txt_len, 0) <= 0)
    {
        free(pkt_txt);
        return TE_EINVAL;
    }

    *pkt_txt_out = pkt_txt;

    return 0;
}

uint16_t
test_rx_burst_with_retries(rcf_rpc_server *rpcs, uint16_t port_id,
                           uint16_t queue_id, rpc_rte_mbuf_p *rx_pkts,
                           uint16_t nb_pkts, uint16_t nb_expected)
{
    unsigned int timeout_ms = TEST_RX_PKTS_WAIT_MAX_MS;
    unsigned int sleep_total_ms = 0;
    unsigned int msleep_now = 1;
    te_bool last_burst = FALSE;
    uint16_t nb_rx = 0;

    while (TRUE)
    {
        uint16_t nb_rx_new;

        nb_rx_new = rpc_rte_eth_rx_burst(rpcs, port_id, queue_id,
                                         rx_pkts + nb_rx, nb_pkts - nb_rx);
        nb_rx += nb_rx_new;

        if (last_burst || sleep_total_ms >= timeout_ms)
            break;

        if (nb_rx >= nb_expected)
        {
            if (nb_rx < nb_pkts)
            {
                MSLEEP(TEST_RX_UNEXP_PKTS_GUARD_TIMEOUT_MS);
                last_burst = TRUE;
                continue;
            }
            else
            {
                break;
            }
        }

        if (nb_rx_new > 0)
            msleep_now = 1;

        msleep_now = MIN(msleep_now, timeout_ms - sleep_total_ms);
        MSLEEP(msleep_now);
        sleep_total_ms += msleep_now;
        msleep_now *= 2;
    }

    return nb_rx;
}

te_errno
test_rx_burst_match_pattern_custom_verdicts(
                                rcf_rpc_server  *rpcs,
                                uint16_t         port_id,
                                uint16_t         queue_id,
                                rpc_rte_mbuf_p  *rx_pkts,
                                uint16_t         nb_pkts,
                                uint16_t         nb_expected,
                                const asn_value *pattern,
                                te_bool          seq_strict,
                                const char      *verdict_no_pkts,
                                const char      *verdict_known_unexp)
{
    uint16_t      nb_rx;
    te_bool       burst_inconsistent = FALSE;
    unsigned int  ret;
    asn_value    *pattern_copy = NULL;
    unsigned int  i;
    unsigned int  j = nb_expected;
    unsigned int  nb_rx_out_of_order = 0;
    unsigned int  nb_rx_unexpected = 0;
    te_bool       need_verdict = FALSE;
    te_errno      rc = 0;

    if (rx_pkts == NULL)
        return TE_EINVAL;

    nb_rx = test_rx_burst_with_retries(rpcs, port_id, queue_id, rx_pkts,
                                       nb_pkts, nb_expected);

    if (nb_rx != nb_expected)
        burst_inconsistent = TRUE;

    if (pattern == NULL || nb_rx == 0)
        goto out;

    if (nb_expected > asn_get_length(pattern, ""))
    {
        ERROR("Pattern is too short (%u units) for sequence matching of %u "
              "packets", asn_get_length(pattern, ""), nb_expected);
        rc = TE_EINVAL;
        goto out;
    }

    tapi_rte_mbuf_match_pattern_seq(rpcs, pattern, rx_pkts, nb_rx, NULL, &ret);
    if ((ret == nb_expected) && !burst_inconsistent)
        goto out;

    if (ret != nb_expected)
    {
        WARN("The number of packets which strictly match the original pattern:"
             " %u, must be %u", ret, nb_expected);
    }

    pattern_copy = asn_copy_value(pattern);
    if (pattern_copy == NULL)
    {
        rc = TE_ENOMEM;
        goto out;
    }

    for (i = 0, j = 0; i < nb_rx; ++i)
    {
        te_bool       reorder = FALSE;
        unsigned int  reorder_idx;
        char         *pkt_txt;

        ret = rx_burst_match_one_packet(rpcs, rx_pkts[i], pattern_copy,
                                        j, &reorder, &reorder_idx);
        if ((ret == 0) || (reorder && !seq_strict))
        {
            ++j;
            continue;
        }

        rc = test_print_one_mbuf(rpcs, rx_pkts[i], &pkt_txt);
        if (rc != 0)
            goto out;

        if (reorder)
        {
            ++nb_rx_out_of_order;

            if (seq_strict)
            {
                ERROR("Packet no. %u is out of order (must be no. %u):\n%s",
                      i, reorder_idx, pkt_txt);
            }
        }
        else
        {
            ++nb_rx_unexpected;
            ERROR("Unexpected packet no. %u:\n%s", i, pkt_txt);
        }

        free(pkt_txt);
    }

out:
    asn_free_value(pattern_copy);

    if (rc != 0)
        return rc;

    if (nb_rx_unexpected > 0)
    {
        ERROR_VERDICT("%u unexpected packets received", nb_rx_unexpected);
        need_verdict = TRUE;
    }

    if (j != nb_expected)
    {
        ERROR("%u packets match, must be %u", j, nb_expected);
        need_verdict = TRUE;
    }

    if (seq_strict && (nb_rx_out_of_order > 0))
    {
        ERROR("%u packets received out of order", nb_rx_out_of_order);
        need_verdict = TRUE;
    }

    if (burst_inconsistent)
    {
        ERROR("%u packets received, must be %u", nb_rx, nb_expected);
        need_verdict = TRUE;
    }

    if (need_verdict)
    {
        if (nb_rx == 0)
        {
            if (verdict_no_pkts != NULL)
                ERROR_VERDICT("%s", verdict_no_pkts);
            else
                ERROR_VERDICT("There are no received packets");
        }
        else if (nb_rx_unexpected == 0)
        {
            if (verdict_known_unexp != NULL)
                ERROR_VERDICT("%s", verdict_known_unexp);
            else
                ERROR_VERDICT("Known but unexpected packets recevied");
        }
        else
        {
            ERROR_VERDICT("Rx operation checks failed");
        }

        return TE_EFAIL;
    }

    return 0;
}

te_errno
test_rx_burst_match_pattern(rcf_rpc_server  *rpcs,
                            uint16_t         port_id,
                            uint16_t         queue_id,
                            rpc_rte_mbuf_p  *rx_pkts,
                            uint16_t         nb_pkts,
                            uint16_t         nb_expected,
                            const asn_value *pattern,
                            te_bool          seq_strict)
{
    return test_rx_burst_match_pattern_custom_verdicts(rpcs, port_id, queue_id,
                                                       rx_pkts, nb_pkts,
                                                       nb_expected,
                                                       pattern, seq_strict,
                                                       NULL, NULL);
}

static te_errno
test_rx_await_pkts_generic(const char     *ta_name,
                   csap_handle_t   csap_handle,
                   unsigned int    nb_pkts,
                   unsigned int    timeount_max_ms,
                   tapi_tad_trrecv_cb_data *cb_data)
{
    unsigned int    nb_pkts_rx_total = 0;
    unsigned int    msleep_total = 0;
    unsigned int    timeout;
    unsigned int    msleep_now = 0;

    if ((ta_name == NULL) || (csap_handle == CSAP_INVALID_HANDLE))
        return TE_EINVAL;

    timeout = (timeount_max_ms == 0) ? TEST_RX_PKTS_WAIT_MAX_MS :
                                       timeount_max_ms;
    while (TRUE)
    {
        unsigned int    nb_pkts_rx;
        te_errno        rc;

        rc = tapi_tad_trrecv_get(ta_name, 0, csap_handle,
                                 cb_data, &nb_pkts_rx);
        if (rc != 0)
            return rc;

        nb_pkts_rx_total += nb_pkts_rx;

        if (nb_pkts_rx_total >= nb_pkts)
        {
            /*
             * Sleep for 10 milliseconds to make sure that no
             * unexpected packets arrive
             */
            MSLEEP(TEST_RX_UNEXP_PKTS_GUARD_TIMEOUT_MS);
            return 0;
        }

        if (msleep_total >= timeout)
            break;

        /*
         * Start from 1 ms sleep.
         * Double sleep interval if no packets received, otherwise drop
         * back to 1 ms.
         */
        if (nb_pkts_rx > 0 || msleep_now == 0)
            msleep_now = 1;
        else
            msleep_now *= 2;

        msleep_now = MIN(msleep_now, timeout - msleep_total);

        MSLEEP(msleep_now);
        msleep_total += msleep_now;
    }

    /*
     * Not everything is received.
     * Tests must (and all existing tests do) check actual number of
     * received packets vs required.
     */
    return 0;
}

te_errno
test_rx_await_pkts(const char     *ta_name,
                   csap_handle_t   csap_handle,
                   unsigned int    nb_pkts,
                   unsigned int    timeount_max_ms)
{
    return test_rx_await_pkts_generic(ta_name, csap_handle, nb_pkts,
                                      timeount_max_ms, NULL);
}

te_errno
test_rx_await_pkts_exec_cb(const char              *ta_name,
                           csap_handle_t            csap_handle,
                           unsigned int             nb_pkts,
                           unsigned int             timeount_max_ms,
                           tapi_tad_trrecv_cb_data *cb_data)
{
    return test_rx_await_pkts_generic(ta_name, csap_handle, nb_pkts,
                                      timeount_max_ms, cb_data);
}

uint16_t
test_tx_prepare_and_burst(rcf_rpc_server *rpcs,
                          uint16_t        port_id,
                          uint16_t        queue_id,
                          rpc_rte_mbuf_p *tx_pkts,
                          uint16_t        nb_pkts)
{
    uint16_t nb_pkts_prepared;

    nb_pkts_prepared = rpc_rte_eth_tx_prepare(rpcs, port_id, queue_id,
                                              tx_pkts, nb_pkts);
    if (nb_pkts_prepared != nb_pkts)
    {
       TEST_VERDICT("Failed to prepare burst of packets; prepared %" PRIu16
                    " packets, must be % " PRIu16, nb_pkts_prepared, nb_pkts);
    }

    return rpc_rte_eth_tx_burst(rpcs, port_id, queue_id, tx_pkts, nb_pkts);
}

static void
tapi_rte_setup_tx_queues(rcf_rpc_server *rpcs,
                         struct tarpc_rte_eth_dev_info *dev_info,
                         uint16_t port_id, int socket_id, uint16_t nb_tx_queue,
                         uint16_t nb_tx_desc,
                         struct tarpc_rte_eth_txconf **tx_confs)
{
    uint16_t queue;
    int rc;

    if (dev_info->max_tx_queues < nb_tx_queue)
        TEST_VERDICT("The number of tx queues is exceeded.");

    for (queue = 0; queue < nb_tx_queue; queue++)
    {
        RPC_AWAIT_IUT_ERROR(rpcs);
        rc = rpc_rte_eth_tx_queue_setup(rpcs, port_id, queue, nb_tx_desc,
                                socket_id,
                                (tx_confs == NULL) ? NULL : tx_confs[queue]);
        if (rc != 0)
        {
            TEST_VERDICT("Failed to setup TxQ %u: %s",
                         queue, errno_rpc2str(-rc));
        }
    }
}

rpc_rte_mempool_p
test_rte_pktmbuf_pool_create(rcf_rpc_server  *rpcs,
                             const char      *name,
                             uint32_t         n,
                             uint32_t         cache_size,
                             uint16_t         priv_size,
                             uint16_t         data_room_size,
                             int              socket_id)
{
    unsigned int   handle_count;
    cfg_handle    *handles;
    te_errno       rc;

    rc = cfg_find_pattern_fmt(&handle_count, &handles,
                              "/local:%s/dpdk:/vdev:net_af_xdp*", rpcs->ta);
    if (rc != 0)
    {
        ERROR("Cannot query af_xdp in vdev registry: %r", rc);
        return RPC_NULL;
    }

    free(handles);

    if (handle_count != 0)
    {
        /* FIXME: one should not hard code this; it should come from PMDs */
        uint16_t data_room_size_max = 3560 /* MAX Rx packet size */ +
                                      256 /* XDP_PACKET_HEADROOM */ +
                                      128 /* RTE_PKTMBUF_HEADROOM */;

        if (data_room_size > data_room_size_max)
            TEST_SKIP("Mbuf data room size is too large for AF_XDP");

        data_room_size = MAX(data_room_size, 2048 /* AF_XDP constraint */);
    }

    return rpc_rte_pktmbuf_pool_create(rpcs, name, n, cache_size, priv_size,
                                       MIN(TEST_RTE_MEMPOOL_DATA_ROOM_OVERHEAD +
                                           data_room_size, UINT16_MAX),
                                       socket_id);
}

rpc_rte_mempool_p
test_rte_pktmbuf_rx_pool_create(rcf_rpc_server                *rpcs,
                                uint16_t                       port_id,
                                struct tarpc_rte_eth_dev_info *dev_info,
                                const char                    *name,
                                uint32_t                       n,
                                uint32_t                       cache_size,
                                uint16_t                       priv_size,
                                uint16_t                       data_room_size,
                                int                            socket_id)
{
    struct tarpc_rte_eth_dev_info local_dev_info;

    if (dev_info == NULL)
    {
        rpc_rte_eth_dev_info_get(rpcs, port_id, &local_dev_info);
        dev_info = &local_dev_info;
    }

    data_room_size = MAX(data_room_size, dev_info->min_rx_bufsize);

    return test_rte_pktmbuf_pool_create(rpcs, name, n, cache_size, priv_size,
                                        data_room_size, socket_id);
}

static void
tapi_rte_setup_rx_queues(rcf_rpc_server *rpcs,
                         struct tarpc_rte_eth_dev_info *dev_info,
                         uint16_t port_id, int socket_id, uint16_t nb_rx_queue,
                         uint16_t nb_rx_desc, uint16_t required_mtu,
                         struct tarpc_rte_eth_rxconf **rx_confs,
                         rpc_rte_mempool_p *mp)
{
    uint16_t queue;
    uint16_t rx_descs = nb_rx_desc;

    if (rx_descs == 0)
    {
        rx_descs = dev_info->default_rxportconf.ring_size;
        if (rx_descs == 0)
            rx_descs = TEST_RTE_ETHDEV_DEF_NB_RX_DESCS;
    }

    if (dev_info->max_rx_queues < nb_rx_queue)
        TEST_VERDICT("The number of rx queues is exceeded.");

    if (*mp == RPC_NULL)
        *mp = test_rte_pktmbuf_rx_pool_create(
                  rpcs, port_id, dev_info, TEST_PKTS_MEMPOOL_NAME,
                  MAX(nb_rx_queue * rx_descs, TEST_RTE_MEMPOOL_DEF_CACHE * 2) +
                  TEST_RTE_MEMPOOL_DEF_EXTRA,
                  TEST_RTE_MEMPOOL_DEF_CACHE, TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                  required_mtu != 0 ?
                  required_mtu + TEST_RTE_MEMPOOL_DATA_ROOM_OVERHEAD :
                  TEST_RTE_MEMPOOL_DEF_DATA_ROOM, socket_id);

    for (queue = 0; queue < nb_rx_queue; queue++)
    {
        rpc_rte_eth_rx_queue_setup(rpcs, port_id, queue, rx_descs, socket_id,
                                   (rx_confs == NULL) ? NULL : rx_confs[queue],
                                   *mp);
    }
}

static void
test_setup_ethdev_initialized(struct test_ethdev_config *test_ethdev_config)
{
    const struct if_nameindex *iut_port_tmp;
    const char                 prefix_tmp[] = "net_bonding";
    int                        ret;

    iut_port_tmp = tapi_env_get_if(test_ethdev_config->env, TEST_ENV_IUT_PORT);
    CHECK_NOT_NULL(iut_port_tmp);

    if (test_ethdev_config->argc != 0)
    {
        /*
         * We always initialise EAL in TEST_START_SPECIFIC
         * with no custom arguments. If the test needs custom
         * arguments, restart the server and initialise EAL.
         */
        CHECK_RC(rcf_rpc_server_restart(test_ethdev_config->rpcs));

        /* RPC server must have enough time to die */
        SLEEP(1);

        tapi_rte_eal_init(test_ethdev_config->env, test_ethdev_config->rpcs,
                          test_ethdev_config->argc, test_ethdev_config->argv);
    }

    /*
     * RTE bonded device probed on EAL initialisation has no active slaves
     * and thus provides clobbered device information unsuitable for tests
     * which often commence by looking at certain fields in this structure.
     *
     * Carry out a dummy configuration call to make RTE vdev inherit valid
     * device information from its back-ends and extract the information.
     */
    ret = strncmp(iut_port_tmp->if_name, prefix_tmp, strlen(prefix_tmp));
    if (ret == 0)
    {
        struct tarpc_rte_eth_conf ec;

        memset(&ec, 0, sizeof(ec));

        rpc_rte_eth_dev_configure(test_ethdev_config->rpcs,
                                  test_ethdev_config->port_id,
                                  1, 1, &ec);
    }

    /* Update information about Ethernet device */
    rpc_rte_eth_dev_info_get(test_ethdev_config->rpcs, test_ethdev_config->port_id,
                             &test_ethdev_config->dev_info);
}

static void
test_workaround_tx_fixed_offloads(rcf_rpc_server               *rpcs,
                                  struct tarpc_rte_eth_txmode  *txmode)
{
    cfg_val_type   vt = CVT_STRING;
    char          *eal_args;
    uint64_t       flag;
    te_errno       rc;

    rc = cfg_get_instance_fmt(&vt, &eal_args, "/agent:%s/rpcserver:%s/config:",
                              rpcs->ta, rpcs->name);
    if (rc != 0)
        return;

    /* Workaround for sfc-specific Tx fast free problem */
    if (strstr(eal_args, "tx_datapath=ef10_simple") != NULL)
    {
        flag = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE_BIT);
        txmode->offloads |= flag;
    }

    free(eal_args);
}

struct tarpc_rte_eth_conf *
test_rpc_rte_eth_make_eth_conf(rcf_rpc_server             *rpcs,
                               uint16_t                    port_id,
                               struct tarpc_rte_eth_conf  *eth_conf)
{
    tapi_rpc_rte_eth_make_eth_conf(rpcs, port_id, eth_conf);
    test_workaround_tx_fixed_offloads(rpcs, &eth_conf->txmode);
    return eth_conf;
}

static void
test_setup_ethdev_configured(struct test_ethdev_config *test_ethdev_config)
{
    struct tarpc_rte_eth_conf   eth_conf;
    int                         socket_id;
    te_errno                    rc;

    RPC_AWAIT_IUT_ERROR(test_ethdev_config->rpcs);
    rc = rpc_rte_eth_dev_configure(test_ethdev_config->rpcs,
                        test_ethdev_config->port_id,
                        test_ethdev_config->nb_rx_queue,
                        test_ethdev_config->nb_tx_queue,
                        (test_ethdev_config->eth_conf == NULL) ?
                        test_rpc_rte_eth_make_eth_conf(test_ethdev_config->rpcs,
                        test_ethdev_config->port_id, &eth_conf) :
                        test_ethdev_config->eth_conf);

    if (rc != 0)
        TEST_VERDICT("rte_eth_dev_configure() failed: %r", -rc);

    socket_id = rpc_rte_eth_dev_socket_id(test_ethdev_config->rpcs,
                                          test_ethdev_config->port_id);

    test_ethdev_config->socket_id = (socket_id != -1) ? socket_id : 0;
}

static void
test_setup_ethdev_rx_setup_done(struct test_ethdev_config *test_ethdev_config)
{
    struct tarpc_rte_eth_dev_info *dev_info = &test_ethdev_config->dev_info;
    uint16_t nb_rx_desc = MAX(test_ethdev_config->min_rx_desc,
                              dev_info->rx_desc_lim.nb_min);

    if (dev_info->rx_desc_lim.nb_align != 0)
        nb_rx_desc = TE_ALIGN(nb_rx_desc, dev_info->rx_desc_lim.nb_align);

    tapi_rte_setup_rx_queues(test_ethdev_config->rpcs,
                             dev_info,
                             test_ethdev_config->port_id,
                             test_ethdev_config->socket_id,
                             test_ethdev_config->nb_rx_queue,
                             nb_rx_desc, test_ethdev_config->required_mtu,
                             test_ethdev_config->rx_confs,
                             &test_ethdev_config->mp);

    test_ethdev_config->is_rx_setup = TRUE;
}

static void
test_setup_ethdev_tx_setup_done(struct test_ethdev_config *test_ethdev_config)
{
    struct tarpc_rte_eth_dev_info *dev_info = &test_ethdev_config->dev_info;
    uint16_t nb_tx_desc = MAX(test_ethdev_config->min_tx_desc,
                              dev_info->tx_desc_lim.nb_min);

    if (dev_info->tx_desc_lim.nb_align != 0)
        nb_tx_desc = TE_ALIGN(nb_tx_desc, dev_info->tx_desc_lim.nb_align);

    tapi_rte_setup_tx_queues(test_ethdev_config->rpcs,
                             dev_info,
                             test_ethdev_config->port_id,
                             test_ethdev_config->socket_id,
                             test_ethdev_config->nb_tx_queue,
                             nb_tx_desc,
                             test_ethdev_config->tx_confs);
}

void
test_await_link_up(rcf_rpc_server *rpcs,
                   uint16_t        port_id)
{
    te_errno rc;

    RPC_AWAIT_ERROR(rpcs);
    rc = rpc_dpdk_eth_await_link_up(rpcs, port_id, TEST_LINK_UP_MAX_CHECKS,
                                    TEST_LINK_UP_WAIT_MS,
                                    TEST_POST_LINK_UP_TIMEOUT);
    if (rc == -TE_RC(TE_RPC, TE_ETIMEDOUT))
    {
        struct tarpc_rte_eth_link eth_link;

        memset(&eth_link, 0, sizeof(eth_link));

        rpc_rte_eth_link_get_nowait(rpcs, port_id, &eth_link);
        if (eth_link.link_status)
            return;
        else
            TEST_VERDICT("Link is not ready");
    }

    CHECK_RC(rc);
}

void
test_rte_eth_dev_set_mtu_await_link_up(rcf_rpc_server *rpcs,
                                       uint16_t        port_id,
                                       uint16_t        mtu,
                                       struct test_ethdev_config *ethdev_config)
{
    test_set_mtu(rpcs, port_id, mtu, ethdev_config);
    test_await_link_up(rpcs, port_id);
}

te_errno
test_tunnel_udp_port_add(struct test_ethdev_config       *test_ethdev_config,
                         struct tarpc_rte_eth_udp_tunnel *tunnel_udp)
{
    unsigned int nb = 0;
    te_errno rc;

    do {
        if (nb != 0)
            MSLEEP(TEST_TUNNEL_UDP_PORT_WAIT_MS);

        RPC_AWAIT_ERROR(test_ethdev_config->rpcs);
        rc = rpc_rte_eth_dev_udp_tunnel_port_add(test_ethdev_config->rpcs,
                                                 test_ethdev_config->port_id,
                                                 tunnel_udp);
    } while ((test_ethdev_config->cur_state == TEST_ETHDEV_STARTED) &&
             (rc == -TE_RC(TE_RPC, TE_EIO)) &&
             (++nb < TEST_TUNNEL_UDP_PORT_MAX_CHECKS));

    if (rc == 0)
        MSLEEP(TEST_TUNNEL_UDP_PORT_AWAIT_MC_REBOOT_MS);

    return rc;
}

te_errno
test_tunnel_udp_port_del(struct test_ethdev_config       *test_ethdev_config,
                         struct tarpc_rte_eth_udp_tunnel *tunnel_udp)
{
    unsigned int nb = 0;
    te_errno rc;

    do {
        if (nb != 0)
            MSLEEP(TEST_TUNNEL_UDP_PORT_WAIT_MS);

        RPC_AWAIT_ERROR(test_ethdev_config->rpcs);
        rc = rpc_rte_eth_dev_udp_tunnel_port_delete(test_ethdev_config->rpcs,
                                                    test_ethdev_config->port_id,
                                                    tunnel_udp);
    } while ((test_ethdev_config->cur_state == TEST_ETHDEV_STARTED) &&
             (rc == -TE_RC(TE_RPC, TE_EIO)) &&
             (++nb < TEST_TUNNEL_UDP_PORT_MAX_CHECKS));

    if (rc == 0)
        MSLEEP(TEST_TUNNEL_UDP_PORT_AWAIT_MC_REBOOT_MS);

    return rc;
}

static void
test_rte_eth_dev_start(rcf_rpc_server *rpcs, uint16_t port_id)
{
    rpc_rte_eth_dev_start(rpcs, port_id);

    test_await_link_up(rpcs, port_id);
}

static void
test_setup_ethdev_started(struct test_ethdev_config *test_ethdev_config)
{
    test_rte_eth_dev_start(test_ethdev_config->rpcs,
                           test_ethdev_config->port_id);
}

static void
test_setup_ethdev_stopped(struct test_ethdev_config *test_ethdev_config)
{
    rpc_rte_eth_dev_stop(test_ethdev_config->rpcs, test_ethdev_config->port_id);
}

static void
test_setup_ethdev_closed(struct test_ethdev_config *test_ethdev_config)
{
    rpc_rte_eth_dev_close(test_ethdev_config->rpcs, test_ethdev_config->port_id);
    test_ethdev_config->closed = TRUE;
}

static void
test_setup_ethdev_detached(struct test_ethdev_config *test_ethdev_config)
{
    rpc_rte_eal_hotplug_remove(test_ethdev_config->rpcs,
                               test_ethdev_config->bus_name,
                               test_ethdev_config->dev_name);
}

static void
test_rollback_ethdev_initialized(struct test_ethdev_config *test_ethdev_config)
{
    UNUSED(test_ethdev_config);
    TEST_VERDICT("It's not possible to rollback the initialized state");
};

static void
test_rollback_ethdev_configured(struct test_ethdev_config *test_ethdev_config)
{
    rpc_rte_eth_dev_stop(test_ethdev_config->rpcs, test_ethdev_config->port_id);
    rpc_rte_eth_dev_close(test_ethdev_config->rpcs, test_ethdev_config->port_id);
    test_ethdev_config->closed = TRUE;
};

static void
test_rollback_ethdev_rx_setup_done(struct test_ethdev_config *test_ethdev_config)
{
    unsigned int cur_queue;

    for (cur_queue = 0; cur_queue < test_ethdev_config->nb_rx_queue; cur_queue++)
    {
        int ret;

        RPC_AWAIT_ERROR(test_ethdev_config->rpcs);
        ret = rpc_rte_eth_dev_rx_queue_stop(test_ethdev_config->rpcs,
                                            test_ethdev_config->port_id,
                                            cur_queue);
        ret = (ret == -TE_RC(TE_RPC, TE_EOPNOTSUPP)) ? 0 : ret;
        CHECK_RC(ret);
    }

    tapi_rte_setup_rx_queues(test_ethdev_config->rpcs,
                             &test_ethdev_config->dev_info,
                             test_ethdev_config->port_id,
                             test_ethdev_config->socket_id,
                             0, 0, 0, NULL, &test_ethdev_config->mp);

    test_ethdev_config->is_rx_setup = FALSE;
};

static void
test_rollback_ethdev_tx_setup_done(struct test_ethdev_config *test_ethdev_config)
{
    unsigned int cur_queue;

    for (cur_queue = 0; cur_queue < test_ethdev_config->nb_tx_queue; cur_queue++)
    {
        int ret;

        RPC_AWAIT_ERROR(test_ethdev_config->rpcs);
        ret = rpc_rte_eth_dev_tx_queue_stop(test_ethdev_config->rpcs,
                                            test_ethdev_config->port_id,
                                            cur_queue);
        ret = (ret == -TE_RC(TE_RPC, TE_EOPNOTSUPP)) ? 0 : ret;
        CHECK_RC(ret);
    }

    tapi_rte_setup_tx_queues(test_ethdev_config->rpcs,
                             &test_ethdev_config->dev_info,
                             test_ethdev_config->port_id,
                             test_ethdev_config->socket_id,
                             0, 0, NULL);
};

static void
test_rollback_ethdev_started(struct test_ethdev_config *test_ethdev_config)
{
    rpc_rte_eth_dev_stop(test_ethdev_config->rpcs, test_ethdev_config->port_id);
};

static void
test_rollback_ethdev_stopped(struct test_ethdev_config *test_ethdev_config)
{
    test_rte_eth_dev_start(test_ethdev_config->rpcs,
                           test_ethdev_config->port_id);
};

static void
test_rollback_ethdev_closed(struct test_ethdev_config *test_ethdev_config)
{
    UNUSED(test_ethdev_config);
    TEST_VERDICT("It's not possible to rollback the closed state");
};

static void
test_rollback_ethdev_detached(struct test_ethdev_config *test_ethdev_config)
{
    UNUSED(test_ethdev_config);
    TEST_VERDICT("It's not possible to rollback the detached state");
};

typedef void (*test_ethdev_setup_state)(struct test_ethdev_config *test_ethdev_config);
typedef void (*test_ethdev_rollback_state)(struct test_ethdev_config *test_ethdev_config);

void
test_prepare_ethdev_dummy(struct test_ethdev_config *test_ethdev_config)
{
    UNUSED(test_ethdev_config);
}

static struct {
    test_ethdev_setup_state     setup;
    test_ethdev_rollback_state  rollback;
} tapi_ethdev_states[] = {
    /** Do nothing for uninitialized state */
    { test_prepare_ethdev_dummy, test_prepare_ethdev_dummy },
#define TEST_PREPARE_ETHDEV(_state) { test_setup_ethdev_##_state, \
                                      test_rollback_ethdev_##_state }
    TEST_PREPARE_ETHDEV(initialized),
    TEST_PREPARE_ETHDEV(configured),
    TEST_PREPARE_ETHDEV(rx_setup_done),
    TEST_PREPARE_ETHDEV(tx_setup_done),
    /** Do nothing for preparing rxtx state */
    { test_prepare_ethdev_dummy, test_prepare_ethdev_dummy },
    TEST_PREPARE_ETHDEV(started),
    TEST_PREPARE_ETHDEV(stopped),
    TEST_PREPARE_ETHDEV(closed),
    TEST_PREPARE_ETHDEV(detached)
#undef TEST_PREPARE_ETHDEV
};

struct test_ethdev_config *
test_prepare_config_mk(tapi_env *env,
                       rcf_rpc_server *rpcs,
                       const char *dev_name,
                       uint16_t port_id,
                       struct test_ethdev_config *test_ethdev_config)
{
    if (test_ethdev_config == NULL)
        return NULL;

    memset(test_ethdev_config, 0, sizeof(*test_ethdev_config));

    test_ethdev_config->env = env;
    test_ethdev_config->rpcs = rpcs;
    test_ethdev_config->cur_state = TEST_ETHDEV_UNINITIALIZED;

    test_ethdev_config->argc = 0;
    test_ethdev_config->argv = NULL;

    test_ethdev_config->eth_conf = NULL;

    if (test_is_vdev(dev_name))
        test_ethdev_config->bus_name = "vdev";
    else
        test_ethdev_config->bus_name = "pci";

    if (strlen(dev_name) >= sizeof(test_ethdev_config->dev_name))
        TEST_VERDICT("Device name is too long");
    te_strlcpy(test_ethdev_config->dev_name, dev_name,
               sizeof(test_ethdev_config->dev_name));

    test_ethdev_config->port_id = port_id;
    test_ethdev_config->nb_rx_queue = 1;
    test_ethdev_config->nb_tx_queue = 1;
    test_ethdev_config->rx_confs = NULL;
    test_ethdev_config->tx_confs = NULL;
    test_ethdev_config->mp = RPC_NULL;
    test_ethdev_config->is_rx_setup = FALSE;
    test_ethdev_config->closed = FALSE;
    test_ethdev_config->required_mtu = 0;

    return test_ethdev_config;
}

struct test_ethdev_config *
test_prepare_config_def_mk(tapi_env *env,
                           rcf_rpc_server *rpcs,
                           const struct if_nameindex *iut_port,
                           struct test_ethdev_config *test_ethdev_config)
{
    if (test_ethdev_config == NULL)
        return NULL;

    test_prepare_config_mk(env, rpcs, iut_port->if_name, iut_port->if_index,
                           test_ethdev_config);

    return test_ethdev_config;
}

te_errno
test_prepare_ethdev(struct test_ethdev_config *test_ethdev_config,
                    test_ethdev_state st)
{
    if (test_ethdev_config == NULL)
        return TE_EWRONGPTR;

    test_ethdev_state cur_state;
    int               next_step;

    if (st < TEST_ETHDEV_UNINITIALIZED || st > TEST_ETHDEV_DETACHED)
        return TE_EINVAL;

    cur_state = test_ethdev_config->cur_state;
    next_step = (cur_state < st) ? 1 : -1;

    if ((cur_state == TEST_ETHDEV_TX_SETUP_DONE) &&
        (next_step > 0) && !test_ethdev_config->is_rx_setup)
        test_setup_ethdev_rx_setup_done(test_ethdev_config);

    while (cur_state != st)
    {
        cur_state += next_step;

        if ((cur_state == TEST_ETHDEV_RX_SETUP_DONE &&
            st == TEST_ETHDEV_TX_SETUP_DONE) ||
            (cur_state == TEST_ETHDEV_TX_SETUP_DONE &&
            st == TEST_ETHDEV_RX_SETUP_DONE))
            continue;

        if (test_ethdev_config->closed)
            TEST_VERDICT("Unable to change device state after device close");

        if (next_step > 0)
            tapi_ethdev_states[cur_state].setup(test_ethdev_config);
        else
            tapi_ethdev_states[cur_state + 1].rollback(test_ethdev_config);
    }

    test_ethdev_config->cur_state = cur_state;

    return 0;
}

te_errno
test_default_prepare_ethdev(tapi_env *env,
                            rcf_rpc_server *rpcs,
                            const struct if_nameindex *iut_port,
                            struct test_ethdev_config *test_ethdev_config,
                            test_ethdev_state st)
{
    struct test_ethdev_config test_ethdev_config_temp;

    return (test_prepare_ethdev(test_prepare_config_def_mk(
                                    env, rpcs, iut_port,
                                    (test_ethdev_config == NULL) ?
                                    &test_ethdev_config_temp :
                                    test_ethdev_config), st));
}

static te_errno
test_calc_hash_by_pdus_and_hf(tarpc_rss_hash_protos_t  hf,
                              uint8_t                 *rss_key,
                              asn_value               *pdus,
                              uint32_t                *hash_regular,
                              uint32_t                *hash_symmetric)
{
    unsigned int              nb_pdus_o;
    asn_value               **pdus_o;
    asn_value                *pdu_ip4;
    asn_value                *pdu_ip6;
    asn_value                *pdu_tcp;
    asn_value                *pdu_udp;
    tarpc_rss_hash_protos_t   hf_ip4;
    tarpc_rss_hash_protos_t   hf_ip6;
    tarpc_rss_hash_protos_t   hf_ip;
    asn_value                *pdu_l4;
    tarpc_rss_hash_protos_t   hf_l4;
    te_toeplitz_hash_cache   *hash_cache;
    uint8_t                   src_addr[sizeof(struct in6_addr)];
    uint8_t                   dst_addr[sizeof(struct in6_addr)];
    size_t                    addr_len = 0;
    uint16_t                  src_port = 0;
    uint16_t                  dst_port = 0;
    te_errno                  rc;

    rc = tapi_tad_pdus_relist_outer_inner(pdus, &nb_pdus_o, &pdus_o,
                                          NULL, NULL);
    if (rc != 0)
        return rc;

    pdu_ip4 = asn_choice_array_look_up_value(nb_pdus_o, pdus_o, TE_PROTO_IP4);
    pdu_ip6 = asn_choice_array_look_up_value(nb_pdus_o, pdus_o, TE_PROTO_IP6);
    pdu_tcp = asn_choice_array_look_up_value(nb_pdus_o, pdus_o, TE_PROTO_TCP);
    pdu_udp = asn_choice_array_look_up_value(nb_pdus_o, pdus_o, TE_PROTO_UDP);

    free(pdus_o);

    if (pdu_ip4 == pdu_ip6)
        return TE_RC(TE_TAPI, TE_EINVAL);

    if (pdu_tcp != NULL && pdu_udp != NULL)
        return TE_RC(TE_TAPI, TE_EINVAL);

    hf_ip4 = TEST_ETH_RSS_IPV4;
    hf_ip6 = TEST_ETH_RSS_IPV6;

    if (pdu_tcp != NULL)
    {
        hf_ip4 |= TEST_ETH_RSS_NONFRAG_IPV4_TCP;
        hf_ip6 |= TEST_ETH_RSS_NONFRAG_IPV6_TCP;
    }

    if (pdu_udp != NULL)
    {
        hf_ip4 |= TEST_ETH_RSS_NONFRAG_IPV4_UDP;
        hf_ip6 |= TEST_ETH_RSS_NONFRAG_IPV6_UDP;
    }

    hf_ip = (pdu_ip4 != NULL) ? hf_ip4 : hf_ip6;
    if ((hf & hf_ip) != 0)
    {
        asn_value *pdu_ip = (pdu_ip4 != NULL) ? pdu_ip4 : pdu_ip6;

        addr_len = (pdu_ip4 != NULL) ? sizeof(struct in_addr) :
                                       sizeof(struct in6_addr);
        rc = asn_read_value_field(pdu_ip, src_addr, &addr_len,
                                  "src-addr.#plain");
        if (rc != 0)
            return TE_RC(TE_TAPI, rc);

        rc = asn_read_value_field(pdu_ip, dst_addr, &addr_len,
                                  "dst-addr.#plain");
        if (rc != 0)
            return TE_RC(TE_TAPI, rc);
    }

    if (pdu_tcp != NULL)
    {
        pdu_l4 = pdu_tcp;
        hf_l4 = (pdu_ip4 != NULL) ? TEST_ETH_RSS_NONFRAG_IPV4_TCP :
                                    TEST_ETH_RSS_NONFRAG_IPV6_TCP;
    }
    else if (pdu_udp != NULL)
    {
        pdu_l4 = pdu_udp;
        hf_l4 = (pdu_ip4 != NULL) ? TEST_ETH_RSS_NONFRAG_IPV4_UDP :
                                    TEST_ETH_RSS_NONFRAG_IPV6_UDP;
    }
    else
    {
        pdu_l4 = NULL;
    }

    if (pdu_l4 != NULL && (hf & hf_l4) == hf_l4)
    {
        int32_t value_read;

        rc = asn_read_int32(pdu_l4, &value_read, "src-port.#plain");
        if (rc != 0)
            return TE_RC(TE_TAPI, rc);
        src_port = htons((uint16_t)value_read);

        rc = asn_read_int32(pdu_l4, &value_read, "dst-port.#plain");
        if (rc != 0)
            return TE_RC(TE_TAPI, rc);
        dst_port = htons((uint16_t)value_read);
    }

    hash_cache = te_toeplitz_cache_init(rss_key);

    if (hash_regular != NULL)
    {
        *hash_regular = te_toeplitz_hash(hash_cache, addr_len, src_addr,
                                         src_port, dst_addr, dst_port);
    }

    if (hash_symmetric != NULL)
    {
        uint32_t  input_dst;
        uint32_t  input_src;
        uint32_t  input_xor;
        uint8_t  *input_xorp = (uint8_t *)&input_xor;
        unsigned int off;

        off = (addr_len == sizeof(struct in6_addr)) ?
              sizeof(struct in6_addr) - sizeof(struct in_addr) : 0;

        memcpy(&input_dst, dst_addr + off, sizeof(input_dst));
        memcpy(&input_src, src_addr + off, sizeof(input_src));
        input_xor = input_dst ^ input_src;

        *hash_symmetric = te_toeplitz_hash_data(hash_cache, input_xorp, 0,
                                                sizeof(input_xor));
    }

    te_toeplitz_hash_fini(hash_cache);

    return 0;
}

te_errno
test_calc_hash_by_tmpl_and_hf(tarpc_rss_hash_protos_t  hf,
                              uint8_t                 *rss_key,
                              asn_value               *tmpl,
                              uint32_t                *hash_regular,
                              uint32_t                *hash_symmetric)
{
    asn_value *pdus;
    te_errno   rc;

    if (tmpl == NULL)
        return TE_EINVAL;

    rc = asn_get_subvalue(tmpl, &pdus, "pdus");
    if (rc != 0)
        return rc;

    return test_calc_hash_by_pdus_and_hf(hf, rss_key, pdus,
                                         hash_regular, hash_symmetric);
}

te_errno
test_change_src_addr_by_reta_index(
    const te_toeplitz_hash_cache *toeplitz_hash_cache,
    unsigned int hash, uint8_t *src_addr, unsigned int addr_size,
    uint16_t reta_size, const unsigned int *indexes, unsigned int nb_indexes)
{
    unsigned int  part_hash[addr_size];
    unsigned int  index_hash;
    unsigned int  pos;
    unsigned int  i;
    uint8_t       byte;

    for (pos = 0; pos < addr_size; pos++)
    {
        part_hash[pos] = te_toeplitz_hash_data(toeplitz_hash_cache,
                                               &src_addr[pos], pos, 1) ^ hash;
    }

    for (i = 0; i < nb_indexes; i++)
    {
        for (pos = 0; pos < addr_size; pos++)
        {
            index_hash = (part_hash[pos] ^ indexes[i]) % reta_size;

            for (byte = 0; byte != UINT8_MAX; byte++)
            {
                if ((te_toeplitz_hash_data(toeplitz_hash_cache,
                                           &byte, pos, 1) % reta_size) ==
                    index_hash)
                {
                    src_addr[pos] = byte;

                    return 0;
                }
           }
        }
    }

    return TE_RC(TE_TAPI, TE_EINVAL);
}

/** The mapping list of RSS hash protocols values */
static const struct test_rss_hash_protos_mapping {
    const char               *proto_name;
    tarpc_rss_hash_protos_t   proto_val;
} rss_hash_proto_map[] = {
    { "IPV4",               TEST_ETH_RSS_IPV4},
    { "FRAG_IPV4",          TEST_ETH_RSS_FRAG_IPV4},
    { "NONFRAG_IPV4_TCP",   TEST_ETH_RSS_NONFRAG_IPV4_TCP},
    { "NONFRAG_IPV4_UDP",   TEST_ETH_RSS_NONFRAG_IPV4_UDP},
    { "NONFRAG_IPV4_SCTP",  TEST_ETH_RSS_NONFRAG_IPV4_SCTP},
    { "NONFRAG_IPV4_OTHER", TEST_ETH_RSS_NONFRAG_IPV4_OTHER},
    { "IPV6",               TEST_ETH_RSS_IPV6},
    { "FRAG_IPV6",          TEST_ETH_RSS_FRAG_IPV6},
    { "NONFRAG_IPV6_TCP",   TEST_ETH_RSS_NONFRAG_IPV6_TCP},
    { "NONFRAG_IPV6_UDP",   TEST_ETH_RSS_NONFRAG_IPV6_UDP},
    { "NONFRAG_IPV6_SCTP",  TEST_ETH_RSS_NONFRAG_IPV6_SCTP},
    { "NONFRAG_IPV6_OTHER", TEST_ETH_RSS_NONFRAG_IPV6_OTHER},
    { "L2_PAYLOAD",         TEST_ETH_RSS_L2_PAYLOAD},
    { "IPV6_EX",            TEST_ETH_RSS_IPV6_EX},
    { "IPV6_TCP_EX",        TEST_ETH_RSS_IPV6_TCP_EX},
    { "IPV6_UDP_EX",        TEST_ETH_RSS_IPV6_UDP_EX},
    { "PORT",               TEST_ETH_RSS_PORT},
    { "VXLAN",              TEST_ETH_RSS_VXLAN},
    { "GENEVE",             TEST_ETH_RSS_GENEVE},
    { "NVGRE",              TEST_ETH_RSS_NVGRE},
    { "IP",                 TEST_ETH_RSS_IP},
    { "TCP",                TEST_ETH_RSS_TCP},
    { "UDP",                TEST_ETH_RSS_UDP},
    { "SCTP",               TEST_ETH_RSS_SCTP},
    { "TUNNEL",             TEST_ETH_RSS_TUNNEL},
    { NULL,                 0}
};

tarpc_rss_hash_protos_t
test_rss_hash_protos_str2bitmask(const char *rss_hash_protos_str)
{
    tarpc_rss_hash_protos_t                   bitmask = 0;
    char                                    **rss_hash_protos_list;
    int                                       list_size;
    const struct test_rss_hash_protos_mapping *ptr;
    int                                       i;

    if (rss_hash_protos_str == NULL)
        TEST_STOP;

    list_size = test_split_param_list(rss_hash_protos_str,
                                      TEST_LIST_PARAM_SEPARATOR,
                                      &rss_hash_protos_list);
    if (list_size == 0)
        TEST_STOP;

    for (i = 0; i < list_size; i++)
    {
        for (ptr = rss_hash_proto_map;; ptr++)
        {
            if (strcmp(rss_hash_protos_list[i], ptr->proto_name) == 0)
            {
                bitmask |= ptr->proto_val;
                break;
            }

            if (ptr->proto_name == NULL)
            {
                free(rss_hash_protos_list[0]);
                free(rss_hash_protos_list);
                TEST_VERDICT("The hash function value should be "
                          "IP, TCP or UDP, but %d-th entry "
                          "is '%s'", i, rss_hash_protos_list[i]);
            }
        }
    }

    free(rss_hash_protos_list[0]);
    free(rss_hash_protos_list);

    return bitmask;
}

te_errno
test_rss_get_hash_by_pattern_unit(tarpc_rss_hash_protos_t  rss_hf,
                                  uint8_t                 *rss_key,
                                  const asn_value         *pattern,
                                  int                      pattern_unit_index,
                                  uint32_t                *hash_regular,
                                  uint32_t                *hash_symmetric)
{
    asn_value *pattern_unit;
    asn_value *pdus;
    te_errno   rc = 0;

    if (pattern == NULL)
        return TE_EINVAL;

    rc = asn_get_indexed(pattern, &pattern_unit, pattern_unit_index, "");
    if (rc != 0)
        return rc;

    rc = asn_get_subvalue(pattern_unit, &pdus, "pdus");
    if (rc != 0)
        return rc;

    return test_calc_hash_by_pdus_and_hf(rss_hf, rss_key, pdus,
                                         hash_regular, hash_symmetric);
}

te_errno
test_get_rss_hf_by_tmpl(asn_value               *tmpl,
                        tarpc_rss_hash_protos_t *hf)
{
    asn_value     *pdus;
    unsigned int   nb_pdus_o;
    asn_value    **pdus_o;
    asn_value     *pdu_ip4;
    asn_value     *pdu_ip6;
    asn_value     *pdu_tcp;
    asn_value     *pdu_udp;
    te_errno       rc;

    if (tmpl == NULL || hf == NULL)
        return TE_EINVAL;

    rc = asn_get_subvalue(tmpl, &pdus, "pdus");
    if (rc != 0)
        return rc;

    rc = tapi_tad_pdus_relist_outer_inner(pdus, &nb_pdus_o, &pdus_o,
                                          NULL, NULL);
    if (rc != 0)
        return rc;

    pdu_ip4 = asn_choice_array_look_up_value(nb_pdus_o, pdus_o, TE_PROTO_IP4);
    pdu_ip6 = asn_choice_array_look_up_value(nb_pdus_o, pdus_o, TE_PROTO_IP6);
    pdu_tcp = asn_choice_array_look_up_value(nb_pdus_o, pdus_o, TE_PROTO_TCP);
    pdu_udp = asn_choice_array_look_up_value(nb_pdus_o, pdus_o, TE_PROTO_UDP);

    free(pdus_o);

    if (pdu_ip4 == pdu_ip6)
        return TE_RC(TE_TAPI, TE_EINVAL);

    if (pdu_tcp != NULL && pdu_udp != NULL)
        return TE_RC(TE_TAPI, TE_EINVAL);

    *hf = 0;
    if (pdu_ip4 != NULL)
        *hf |= TEST_ETH_RSS_IPV4;
    else
        *hf |= TEST_ETH_RSS_IPV6;

    if (pdu_tcp != NULL)
        *hf |= (pdu_ip4 != NULL) ? TEST_ETH_RSS_NONFRAG_IPV4_TCP :
                                   TEST_ETH_RSS_NONFRAG_IPV6_TCP;

    if (pdu_udp != NULL)
        *hf |= (pdu_ip4 != NULL) ? TEST_ETH_RSS_NONFRAG_IPV4_UDP :
                                   TEST_ETH_RSS_NONFRAG_IPV6_UDP;

    return 0;
}

void
test_parse_mbuf_seg_ptrn(const char                 *seg_ptrn_str,
                         struct test_param_seg_ptrn *segmentation)
{
    te_bool                         err_occurred = FALSE;
    struct tarpc_pktmbuf_seg_group *seg_groups = NULL;
    unsigned int                    nb_seg_groups = 0;
    unsigned int                    n_seg_total = 0;
    const char                     *nptr;
    char                           *endptr;

    TAPI_ON_JMP(err_occurred = TRUE; goto out);

    CHECK_NOT_NULL(seg_ptrn_str);
    CHECK_NOT_NULL(segmentation);

    for (nptr = seg_ptrn_str, endptr = (char *)nptr;
         *endptr != '\0';
         nptr = endptr + 1)
    {
        char           *endptr_prev = endptr;
        unsigned long   token;

        token = strtoul(nptr, &endptr, 0);

        if (nptr == endptr)
        {
            if (nptr == seg_ptrn_str)
                break;

            TEST_VERDICT("Wrong parameter: segmentation = '%s'", seg_ptrn_str);
        }

        seg_groups = tapi_realloc(seg_groups,
                                  (nb_seg_groups + 1) * sizeof(*seg_groups));
        if ((*endptr_prev == ',') || (endptr_prev == seg_ptrn_str))
        {
            seg_groups[nb_seg_groups].num = 0;
            seg_groups[nb_seg_groups].len = 0;
        }

        if (*endptr == '*')
        {
            seg_groups[nb_seg_groups].num = (uint16_t)token;

            n_seg_total += seg_groups[nb_seg_groups].num;
            if (n_seg_total > UINT16_MAX)
                TEST_VERDICT("The pattern assumes too many mbuf segments\n"
                          "wrong parameter: segmentation = '%s'",
                          seg_ptrn_str);
        }

        if ((*endptr == ',') || (*endptr == '\0'))
            seg_groups[nb_seg_groups++].len = (uint16_t)token;
    }

    segmentation->seg_groups = seg_groups;
    segmentation->nb_seg_groups = nb_seg_groups;

out:
    if (err_occurred)
    {
        free(seg_groups);
        TEST_STOP;
    }

    TAPI_JMP_POP;
}

te_errno
test_check_iut_tst_link_status(
    const char *tst_ta, const char *tst_if_name, rcf_rpc_server *iut_rpcs,
    uint16_t iut_port_id, te_bool expected_status)
{
    struct tarpc_rte_eth_link   iut_eth_link;
    int                         tst_link_state;

    CHECK_RC(tapi_cfg_phy_state_get(tst_ta, tst_if_name, &tst_link_state));

    if (tst_link_state != (int)expected_status)
        TEST_VERDICT("%s is unexpected link status for tester interface",
                     (tst_link_state) ? "UP" : "DOWN");

    rpc_rte_eth_link_get(iut_rpcs, iut_port_id, &iut_eth_link);

    if (iut_eth_link.link_status != (uint16_t)expected_status)
        TEST_VERDICT("%s is unexpected link status for IUT interface",
                     (iut_eth_link.link_status) ? "UP" : "DOWN");

    return 0;
}

/** The mapping list of xstats names */
static const char *xstats_map[][TEST_MAX_NB_XSTAT_NAMES] = {
    {"rx_good_packets"},
    {"rx_good_bytes"},
    {"rx_octets"},
    {"rx_pkts"},
    {"rx_unicst_pkts", "rx_unicast_packets"},
    {"rx_multicst_pkts", "rx_multicast_packets"},
    {"rx_brdcst_pkts", "rx_broadcast_packets"},
    {"rx_le_64_pkts", "rx_size_64_packets"},
    {"rx_65_to_127_pkts", "rx_size_65_to_127_packets"},
    {"rx_128_to_255_pkts", "rx_size_128_to_255_packets"},
    {"rx_256_to_511_pkts", "rx_size_256_to_511_packets"},
    {"rx_512_to_1023_pkts", "rx_size_512_to_1023_packets"},
    {"rx_1024_to_15xx_pkts", "rx_size_1024_to_1522_packets"},
    {"rx_ge_15xx_pkts", "rx_size_1523_to_max_packets"},
    {"tx_good_packets"},
    {"tx_good_bytes"},
    {"tx_octets"},
    {"tx_pkts"},
    {"tx_unicst_pkts","tx_unicast_packets"},
    {"tx_multicst_pkts", "tx_multicast_packets"},
    {"tx_brdcst_pkts", "tx_broadcast_packets"},
    {"tx_le_64_pkts", "tx_size_64_packets"},
    {"tx_65_to_127_pkts", "tx_size_65_to_127_packets"},
    {"tx_128_to_255_pkts", "tx_size_128_to_255_packets"},
    {"tx_256_to_511_pkts", "tx_size_256_to_511_packets"},
    {"tx_512_to_1023_pkts", "tx_size_512_to_1023_packets"},
    {"tx_1024_to_15xx_pkts", "tx_size_1024_to_1522_packets"},
    {"tx_ge_15xx_pkts", "tx_size_1523_to_max_packets"}
};

int
test_get_xstat_by_name(const char *xstat_name,
                       struct tarpc_rte_eth_xstat_name *xstats_names,
                       struct tarpc_rte_eth_xstat *xstats,
                       unsigned nb_xstats, uint64_t *value)
{
    int         xstat = -1;
    unsigned    i;
    unsigned    j;

    for (i = 0; xstat < 0 && i < TE_ARRAY_LEN(xstats_map); i++)
    {
        for (j = 0; j < TE_ARRAY_LEN(xstats_map[i]); j++)
        {
            if (xstats_map[i][j] == NULL)
                break;

            if (strcmp(xstat_name, xstats_map[i][j]) == 0)
            {
                xstat = i;
                break;
            }
        }
    }

    if (xstat < 0)
        return -1;

    for (i = 0; i < nb_xstats; i++)
    {
        for (j = 0; j < TE_ARRAY_LEN(xstats_map[xstat]); j++)
        {
            if (xstats_map[xstat][j] == NULL)
                break;

            if (strcmp(xstats_map[xstat][j], xstats_names[i].name) == 0)
            {
                *value = xstats[i].value;
                return 0;
            }
        }
    }

    return -1;
}

static te_errno
test_pt_by_eth_pdu_choice(const asn_value *epc,
                          int             *l2_inner,
                          int             *l2_outer,
                          int             *l2_outer_alt)
{
    const asn_value *et = NULL;
    asn_tag_value    etc_tag = NDN_TAG_ETH_UNTAGGED;
    te_errno         rc = 0;

    rc = asn_get_child_value(epc, &et, PRIVATE, NDN_TAG_VLAN_TAGGED);
    if (rc == 0)
    {
        asn_value *etc = NULL;

        rc = asn_get_choice_value(et, &etc, NULL, &etc_tag);
        if (rc != 0)
            return rc;
    }

    rc = (rc == TE_EASNINCOMPLVAL) ? 0 : rc;
    if (rc != 0)
        return rc;

    switch (etc_tag) {
        case NDN_TAG_ETH_UNTAGGED:
            *l2_inner = TARPC_RTE_PTYPE_INNER_L2_ETHER;

            if (*l2_outer == 0)
                *l2_outer = TARPC_RTE_PTYPE_L2_ETHER;
            else
                *l2_outer_alt = TARPC_RTE_PTYPE_L2_ETHER;
            break;

        case NDN_TAG_VLAN_TAG_HEADER:
            *l2_inner = TARPC_RTE_PTYPE_INNER_L2_ETHER_VLAN;

            if (*l2_outer == 0)
                *l2_outer = TARPC_RTE_PTYPE_L2_ETHER_VLAN;
            else
                *l2_outer_alt = TARPC_RTE_PTYPE_L2_ETHER_VLAN;
            break;

        case NDN_TAG_VLAN_DOUBLE_TAGGED:
            *l2_inner = TARPC_RTE_PTYPE_INNER_L2_ETHER_QINQ;

            if (*l2_outer == 0)
                *l2_outer = TARPC_RTE_PTYPE_L2_ETHER_QINQ;
            else
                *l2_outer_alt = TARPC_RTE_PTYPE_L2_ETHER_QINQ;
            break;

        default:
            rc = TE_EINVAL;
            break;
    }

    return rc;
}

static te_errno
test_pt_by_ip4_pdu_choice(const asn_value *ipc,
                          te_bool          inner_ip4_ext_supported,
                          te_bool          outer_ip4_ext_supported,
                          int             *l3_inner,
                          int             *l3_outer,
                          int             *l4_inner,
                          int             *l4_outer)
{
    int32_t  h_len;
    int32_t  df_flag;
    int32_t  mf_flag;
    int32_t  offset;
    te_errno rc = 0;

    rc = asn_read_int32(ipc, &h_len, "h-length.#plain");
    if (rc != 0)
        return rc;

    if (inner_ip4_ext_supported)
    {
        if (h_len > 5)
            *l3_inner = TARPC_RTE_PTYPE_INNER_L3_IPV4_EXT;
        else
            *l3_inner = TARPC_RTE_PTYPE_INNER_L3_IPV4;
    }
    else
    {
        *l3_inner = TARPC_RTE_PTYPE_INNER_L3_IPV4_EXT_UNKNOWN;
    }

    if (outer_ip4_ext_supported)
    {
        if (h_len > 5)
            *l3_outer = TARPC_RTE_PTYPE_L3_IPV4_EXT;
        else
            *l3_outer = TARPC_RTE_PTYPE_L3_IPV4;
    }
    else
    {
        *l3_outer = TARPC_RTE_PTYPE_L3_IPV4_EXT_UNKNOWN;
    }

    rc = asn_read_int32(ipc, &df_flag, "dont-frag.#plain");
    if (rc != 0)
        return rc;

    rc = asn_read_int32(ipc, &mf_flag, "more-frags.#plain");
    if (rc != 0)
        return rc;

    rc = asn_read_int32(ipc, &offset, "frag-offset.#plain");
    if (rc != 0)
        return rc;

    if ((df_flag == 0) && ((mf_flag != 0) || (offset != 0)))
    {
        *l4_inner = TARPC_RTE_PTYPE_INNER_L4_FRAG;
        *l4_outer = TARPC_RTE_PTYPE_L4_FRAG;
    }

    return 0;
}

static te_errno
test_pt_by_ip6_pdu_choice(const asn_value *ipc,
                          te_bool          inner_ip6_ext_supported,
                          te_bool          outer_ip6_ext_supported,
                          int             *l3_inner,
                          int             *l3_outer)
{
    int32_t  next_header;
    te_errno rc = 0;

    rc = asn_read_int32(ipc, &next_header, "next-header.#plain");
    if (rc != 0)
        return rc;

    if (inner_ip6_ext_supported)
    {
        if ((next_header == IPPROTO_UDP) || (next_header == IPPROTO_TCP))
            *l3_inner = TARPC_RTE_PTYPE_INNER_L3_IPV6;
        else
            *l3_inner = TARPC_RTE_PTYPE_INNER_L3_IPV6_EXT;
    }
    else
    {
        *l3_inner = TARPC_RTE_PTYPE_INNER_L3_IPV6_EXT_UNKNOWN;
    }

    if (outer_ip6_ext_supported)
    {
        if ((next_header == IPPROTO_UDP) || (next_header == IPPROTO_TCP))
            *l3_outer = TARPC_RTE_PTYPE_L3_IPV6;
        else
            *l3_outer = TARPC_RTE_PTYPE_L3_IPV6_EXT;
    }
    else
    {
        *l3_outer = TARPC_RTE_PTYPE_L3_IPV6_EXT_UNKNOWN;
    }

    return 0;
}

uint32_t
test_ptype_from_pattern(const asn_value *pattern,
                        uint32_t        *supp,
                        unsigned int     supp_count)
{
    te_bool        inner_ip4_ext_supported = TRUE;
    te_bool        outer_ip4_ext_supported = TRUE;
    te_bool        inner_ip6_ext_supported = TRUE;
    te_bool        outer_ip6_ext_supported = TRUE;
    asn_value     *pdus = NULL;
    int            pdus_count;
    asn_value     *pdu = NULL;
    asn_value     *pdu_choice = NULL;
    asn_tag_value  pdu_choice_tag;
    asn_value     *tunnel_pdu_choice = NULL;
    asn_tag_value  tunnel_pdu_choice_tag;
    int            l2_inner = 0, l2_outer = 0;
    int            l2_outer_alt = 0;
    int            l3_inner = 0, l3_outer = 0;
    int            l4_inner = 0, l4_outer = 0;
    uint32_t       pt_inner = 0;
    uint32_t       pt_tunnel = 0;
    uint32_t       pt_outer = 0;
    uint32_t       pt = 0;
    te_bool        pt_inner_l2_supported = FALSE;
    te_bool        pt_inner_l3_supported = FALSE;
    te_bool        pt_inner_l4_supported = FALSE;
    te_bool        pt_tunnel_supported = FALSE;
    te_bool        pt_outer_l2_supported = FALSE;
    te_bool        pt_outer_l3_supported = FALSE;
    te_bool        pt_outer_l4_supported = FALSE;
    te_bool        pt_outer_l2_ether_supported = FALSE;
    te_bool        pt_outer_l2_vlan_supported = FALSE;
    te_bool        pt_outer_l2_qinq_supported = FALSE;
    te_errno       rc = 0;
    int            i;

    CHECK_NOT_NULL(pattern);
    CHECK_NOT_NULL(supp);

    for (i = 0; (unsigned int)i < supp_count; ++i)
    {
        if (supp[i] == (TARPC_RTE_PTYPE_INNER_L3_IPV4_EXT_UNKNOWN <<
                        TARPC_RTE_PTYPE_INNER_L3_OFFSET))
            inner_ip4_ext_supported = FALSE;
        else if (supp[i] == (TARPC_RTE_PTYPE_L3_IPV4_EXT_UNKNOWN <<
                             TARPC_RTE_PTYPE_L3_OFFSET))
            outer_ip4_ext_supported = FALSE;
        else if (supp[i] == (TARPC_RTE_PTYPE_INNER_L3_IPV6_EXT_UNKNOWN <<
                        TARPC_RTE_PTYPE_INNER_L3_OFFSET))
            inner_ip6_ext_supported = FALSE;
        else if (supp[i] == (TARPC_RTE_PTYPE_L3_IPV6_EXT_UNKNOWN <<
                             TARPC_RTE_PTYPE_L3_OFFSET))
            outer_ip6_ext_supported = FALSE;
    }

    /* It is assumed that this API is used for single-packet patterns only */
    CHECK_RC(asn_get_subvalue(pattern, &pdus, "0.pdus"));

    pdus_count = asn_get_length(pdus, "");
    rc = (pdus_count > 0) ? 0 : TE_EINVAL;
    CHECK_RC(rc);

    for (i = 0; i < pdus_count; ++i)
    {
        CHECK_RC(asn_get_indexed(pdus, &pdu, i, ""));
        CHECK_RC(asn_get_choice_value(pdu, &pdu_choice, NULL, &pdu_choice_tag));

        switch (pdu_choice_tag) {
            case TE_PROTO_ETH:
                CHECK_RC(test_pt_by_eth_pdu_choice(pdu_choice, &l2_inner,
                                                   &l2_outer, &l2_outer_alt));
                break;

            case TE_PROTO_ARP:
                l2_inner = TARPC_RTE_PTYPE_INNER_L2_ETHER;
                l2_outer = TARPC_RTE_PTYPE_L2_ETHER_ARP;
                break;

            case TE_PROTO_IP4:
                CHECK_RC(test_pt_by_ip4_pdu_choice(pdu_choice,
                                                   inner_ip4_ext_supported,
                                                   outer_ip4_ext_supported,
                                                   &l3_inner, &l3_outer,
                                                   &l4_inner, &l4_outer));
                break;

            case TE_PROTO_IP6:
                CHECK_RC(test_pt_by_ip6_pdu_choice(pdu_choice,
                                                   inner_ip6_ext_supported,
                                                   outer_ip6_ext_supported,
                                                   &l3_inner, &l3_outer));
                break;

            case TE_PROTO_UDP:
                l4_inner = TARPC_RTE_PTYPE_INNER_L4_UDP;
                l4_outer = TARPC_RTE_PTYPE_L4_UDP;
                break;

            case TE_PROTO_TCP:
                l4_inner = TARPC_RTE_PTYPE_INNER_L4_TCP;
                l4_outer = TARPC_RTE_PTYPE_L4_TCP;
                break;

            case TE_PROTO_VXLAN:
            case TE_PROTO_GENEVE:
            case TE_PROTO_GRE:
                tunnel_pdu_choice_tag = pdu_choice_tag;
                tunnel_pdu_choice = pdu_choice;

                pt_inner |= (l2_inner << TARPC_RTE_PTYPE_INNER_L2_OFFSET);
                pt_inner |= (l3_inner << TARPC_RTE_PTYPE_INNER_L3_OFFSET);
                pt_inner |= (l4_inner << TARPC_RTE_PTYPE_INNER_L4_OFFSET);

                l2_outer = 0;
                l2_outer_alt = 0;
                l3_outer = 0;
                l4_outer = 0;
                break;

            default:
                break;
        }
    }

    switch (tunnel_pdu_choice_tag) {
        case TE_PROTO_VXLAN:
            pt_tunnel = TARPC_RTE_PTYPE_TUNNEL_VXLAN;
            break;

        case TE_PROTO_GENEVE:
            pt_tunnel = TARPC_RTE_PTYPE_TUNNEL_GENEVE;
            break;

        case TE_PROTO_GRE:
            {
                asn_value *key_nvgre = NULL;

                rc = asn_get_subvalue(tunnel_pdu_choice, &key_nvgre,
                                      "opt-key.#nvgre");
                if (rc == 0)
                    pt_tunnel = TARPC_RTE_PTYPE_TUNNEL_NVGRE;
                else if (rc == TE_EASNINCOMPLVAL)
                    pt_tunnel = TARPC_RTE_PTYPE_TUNNEL_GRE;

                rc = (rc == TE_EASNINCOMPLVAL) ? 0 : rc;
                CHECK_RC(rc);
            }
            break;

        default:
            pt_tunnel = TARPC_RTE_PTYPE_TUNNEL_UNKNOWN;
            break;
    }

    pt_tunnel <<= TARPC_RTE_PTYPE_TUNNEL_OFFSET;

    pt_outer = 0;
    pt_outer |= (l2_outer << TARPC_RTE_PTYPE_L2_OFFSET);
    pt_outer |= (l3_outer << TARPC_RTE_PTYPE_L3_OFFSET);
    pt_outer |= (l4_outer << TARPC_RTE_PTYPE_L4_OFFSET);

    pt = pt_inner | pt_tunnel | pt_outer;

    for (i = 0; (unsigned int)i < supp_count; i++)
    {
        int value_vlan = TARPC_RTE_PTYPE_L2_ETHER_VLAN;
        int value_qinq = TARPC_RTE_PTYPE_L2_ETHER_QINQ;

        if (supp[i] == (pt & TARPC_RTE_PTYPE_INNER_L2_MASK))
            pt_inner_l2_supported = TRUE;
        else if (supp[i] == (pt & TARPC_RTE_PTYPE_INNER_L3_MASK))
            pt_inner_l3_supported = TRUE;
        else if (supp[i] == (pt & TARPC_RTE_PTYPE_INNER_L4_MASK))
            pt_inner_l4_supported = TRUE;
        else if (supp[i] == (pt & TARPC_RTE_PTYPE_TUNNEL_MASK))
            pt_tunnel_supported = TRUE;
        else if (supp[i] == (pt & TARPC_RTE_PTYPE_L2_MASK))
            pt_outer_l2_supported = TRUE;
        else if (supp[i] == (pt & TARPC_RTE_PTYPE_L3_MASK))
            pt_outer_l3_supported = TRUE;
        else if (supp[i] == (pt & TARPC_RTE_PTYPE_L4_MASK))
            pt_outer_l4_supported = TRUE;

        if (supp[i] == (TARPC_RTE_PTYPE_L2_ETHER << TARPC_RTE_PTYPE_L2_OFFSET))
            pt_outer_l2_ether_supported = TRUE;

        if (supp[i] == (uint32_t)(value_vlan << TARPC_RTE_PTYPE_L2_OFFSET))
            pt_outer_l2_vlan_supported = TRUE;

        if (supp[i] == (uint32_t)(value_qinq << TARPC_RTE_PTYPE_L2_OFFSET))
            pt_outer_l2_qinq_supported = TRUE;
    }

    if (((pt & TARPC_RTE_PTYPE_INNER_L2_MASK) != 0) &&
        !pt_inner_l2_supported)
    {
        WARN_VERDICT("Inner layer 2 packet type is unsupported by the driver");
        pt &= ~TARPC_RTE_PTYPE_INNER_L2_MASK;
    }

    if (((pt & TARPC_RTE_PTYPE_INNER_L3_MASK) != 0) &&
        !pt_inner_l3_supported)
    {
        WARN_VERDICT("Inner layer 3 packet type is unsupported by the driver");
        pt &= ~TARPC_RTE_PTYPE_INNER_L3_MASK;
    }

    if (((pt & TARPC_RTE_PTYPE_INNER_L4_MASK) != 0) &&
        !pt_inner_l4_supported)
    {
        WARN_VERDICT("Inner layer 4 packet type is unsupported by the driver");
        pt &= ~TARPC_RTE_PTYPE_INNER_L4_MASK;
    }

    if (((pt & TARPC_RTE_PTYPE_TUNNEL_MASK) != 0) && !pt_tunnel_supported)
    {
        WARN_VERDICT("Tunnel packet type is unsupported by the driver");
        pt &= ~TARPC_RTE_PTYPE_TUNNEL_MASK;
    }

    if (((pt & TARPC_RTE_PTYPE_L2_MASK) != 0) && !pt_outer_l2_supported)
    {
        WARN_VERDICT("Layer 2 packet type is unsupported by the driver");

        /*
         * If the unsupported outer L2 PT is ARP, one may expect that
         * the PMD will set one of the following: QINQ, VLAN, ETHER
         * depending on how many VLAN tags are in the outer header.
         *
         * In any case, classification of tags may also be limited.
         */

        if (l2_outer_alt == 0)
            l2_outer_alt = l2_outer;

        if (l2_outer_alt == TARPC_RTE_PTYPE_L2_ETHER_QINQ &&
            !pt_outer_l2_qinq_supported)
            l2_outer_alt = TARPC_RTE_PTYPE_L2_ETHER_VLAN;

        if (l2_outer_alt == TARPC_RTE_PTYPE_L2_ETHER_VLAN &&
            !pt_outer_l2_vlan_supported)
            l2_outer_alt = TARPC_RTE_PTYPE_L2_ETHER;

        if (l2_outer_alt == TARPC_RTE_PTYPE_L2_ETHER &&
            !pt_outer_l2_ether_supported)
            l2_outer_alt = 0;

        pt &= ~TARPC_RTE_PTYPE_L2_MASK;
        pt |= (uint32_t)(l2_outer_alt << TARPC_RTE_PTYPE_L2_OFFSET);
    }

    if (((pt & TARPC_RTE_PTYPE_L3_MASK) != 0) && !pt_outer_l3_supported)
    {
        WARN_VERDICT("Layer 3 packet type is unsupported by the driver");
        pt &= ~TARPC_RTE_PTYPE_L3_MASK;
    }

    if (((pt & TARPC_RTE_PTYPE_L4_MASK) != 0) && !pt_outer_l4_supported)
    {
        WARN_VERDICT("Layer 4 packet type is unsupported by the driver");
        pt &= ~TARPC_RTE_PTYPE_L4_MASK;
    }

    return pt;
}

#define PT_FLAG(_g, _v) \
    { TARPC_RTE_PTYPE_##_g##_##_v, TARPC_RTE_PTYPE_##_g##_OFFSET, #_g "_" #_v }

static const struct test_ptype_desc {
    int   value;
    int   offset;
    char *name;
} test_ptype_map[] = {
    PT_FLAG(L2, UNKNOWN),
    PT_FLAG(L2, ETHER),
    PT_FLAG(L2, ETHER_TIMESYNC),
    PT_FLAG(L2, ETHER_ARP),
    PT_FLAG(L2, ETHER_LLDP),
    PT_FLAG(L2, ETHER_NSH),
    PT_FLAG(L2, ETHER_VLAN),
    PT_FLAG(L2, ETHER_QINQ),

    PT_FLAG(L3, UNKNOWN),
    PT_FLAG(L3, IPV4),
    PT_FLAG(L3, IPV4_EXT),
    PT_FLAG(L3, IPV6),
    PT_FLAG(L3, IPV4_EXT_UNKNOWN),
    PT_FLAG(L3, IPV6_EXT),
    PT_FLAG(L3, IPV6_EXT_UNKNOWN),

    PT_FLAG(L4, UNKNOWN),
    PT_FLAG(L4, TCP),
    PT_FLAG(L4, UDP),
    PT_FLAG(L4, FRAG),
    PT_FLAG(L4, SCTP),
    PT_FLAG(L4, ICMP),
    PT_FLAG(L4, NONFRAG),

    PT_FLAG(TUNNEL, UNKNOWN),
    PT_FLAG(TUNNEL, IP),
    PT_FLAG(TUNNEL, GRE),
    PT_FLAG(TUNNEL, VXLAN),
    PT_FLAG(TUNNEL, NVGRE),
    PT_FLAG(TUNNEL, GENEVE),
    PT_FLAG(TUNNEL, GRENAT),
    PT_FLAG(TUNNEL, GTPC),
    PT_FLAG(TUNNEL, GTPU),
    PT_FLAG(TUNNEL, ESP),

    PT_FLAG(INNER_L2, UNKNOWN),
    PT_FLAG(INNER_L2, ETHER),
    PT_FLAG(INNER_L2, ETHER_VLAN),
    PT_FLAG(INNER_L2, ETHER_QINQ),

    PT_FLAG(INNER_L3, UNKNOWN),
    PT_FLAG(INNER_L3, IPV4),
    PT_FLAG(INNER_L3, IPV4_EXT),
    PT_FLAG(INNER_L3, IPV6),
    PT_FLAG(INNER_L3, IPV4_EXT_UNKNOWN),
    PT_FLAG(INNER_L3, IPV6_EXT),
    PT_FLAG(INNER_L3, IPV6_EXT_UNKNOWN),

    PT_FLAG(INNER_L4, UNKNOWN),
    PT_FLAG(INNER_L4, TCP),
    PT_FLAG(INNER_L4, UDP),
    PT_FLAG(INNER_L4, FRAG),
    PT_FLAG(INNER_L4, SCTP),
    PT_FLAG(INNER_L4, ICMP),
    PT_FLAG(INNER_L4, NONFRAG)
};

#undef PT_FLAG

te_bool
test_check_packet_ptype(uint32_t pt_flag_expected,
                        int      pt_value_detected,
                        int      pt_flag_offset)
{
    uint32_t pt_flag_detected = (uint32_t)(pt_value_detected << pt_flag_offset);
    te_bool  mismatch_detected = FALSE;

    if (pt_flag_detected != pt_flag_expected)
    {
        char          bad_type[] = "BAD_TYPE";
        char         *name_detected = bad_type;
        char         *name_expected = bad_type;
        unsigned int  i;

        for (i = 0; i < TE_ARRAY_LEN(test_ptype_map); ++i)
        {
            const struct test_ptype_desc *desc = &test_ptype_map[i];

            if (desc->offset == pt_flag_offset)
            {
                uint32_t pt_flag_i = desc->value << desc->offset;

                if (pt_flag_detected == pt_flag_i)
                    name_detected = desc->name;

                if (pt_flag_expected == pt_flag_i)
                    name_expected = desc->name;
            }
        }

        WARN_VERDICT("Packet type mismatch: got %s, must be %s",
                     name_detected, name_expected);

        mismatch_detected = TRUE;
    }

    return mismatch_detected;
}

static int
test_masked_array_cmp(uint8_t *data_1, uint8_t *data_2,
                      uint8_t *mask, size_t size)
{
    unsigned int i;

    for (i = 0; i < size; i++)
        if ((data_1[i] & mask[i]) != (data_2[i] & mask[i]))
            return -1;

    return 0;
}

te_errno
test_dev_info_cmp(struct tarpc_rte_eth_dev_info *dev_info_1,
                  struct tarpc_rte_eth_dev_info *dev_info_2)
{
    struct tarpc_rte_eth_dev_info   mask;

    memset(&mask, 0xff, sizeof(mask));
    mask.driver_name = NULL;

    if (strcmp(dev_info_2->driver_name, dev_info_1->driver_name) != 0)
    {
        ERROR("Fields driver_name don't match");
        return TE_EINVAL;
    }

    if (test_masked_array_cmp((uint8_t *)dev_info_1, (uint8_t *)dev_info_2,
                              (uint8_t *)&mask, sizeof(mask)) == 0)
        return 0;

#define TEST_CMP_DEV_INFO_FIELD(_field) \
    if (dev_info_2->_field != dev_info_1->_field)   \
    {                                               \
        ERROR("Fields "#_field" don't match");      \
        return TE_EINVAL;                           \
    }                                               \


#define TEST_CMP_DEV_INFO_DESC_LIM_FIELD(_field) \
    if (dev_info_2->rx_desc_lim._field != dev_info_1->rx_desc_lim._field)   \
    {                                                                       \
        ERROR("rx_desc_lim's fields "#_field" don't match");                \
        return TE_EINVAL;                                                   \
    }                                                                       \
    if (dev_info_2->tx_desc_lim._field != dev_info_1->tx_desc_lim._field)   \
    {                                                                       \
        ERROR("tx_desc_lim's fields "#_field" don't match");                \
        return TE_EINVAL;                                                   \
    }

#define TEST_CMP_DEV_INFO_CONF_FIELD(_conf, _field) \
    if (dev_info_2->_conf._field != dev_info_1->_conf._field)   \
    {                                                           \
        ERROR(#_conf"'s fields "#_field" don't match");         \
        return TE_EINVAL;                                       \
    }

#define TEST_CMP_DEV_INFO_CONF_THRESH_FIELD(_conf, _field, _thresh_field)   \
    if (dev_info_2->_conf._field._thresh_field !=                           \
        dev_info_1->_conf._field._thresh_field)                             \
    {                                                                       \
        ERROR(#_field"'s fields "#_thresh_field" in "#_conf" don't match"); \
        return TE_EINVAL;                                                   \
    }

    TEST_CMP_DEV_INFO_FIELD(if_index);
    TEST_CMP_DEV_INFO_FIELD(min_rx_bufsize);
    TEST_CMP_DEV_INFO_FIELD(max_rx_pktlen);
    TEST_CMP_DEV_INFO_FIELD(max_rx_queues);
    TEST_CMP_DEV_INFO_FIELD(max_tx_queues);
    TEST_CMP_DEV_INFO_FIELD(max_mac_addrs);
    TEST_CMP_DEV_INFO_FIELD(max_hash_mac_addrs);
    TEST_CMP_DEV_INFO_FIELD(max_vfs);
    TEST_CMP_DEV_INFO_FIELD(max_vmdq_pools);
    TEST_CMP_DEV_INFO_FIELD(rx_offload_capa);
    TEST_CMP_DEV_INFO_FIELD(tx_offload_capa);
    TEST_CMP_DEV_INFO_FIELD(reta_size);
    TEST_CMP_DEV_INFO_FIELD(hash_key_size);
    TEST_CMP_DEV_INFO_FIELD(flow_type_rss_offloads);
    TEST_CMP_DEV_INFO_FIELD(vmdq_queue_base);
    TEST_CMP_DEV_INFO_FIELD(vmdq_queue_num);
    TEST_CMP_DEV_INFO_FIELD(vmdq_pool_base);
#undef TEST_CMP_DEV_INFO_FIELD

    TEST_CMP_DEV_INFO_DESC_LIM_FIELD(nb_max);
    TEST_CMP_DEV_INFO_DESC_LIM_FIELD(nb_min);
    TEST_CMP_DEV_INFO_DESC_LIM_FIELD(nb_align);
#undef TEST_CMP_DEV_INFO_DESC_LIM_FIELD

    TEST_CMP_DEV_INFO_CONF_THRESH_FIELD(default_rxconf, rx_thresh, pthresh);
    TEST_CMP_DEV_INFO_CONF_THRESH_FIELD(default_rxconf, rx_thresh, hthresh);
    TEST_CMP_DEV_INFO_CONF_THRESH_FIELD(default_rxconf, rx_thresh, wthresh);
    TEST_CMP_DEV_INFO_CONF_FIELD(default_rxconf, rx_free_thresh);
    TEST_CMP_DEV_INFO_CONF_FIELD(default_rxconf, rx_drop_en);
    TEST_CMP_DEV_INFO_CONF_FIELD(default_rxconf, rx_deferred_start);
    TEST_CMP_DEV_INFO_CONF_THRESH_FIELD(default_txconf, tx_thresh, pthresh);
    TEST_CMP_DEV_INFO_CONF_THRESH_FIELD(default_txconf, tx_thresh, hthresh);
    TEST_CMP_DEV_INFO_CONF_THRESH_FIELD(default_txconf, tx_thresh, wthresh);
    TEST_CMP_DEV_INFO_CONF_FIELD(default_txconf, tx_rs_thresh);
    TEST_CMP_DEV_INFO_CONF_FIELD(default_txconf, tx_free_thresh);
    TEST_CMP_DEV_INFO_CONF_FIELD(default_txconf, txq_flags);
    TEST_CMP_DEV_INFO_CONF_FIELD(default_txconf, tx_deferred_start);
#undef TEST_CMP_DEV_INFO_CONF_FIELD
#undef TEST_CMP_DEV_INFO_CONF_THRESH_FIELD

    ERROR("Device informations are different, but detailed "
          "analysis has found no differences");

    return TE_EINVAL;
}
te_errno
test_change_tmpl_ip_src_addr_by_queue_nb(
                            asn_value *tmpl,
                            uint16_t queue,
                            uint64_t reta_size,
                            struct tarpc_rte_eth_rss_reta_entry64 *reta_conf,
                            struct tarpc_rte_eth_rss_conf *rss_conf)
{
    unsigned int                            reta_indxs[reta_size];
    unsigned int                            nb_reta_indxs = 0;
    uint8_t                                 src_addr[sizeof(struct in6_addr)];
    unsigned int                            i;
    te_errno                                rc;
    asn_value                              *pdus = NULL;
    asn_value                              *ip_pdu = NULL;
    size_t                                  addr_len;
    uint32_t                                packet_hash;
    te_toeplitz_hash_cache                 *hash_cache;

    CHECK_RC(asn_get_subvalue(tmpl, &pdus, "pdus"));

    ip_pdu = asn_find_child_choice_value(pdus, TE_PROTO_IP4);
    if (ip_pdu != NULL)
    {
        addr_len = sizeof(struct in_addr);
    }
    else
    {
        ip_pdu = asn_find_child_choice_value(pdus, TE_PROTO_IP6);
        CHECK_NOT_NULL(ip_pdu);

        addr_len = sizeof(struct in6_addr);
    }

    CHECK_RC(asn_read_value_field(ip_pdu, src_addr, &addr_len,
                                  "src-addr.#plain"));

    memset(reta_indxs, 0, reta_size * sizeof(*reta_indxs));
    for (i = 0; i < reta_size; i++)
    {
        if (reta_conf[i / RPC_RTE_RETA_GROUP_SIZE].reta[
                i % RPC_RTE_RETA_GROUP_SIZE] == queue)
            reta_indxs[nb_reta_indxs++] = i;
    }

    CHECK_RC(test_calc_hash_by_tmpl_and_hf(
                rss_conf->rss_hf, rss_conf->rss_key.rss_key_val,
                tmpl, &packet_hash, NULL));

    hash_cache = te_toeplitz_cache_init(rss_conf->rss_key.rss_key_val);
    rc = test_change_src_addr_by_reta_index(hash_cache, packet_hash,
                                            src_addr, addr_len, reta_size,
                                            reta_indxs, nb_reta_indxs);
    te_toeplitz_hash_fini(hash_cache);

    if (rc != 0)
        TEST_VERDICT("RSS configuration does not allow to receive"
                  "a packet on the %u queue", queue);

    CHECK_RC(asn_write_value_field(ip_pdu, src_addr, addr_len,
                                   "src-addr.#plain"));

    return 0;
}

static void
test_pdus_reverse(asn_value *pdus)
{
    unsigned int i,j;

    for (i = 0, j = (unsigned int)pdus->len - 1; i < j; i++, j--)
    {
        asn_value *tmp = pdus->data.array[i];

        pdus->data.array[i] = pdus->data.array[j];
        pdus->data.array[j] = tmp;
    }
}

static te_errno
test_get_flow_match_fields(asn_value *pdu, asn_value *pdu_field,
                           uint32_t *matches, te_bool is_ifrm)
{
    asn_tag_value pdu_tag;
    asn_tag_value pdu_field_tag;

#define TEST_CHECK_FLOW_MATCH_FIELD(_ndn_tag, _field)     \
    do {                                                        \
        if (pdu_field_tag == NDN_TAG_##_ndn_tag)                \
                *matches |= is_ifrm ? TEST_TMPL_IFRM_##_field : \
                                      TEST_TMPL_##_field;       \
    } while (0)

    pdu_tag = asn_get_tag(pdu);
    pdu_field_tag = asn_get_tag(pdu_field);

    if (strcmp(asn_get_name(pdu), "tagged") == 0)
    {
        TEST_CHECK_FLOW_MATCH_FIELD(VLAN_TAG_HEADER_VID, OUTER_VID);
        return 0;
    }
    else if (strcmp(asn_get_name(pdu), "outer") == 0)
    {
        TEST_CHECK_FLOW_MATCH_FIELD(VLAN_HEADER_VID, OUTER_VID);
        return 0;
    }
    else if (strcmp(asn_get_name(pdu), "inner") == 0)
    {
        TEST_CHECK_FLOW_MATCH_FIELD(VLAN_HEADER_VID, INNER_VID);
        return 0;
    }
    else if (strcmp(asn_get_name(pdu), "nvgre") == 0)
    {
        if (pdu_field_tag == NDN_TAG_GRE_OPT_KEY_NVGRE_VSID)
            *matches |= TEST_TMPL_VNI_OR_VSID;
        return 0;
    }

    switch (pdu_tag)
    {
        case TE_PROTO_TCP:
            TEST_CHECK_FLOW_MATCH_FIELD(TCP_SRC_PORT, SRC_PORT);
            TEST_CHECK_FLOW_MATCH_FIELD(TCP_DST_PORT, DST_PORT);
            break;

        case TE_PROTO_UDP:
            TEST_CHECK_FLOW_MATCH_FIELD(UDP_SRC_PORT, SRC_PORT);
            TEST_CHECK_FLOW_MATCH_FIELD(UDP_DST_PORT, DST_PORT);
            break;

        case TE_PROTO_IP4:
            TEST_CHECK_FLOW_MATCH_FIELD(IP4_SRC_ADDR, SRC_HOST);
            TEST_CHECK_FLOW_MATCH_FIELD(IP4_DST_ADDR, DST_HOST);
            TEST_CHECK_FLOW_MATCH_FIELD(IP4_PROTOCOL, IP_PROTO);
            break;

        case TE_PROTO_IP6:
            TEST_CHECK_FLOW_MATCH_FIELD(IP6_SRC_ADDR, SRC_HOST);
            TEST_CHECK_FLOW_MATCH_FIELD(IP6_DST_ADDR, DST_HOST);
            TEST_CHECK_FLOW_MATCH_FIELD(IP6_NEXT_HEADER, IP_PROTO);
            break;

        case TE_PROTO_ETH:
            if (pdu_field_tag == NDN_TAG_802_3_DST)
            {
                size_t size = ETHER_ADDR_LEN;
                uint8_t mask[ETHER_ADDR_LEN];
                uint8_t addr[ETHER_ADDR_LEN];
                const uint8_t ig_mask[ETHER_ADDR_LEN] = {
                    0x01, 0x00, 0x00, 0x00, 0x00, 0x00
                };
                int rc;

                rc = asn_read_value_field(pdu_field, mask, &size,
                                          "#range.mask");
                if (rc != 0 && rc != TE_EASNOTHERCHOICE)
                    return rc;

                if ((rc == TE_EASNOTHERCHOICE) ||
                    (memcmp(mask, ig_mask, ETHER_ADDR_LEN) != 0))
                {
                    TEST_CHECK_FLOW_MATCH_FIELD(802_3_DST, DST_MAC);
                }
                else
                {
                    CHECK_RC(asn_read_value_field(pdu_field, addr, &size,
                                                  "#range.first"));
                    if ((addr[0] & ig_mask[0]) == 0)
                        TEST_CHECK_FLOW_MATCH_FIELD(802_3_DST, UN_UCAST_DST);
                    else
                        TEST_CHECK_FLOW_MATCH_FIELD(802_3_DST, UN_MCAST_DST);
                }
            }
            TEST_CHECK_FLOW_MATCH_FIELD(802_3_SRC, SRC_MAC);
            TEST_CHECK_FLOW_MATCH_FIELD(802_3_LENGTH_TYPE, ETHER_TYPE);
            break;

        case TE_PROTO_VXLAN:
            if (pdu_field_tag == NDN_TAG_VXLAN_VNI)
                *matches |= TEST_TMPL_VNI_OR_VSID;
            break;

        case TE_PROTO_GENEVE:
            if (pdu_field_tag == NDN_TAG_GENEVE_VNI)
                *matches |= TEST_TMPL_VNI_OR_VSID;
            break;
    }
#undef TEST_CHECK_FLOW_MATCH_FIELD

    return 0;
}

static te_errno
test_pdu_change_range_fields_to_plain(asn_value *pdu, uint32_t *matches,
                                      te_bool is_ifrm)
{
    unsigned int i;
    int rc;

    for (i = 0; i < (unsigned int)pdu->len; i++)
    {
        asn_value *pdu_field = pdu->data.array[i];
        asn_value *pdu_field_sub_val;
        asn_value *pdu_first;
        asn_value *pdu_plain;
        asn_tag_value pdu_field_sub_tag;

        if (pdu_field == NULL)
            continue;

        rc = asn_get_choice_value(pdu_field, &pdu_field_sub_val,
                                  NULL, &pdu_field_sub_tag);
        if (rc != 0)
            return rc;

        if (pdu_field_sub_tag == NDN_DU_RANGE ||
            pdu_field_sub_tag == NDN_DU_PLAIN)
        {
            test_get_flow_match_fields(pdu, pdu_field, matches, is_ifrm);
        }

        if (pdu_field_sub_tag != NDN_DU_RANGE)
            continue;

        rc = asn_get_subvalue(pdu_field_sub_val, &pdu_first, "first");
        if (rc != 0)
            return rc;

        pdu_plain = asn_copy_value(pdu_first);

        rc = asn_put_child_value_by_label(pdu_field, pdu_plain, "plain");
        if (rc != 0)
            return rc;
    }

    return 0;
}

static te_errno
test_pattern_to_pdus(const asn_value *pattern, asn_value *pdus, uint32_t *matches)
{
    unsigned int i;
    asn_value *pdu;
    asn_value *pdu_choice;
    asn_tag_value pdu_choice_tag;
    te_bool is_ifrm = FALSE;
    int rc;

    rc = asn_assign_value(pdus, pattern);
    if (rc != 0)
        return rc;

    for (i = 0; i < (unsigned int)pdus->len; i++)
    {
        rc = asn_get_indexed(pdus, &pdu, i, "");
        if (rc != 0)
            return rc;

        rc = asn_get_choice_value(pdu, &pdu_choice, NULL, &pdu_choice_tag);
        if (rc != 0)
            return rc;

        rc = test_pdu_change_range_fields_to_plain(pdu_choice, matches, is_ifrm);
        if (rc != 0)
            return rc;

        switch (pdu_choice_tag)
        {
            case TE_PROTO_ETH:
                rc = asn_get_subvalue(pdu_choice, &pdu, "tagged.#tagged");
                if (rc == 0)
                {
                    rc = test_pdu_change_range_fields_to_plain(pdu, matches,
                                                               is_ifrm);
                }
                else if (rc == TE_EASNOTHERCHOICE)
                {
                    asn_value *tmp;

                    rc = asn_get_subvalue(pdu_choice, &pdu,
                                          "tagged.#double-tagged");
                    if (rc != 0)
                        return rc;

                    rc = asn_get_subvalue(pdu, &tmp, "outer");
                    if (rc == 0)
                        rc = test_pdu_change_range_fields_to_plain(tmp, matches,
                                                                   is_ifrm);

                    rc = asn_get_subvalue(pdu, &tmp, "inner");
                    if (rc == 0)
                        rc = test_pdu_change_range_fields_to_plain(tmp, matches,
                                                                   is_ifrm);
                }
                if (rc != 0 && rc != TE_EASNINCOMPLVAL)
                {
                    return rc;
                }
                break;

            case TE_PROTO_GRE:
                rc = asn_get_subvalue(pdu_choice, &pdu, "opt-key.#nvgre");
                if (rc == 0)
                    rc = test_pdu_change_range_fields_to_plain(pdu, matches,
                                                               is_ifrm);
                if (rc != 0 && rc != TE_EASNINCOMPLVAL &&
                    rc != TE_EASNOTHERCHOICE)
                    return rc;
                /* FALLTHROUGH */

            case TE_PROTO_VXLAN:
            case TE_PROTO_GENEVE:
                is_ifrm = TRUE;
                break;

            case TE_PROTO_ARP:
            case TE_PROTO_PPPOE:
                *matches |= is_ifrm ? TEST_TMPL_IFRM_ETHER_TYPE :
                                      TEST_TMPL_ETHER_TYPE;
                break;

            case TE_PROTO_ICMP4:
                *matches |= is_ifrm ? TEST_TMPL_IFRM_IP_PROTO :
                                      TEST_TMPL_IP_PROTO;
                break;
        }
    }

    /* Template and pattern have different order of pdus */
    test_pdus_reverse(pdus);

    return 0;
}

#define TEST_ASN_WRITE_INT_IF_EMPTY(_asn_val, _val, _name) \
        do {                                                        \
            asn_value *__pdu;                                       \
                                                                    \
            rc = asn_get_subvalue(_asn_val, &__pdu, _name);         \
            if (rc == TE_EASNINCOMPLVAL)                            \
                rc = asn_write_int32(_asn_val, _val,                \
                                     _name ".#plain");              \
            if (rc != 0)                                            \
                return rc;                                          \
        } while (0)

#define TEST_ASN_WRITE_ADDR_IF_EMPTY(_asn_val, _name, _addr, _size) \
        do {                                                        \
            asn_value *__pdu;                                       \
                                                                    \
            rc = asn_get_subvalue(_asn_val, &__pdu, _name);         \
            if (rc == TE_EASNINCOMPLVAL)                            \
                rc = asn_write_value_field(_asn_val, _addr,         \
                                           _size, _name ".#plain"); \
            if (rc != 0)                                            \
                return rc;                                          \
        } while (0)

static te_errno
test_add_pdu(asn_value *pdus, const asn_type *type, int index)
{
    asn_value *pdu;
    asn_value *generic_pdu;
    int rc = 0;

    pdu = asn_init_value(type);
    if (pdu == NULL)
        return TE_ENOMEM;

    generic_pdu = asn_init_value(ndn_generic_pdu);
    if (generic_pdu == NULL)
        return TE_ENOMEM;

    rc = asn_put_choice(generic_pdu, pdu);
    if (rc != 0)
        return rc;

    rc = asn_insert_indexed(pdus, generic_pdu, index, "");
    return rc;
}

static te_errno
test_get_pdu_tag_by_index(asn_value *pdus, int index, asn_tag_value *pdu_tag)
{
    asn_value *pdu;
    asn_value *generic_pdu;
    asn_tag_value pdu_choice_tag;
    int rc;

    rc = asn_get_indexed(pdus, &generic_pdu, index, "");
    if (rc == 0)
        rc = asn_get_choice_value(generic_pdu, &pdu, NULL, &pdu_choice_tag);
    if (rc != 0)
        return rc;

    *pdu_tag = pdu_choice_tag;
    return 0;
}

static te_errno
test_complete_packet(asn_value *pdus, te_bool is_ip6_ifrm)
{
    asn_tag_value pdu_choice_tag;
    te_bool is_encap_tmpl = FALSE;
    int pdus_count;
    int i;
    int rc = 0;

    pdus_count = asn_get_length(pdus, "");
    rc = (pdus_count > 0) ? 0 : TE_EINVAL;
    if (rc != 0)
        return rc;

    for (i = 0; i < pdus_count; i++)
    {
        rc = test_get_pdu_tag_by_index(pdus, i, &pdu_choice_tag);
        if (rc != 0)
            return rc;

        if ((pdu_choice_tag == TE_PROTO_VXLAN) ||
            (pdu_choice_tag == TE_PROTO_GENEVE) ||
            (pdu_choice_tag == TE_PROTO_GRE))
        {
            is_encap_tmpl = TRUE;
            break;
        }
    }

    rc = test_get_pdu_tag_by_index(pdus, -1, &pdu_choice_tag);
    if (rc != 0)
        return rc;

    /*
     * Add missing layers that could be omitted in the flow rule.
     * IPv4 and UDP protocols are used to complete packets.
     */
    switch (pdu_choice_tag)
    {
        case TE_PROTO_VXLAN:
        case TE_PROTO_GENEVE:
            rc = test_add_pdu(pdus, ndn_udp_header, -1);
            /* FALLTHROUGH */

        case TE_PROTO_GRE:
        case TE_PROTO_TCP:
        case TE_PROTO_UDP:
            if (rc == 0)
                rc = test_add_pdu(pdus, ndn_ip4_header, -1);
            /* FALLTHROUGH */

        case TE_PROTO_IP4:
        case TE_PROTO_IP6:
            if (rc == 0)
                rc = test_add_pdu(pdus, ndn_eth_header, -1);
            /* FALLTHROUGH */
    }
    if (rc != 0)
        return rc;

    if (is_encap_tmpl)
    {
        rc = test_get_pdu_tag_by_index(pdus, 0, &pdu_choice_tag);
        if (rc != 0)
            return rc;

        /* Complete inner frame with IPv4/IPv6 and UDP protocols */
        switch (pdu_choice_tag)
        {
            case TE_PROTO_GRE:
            case TE_PROTO_VXLAN:
            case TE_PROTO_GENEVE:
                rc = test_add_pdu(pdus, ndn_eth_header, 0);
                /* FALLTHROUGH */

            case TE_PROTO_ETH:
                if (rc == 0)
                {
                    if (is_ip6_ifrm)
                        rc = test_add_pdu(pdus, ndn_ip6_header, 0);
                    else
                        rc = test_add_pdu(pdus, ndn_ip4_header, 0);
                }
                /* FALLTHROUGH */

            case TE_PROTO_IP4:
            case TE_PROTO_IP6:
                if (rc == 0)
                    rc = test_add_pdu(pdus, ndn_udp_header, 0);
                /* FALLTHROUGH */
        }
        if (rc != 0)
            return rc;
    }

    return 0;
}

static te_errno
test_write_def_udp_dst_port(asn_value *udp_pdu, uint16_t port_number)
{
    asn_value *pdu;
    int32_t val;
    int rc;

    if (udp_pdu == NULL)
        return TE_EINVAL;

    rc = asn_get_subvalue(udp_pdu, &pdu, "dst-port");
    if (rc == 0)
    {
        rc = asn_read_int32(udp_pdu, &val, "dst-port.#plain");
        if (rc != 0 || val != 0)
            return rc;
    }
    else if (rc != TE_EASNINCOMPLVAL)
    {
        return rc;
    }

    rc = asn_write_int32(udp_pdu, port_number, "dst-port.#plain");
    return rc;
}

static te_errno
test_asn_write_gre_key_present(asn_value *gre_pdu)
{
    asn_value *opt_key_pdu;
    int rc;

    rc = asn_get_subvalue(gre_pdu, &opt_key_pdu, "opt-key");
    if (rc == 0)
        rc = asn_write_int32(gre_pdu, 1, "key-present.#plain");
    else if (rc == TE_EASNINCOMPLVAL)
        rc = 0;

    return rc;
}

te_errno
test_add_tunnel_udp_port_from_tmpl(struct test_ethdev_config *ethdev_config,
                                   asn_value *tmpl, te_bool required)
{
    asn_value                       *pdus = NULL;
    int                              pdus_count;
    asn_value                       *pdu_udp_cv = NULL;
    uint16_t                         udp_port_def = 0;
    struct tarpc_rte_eth_udp_tunnel  ut = {
                                           UINT16_MAX,
                                           TARPC_RTE_TUNNEL_TYPE_NONE,
    };
    te_errno                         rc;
    int                              i;

    rc = asn_get_subvalue(tmpl, &pdus, "pdus");
    if (rc != 0)
        return rc;

    pdus_count = asn_get_length(pdus, "");
    rc = (pdus_count > 0) ? 0 : TE_EINVAL;
    if (rc != 0)
        return rc;

    /* Pay attention to find outer UDP PDU only */
    for (i = 0; i < pdus_count; ++i)
    {
        asn_value     *pdu = NULL;
        asn_value     *pdu_cv = NULL;
        asn_tag_value  pdu_ct;

        rc = asn_get_indexed(pdus, &pdu, i, "");
        if (rc != 0)
            return rc;

        rc = asn_get_choice_value(pdu, &pdu_cv, NULL, &pdu_ct);
        if (rc != 0)
            return rc;

        switch (pdu_ct) {
            case TE_PROTO_UDP:
                pdu_udp_cv = pdu_cv;
                break;

            case TE_PROTO_VXLAN:
                udp_port_def = TEST_DEF_VXLAN_UDP_PORT;
                ut.prot_type = TARPC_RTE_TUNNEL_TYPE_VXLAN;
                break;

            case TE_PROTO_GENEVE:
                udp_port_def = TEST_DEF_GENEVE_UDP_PORT;
                ut.prot_type = TARPC_RTE_TUNNEL_TYPE_GENEVE;
                break;

            default:
                break;
        }
    }

    if ((pdu_udp_cv != NULL) &&
        (ut.prot_type != TARPC_RTE_TUNNEL_TYPE_NONE))
    {
        size_t ut_port_size = sizeof(ut.udp_port);

        rc = asn_read_value_field(pdu_udp_cv, &ut.udp_port,
                                  &ut_port_size, "dst-port");
        if (rc == TE_EASNINCOMPLVAL)
            ut.udp_port = udp_port_def;
        else if (rc != 0)
            return rc;

        rc = test_tunnel_udp_port_add(ethdev_config, &ut);
        if (rc == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
        {
            if (required)
                TEST_SKIP("The operation to add a tunnel UDP port is unsupported");
            else
                WARN("The operation to add a tunnel UDP port is unsupported");
        }
        else if (rc == -TE_RC(TE_RPC, TE_EINVAL))
        {
            TEST_SKIP("The tunnel type is unsupported");
        }
        else if (rc != 0)
        {
            TEST_VERDICT("Failed to add a tunnel UDP port");
        }
        /*
         * UDP tunnel port reconfiguration on Xilinx X2 NICs
         * may require MC reboot. So wait link up.
         */
        if (ethdev_config->cur_state == TEST_ETHDEV_STARTED)
            test_await_link_up(ethdev_config->rpcs, ethdev_config->port_id);
    }

    return 0;
}

te_errno
test_pdus_detect_and_spoil_cksums(unsigned int   nb_pdus,
                                  asn_value    **pdus,
                                  uint64_t      *flags_good_out,
                                  uint64_t      *flags_bad_out,
                                  int           *l4_type_out)
{
    asn_value *pdu_ip4 = NULL;
    asn_value *pdu_ip6 = NULL;
    asn_value *pdu_tcp = NULL;
    asn_value *pdu_udp = NULL;
    asn_value *pdu_icmp4 = NULL;
    uint64_t   flags_good = 0;
    uint64_t   flags_bad = 0;
    int        l4_type = -1;
    te_bool    need_l4_flags = FALSE;
    te_errno   rc = 0;

    pdu_ip4 = asn_choice_array_look_up_value(nb_pdus, pdus, TE_PROTO_IP4);
    if (pdu_ip4 != NULL)
    {
        rc = asn_write_int32(pdu_ip4, 0, "h-checksum.#plain");
        if (rc != 0)
            return rc;

        flags_good |= TARPC_RTE_MBUF_F_RX_IP_CKSUM_GOOD;
        flags_bad |= TARPC_RTE_MBUF_F_RX_IP_CKSUM_BAD;
    }

    pdu_tcp = asn_choice_array_look_up_value(nb_pdus, pdus, TE_PROTO_TCP);
    if (pdu_tcp != NULL)
    {
        rc = asn_write_int32(pdu_tcp, TE_IP4_UPPER_LAYER_CSUM_BAD,
                             "checksum.#plain");
        if (rc != 0)
            return rc;

        l4_type = TE_PROTO_TCP;
        need_l4_flags = TRUE;
    }

    pdu_udp = asn_choice_array_look_up_value(nb_pdus, pdus, TE_PROTO_UDP);
    if (pdu_udp != NULL)
    {
        rc = asn_write_int32(pdu_udp, TE_IP4_UPPER_LAYER_CSUM_BAD,
                             "checksum.#plain");
        if (rc != 0)
            return rc;

        l4_type = TE_PROTO_UDP;
        need_l4_flags = TRUE;
    }

    pdu_icmp4 = asn_choice_array_look_up_value(nb_pdus, pdus, TE_PROTO_ICMP4);

    if (need_l4_flags && (pdu_icmp4 == NULL))
    {
        flags_good |= TARPC_RTE_MBUF_F_RX_L4_CKSUM_GOOD;
        flags_bad |= TARPC_RTE_MBUF_F_RX_L4_CKSUM_BAD;
    }

    pdu_ip6 = asn_choice_array_look_up_value(nb_pdus, pdus, TE_PROTO_IP6);
    if (pdu_ip6 != NULL)
    {
        const asn_type *type = NULL;
        asn_value      *pld_cksum = NULL;
        asn_value      *pld_cksum_val = NULL;

        rc = asn_get_subtype(ndn_ip6_header, &type, "pld-checksum");
        if (rc != 0)
            return rc;

        pld_cksum = asn_init_value(type);
        if (pld_cksum == NULL)
            return TE_ENOMEM;

        pld_cksum_val = asn_init_value(asn_base_null);
        if (pld_cksum_val == NULL)
        {
            asn_free_value(pld_cksum);
            return TE_ENOMEM;
        }

        rc = asn_put_child_value_by_label(pld_cksum, pld_cksum_val, "disable");
        if (rc != 0)
        {
            asn_free_value(pld_cksum_val);
            asn_free_value(pld_cksum);
            return rc;
        }

        rc = asn_put_child_value_by_label(pdu_ip6, pld_cksum, "pld-checksum");
        if (rc != 0)
        {
            asn_free_value(pld_cksum);
            return rc;
        }
    }

    if (flags_good_out != NULL)
        *flags_good_out = flags_good;

    if (flags_bad_out != NULL)
        *flags_bad_out = flags_bad;

    if (l4_type_out != NULL)
        *l4_type_out = l4_type;

    return 0;
}

struct sockaddr *
test_rand_ip_addr(int af)
{
    struct sockaddr *addr = NULL;

    addr = TE_ALLOC((af == AF_INET) ? sizeof(struct sockaddr_in) :
                                      sizeof(struct sockaddr_in6));
    if (addr == NULL)
        return NULL;

    if (af == AF_INET)
    {
        do {
            SIN(addr)->sin_addr.s_addr = rand();
        } while (IN_MULTICAST(SIN(addr)->sin_addr.s_addr));
    }
    else if (af == AF_INET6)
    {
        unsigned int i;

        do {
            for (i = 0; i < TE_ARRAY_LEN(SIN6(addr)->sin6_addr.s6_addr); i++)
                SIN6(addr)->sin6_addr.s6_addr[i] = rand();
        } while (IN6_IS_ADDR_MULTICAST(&(SIN6(addr)->sin6_addr)));
    }
    else
    {
        free(addr);
        return NULL;
    }

    SIN(addr)->sin_family = af;

    return addr;
}

void
test_set_pkt_addresses(struct test_pkt_addresses *addrs,
                       const uint8_t *src_mac,
                       const uint8_t *dst_mac,
                       const struct sockaddr *src_ip,
                       const struct sockaddr *dst_ip)
{
    CHECK_NOT_NULL(addrs);

    addrs->src_mac = src_mac;
    addrs->dst_mac = dst_mac;
    addrs->src_ip = src_ip;
    addrs->dst_ip = dst_ip;
}

static te_errno
test_complete_empty_pdu_arp(asn_value *pdu_arp,
                            const uint8_t *src_mac_addr,
                            const uint8_t *dst_mac_addr,
                            const struct sockaddr *src_ip_addr,
                            const struct sockaddr *dst_ip_addr)
{
    int rc;

    rc = asn_write_int32(pdu_arp, ARPOP_REQUEST, "opcode.#plain");
    if (rc == 0)
        rc = asn_write_int32(pdu_arp, ARPHRD_ETHER, "hw-type.#plain");
    if (rc == 0)
        rc = asn_write_int32(pdu_arp, ETHER_ADDR_LEN, "hw-size.#plain");
    if (rc == 0)
        rc = asn_write_int32(pdu_arp, TEST_IP4_ADDR_LEN, "proto-size.#plain");
    if (rc == 0)
        rc = asn_write_int32(pdu_arp, ETHERTYPE_IP, "proto-type.#plain");
    if (rc == 0)
        rc = asn_write_value_field(pdu_arp, src_mac_addr, ETHER_ADDR_LEN,
                                   "snd-hw-addr.#plain");
    if (rc == 0)
        rc = asn_write_int32(pdu_arp, CONST_SIN(src_ip_addr)->sin_addr.s_addr,
                             "snd-proto-addr.#plain");
    if (rc == 0)
        rc = asn_write_value_field(pdu_arp, dst_mac_addr, ETHER_ADDR_LEN,
                                   "tgt-hw-addr.#plain");
    if (rc == 0)
        rc = asn_write_int32(pdu_arp, CONST_SIN(dst_ip_addr)->sin_addr.s_addr,
                             "tgt-proto-addr.#plain");
    return rc;
}

static te_errno
test_complete_empty_pdu_pppoe(asn_value *pdu_pppoe)
{
    int rc;

    rc = asn_write_int32(pdu_pppoe, TEST_DEF_PPPOE_VER, "version.#plain");
    if (rc == 0)
        rc = asn_write_int32(pdu_pppoe, TEST_DEF_PPPOE_TYPE, "type.#plain");
    if (rc == 0)
        rc = asn_write_int32(pdu_pppoe, TEST_DEF_PPPOE_PADI_CODE, "code.#plain");
    if (rc == 0)
        rc = asn_write_int32(pdu_pppoe, 0, "session-id.#plain");
    return rc;
}

static te_errno
test_complete_empty_pdu_icmp(asn_value *pdu_icmp)
{
    int rc;

    rc = asn_write_int32(pdu_icmp, ICMP_ECHOREPLY, "type.#plain");
    if (rc == 0)
        rc = asn_write_int32(pdu_icmp, 0, "code.#plain");
    if (rc == 0)
        rc = asn_write_int32(pdu_icmp, 0, "id.#plain");
    if (rc == 0)
        rc = asn_write_int32(pdu_icmp, 0, "seq.#plain");
    return rc;
}

te_errno
test_fill_in_tmpl_req_fields(const asn_value *tmpl,
                             struct test_pkt_addresses *addrs,
                             struct test_pkt_addresses *ifrm_addrs)
{
    int i;
    asn_value *pdus;
    asn_value *eth_pdu = NULL;
    asn_value *ip4_pdu = NULL;
    asn_value *udp_pdu = NULL;
    te_bool is_l4 = FALSE;
    te_bool is_l3 = FALSE;
    te_bool is_ifrm = FALSE;
    te_bool is_ip6_ifrm = FALSE;
    int rc;

    rc = asn_get_subvalue(tmpl, &pdus, "pdus");
    if (rc != 0)
        return rc;

    if (ifrm_addrs->src_ip != NULL && ifrm_addrs->dst_ip != NULL)
        is_ip6_ifrm = (CONST_SIN6(ifrm_addrs->src_ip)->sin6_family == AF_INET6);

    rc = test_complete_packet(pdus, is_ip6_ifrm);
    if (rc != 0)
        return rc;

    for (i = pdus->len - 1; i >= 0; i--)
    {
        asn_value *pdu;
        asn_value *pdu_choice;
        asn_tag_value pdu_choice_tag;
        const uint8_t *src_mac = addrs->src_mac;
        const uint8_t *dst_mac = addrs->dst_mac;
        const struct sockaddr *src_ip = addrs->src_ip;
        const struct sockaddr *dst_ip = addrs->dst_ip;

        rc = asn_get_indexed(pdus, &pdu, i, "");
        if (rc != 0)
            return rc;

        rc = asn_get_choice_value(pdu, &pdu_choice, NULL, &pdu_choice_tag);
        if (rc != 0)
            return rc;

        switch (pdu_choice_tag)
        {
            case TE_PROTO_GENEVE:
                TEST_ASN_WRITE_INT_IF_EMPTY(pdu_choice,
                                            TEST_DEF_PROTOCOL_TYPE,
                                            "protocol");
                /* FALLTHROUGH */
            case TE_PROTO_VXLAN:
                is_ifrm = TRUE;
                rc = test_write_def_udp_dst_port(udp_pdu,
                                                 (pdu_choice_tag == TE_PROTO_VXLAN) ?
                                                 TEST_DEF_VXLAN_UDP_PORT :
                                                 TEST_DEF_GENEVE_UDP_PORT);
                if (rc != 0)
                    return rc;
                break;

            case TE_PROTO_GRE:
                rc = test_asn_write_gre_key_present(pdu_choice);
                if (rc != 0)
                    return rc;
                TEST_ASN_WRITE_INT_IF_EMPTY(pdu_choice,
                                            TEST_DEF_PROTOCOL_TYPE,
                                            "protocol");
                is_ifrm = TRUE;
                is_l4 = TRUE;
                break;

            case TE_PROTO_TCP:
                TEST_ASN_WRITE_INT_IF_EMPTY(pdu_choice, 0, "seqn");
                /* FALLTHROUGH */

            case TE_PROTO_UDP:
                TEST_ASN_WRITE_INT_IF_EMPTY(pdu_choice, 0, "dst-port");
                TEST_ASN_WRITE_INT_IF_EMPTY(pdu_choice, 0, "src-port");
                if (pdu_choice_tag == TE_PROTO_UDP)
                    udp_pdu = pdu_choice;
                is_l4 = TRUE;
                break;

            case TE_PROTO_ICMP4:
                rc = test_complete_empty_pdu_icmp(pdu_choice);
                if (rc != 0)
                    return rc;
                is_l4 = TRUE;
                break;

            case TE_PROTO_IP4:
            case TE_PROTO_IP6:
                if (is_ifrm)
                {
                    if (ifrm_addrs->src_ip == NULL ||
                        ifrm_addrs->dst_ip == NULL)
                        return TE_EINVAL;

                    src_ip = ifrm_addrs->src_ip;
                    dst_ip = ifrm_addrs->dst_ip;
                }
                if (pdu_choice_tag == TE_PROTO_IP4)
                {
                    TEST_ASN_WRITE_INT_IF_EMPTY(pdu_choice,
                                                CONST_SIN(src_ip)->sin_addr.s_addr,
                                                "src-addr");
                    TEST_ASN_WRITE_INT_IF_EMPTY(pdu_choice,
                                                CONST_SIN(dst_ip)->sin_addr.s_addr,
                                                "dst-addr");
                    ip4_pdu = pdu_choice;
                }
                else
                {
                    TEST_ASN_WRITE_ADDR_IF_EMPTY(pdu_choice, "src-addr",
                                                 CONST_SIN6(src_ip)->sin6_addr.s6_addr,
                                                 TEST_IP6_ADDR_LEN);
                    TEST_ASN_WRITE_ADDR_IF_EMPTY(pdu_choice, "dst-addr",
                                                 CONST_SIN6(dst_ip)->sin6_addr.s6_addr,
                                                 TEST_IP6_ADDR_LEN);
                }
                is_l3 = TRUE;
                break;

            case TE_PROTO_ARP:
                rc = test_complete_empty_pdu_arp(pdu_choice,
                                                 addrs->src_mac, addrs->dst_mac,
                                                 addrs->src_ip, addrs->dst_ip);
                if (rc != 0)
                    return rc;
                is_l3 = TRUE;
                break;

            case TE_PROTO_PPPOE:
                rc = test_complete_empty_pdu_pppoe(pdu_choice);
                if (rc != 0)
                    return rc;
                is_l3 = TRUE;
                break;

            case TE_PROTO_ETH:
                if (is_ifrm)
                {
                    if (ifrm_addrs->src_mac == NULL ||
                        ifrm_addrs->dst_mac == NULL)
                        return TE_EINVAL;

                    src_mac = ifrm_addrs->src_mac;
                    dst_mac = ifrm_addrs->dst_mac;
                    is_l3 = FALSE;
                    is_l4 = FALSE;
                    ip4_pdu = NULL;
                }
                TEST_ASN_WRITE_ADDR_IF_EMPTY(pdu_choice, "src-addr",
                                             src_mac, ETHER_ADDR_LEN);
                TEST_ASN_WRITE_ADDR_IF_EMPTY(pdu_choice, "dst-addr",
                                             dst_mac, ETHER_ADDR_LEN);
                eth_pdu = pdu_choice;
                break;
        }
    }

    if (!is_l3 && eth_pdu != NULL)
        TEST_ASN_WRITE_INT_IF_EMPTY(eth_pdu, TEST_DEF_ETHER_TYPE, "length-type");
    if (!is_l4 && ip4_pdu != NULL)
        TEST_ASN_WRITE_INT_IF_EMPTY(ip4_pdu, TEST_DEF_IP_PROTO, "protocol");

    return 0;
}

te_errno
test_mk_template_get_match_fields_from_flow_ndn(const asn_value *flow_ndn,
                                                asn_value **tmpl_out,
                                                uint32_t *match_fields)
{
    asn_value *tmpl;
    asn_value *pattern = NULL;
    asn_value *pdus = NULL;
    uint32_t matches = 0;
    const char *pattern_label;
    int rc = 0;

    if (tmpl_out == NULL || flow_ndn == NULL)
        return TE_EINVAL;

    tmpl = asn_init_value(ndn_traffic_template);
    if (tmpl == NULL)
        return TE_ENOMEM;

    /* flow_ndn is could be ASN.1 representation of flow rule or pattern */
    pattern_label = (asn_get_type(flow_ndn) == ndn_rte_flow_pattern) ?
                    "" : "pattern";

    pattern = asn_find_descendant(flow_ndn, &rc, pattern_label);
    if (rc != 0)
        return rc;

    pdus = asn_retrieve_descendant(tmpl, &rc, "pdus");
    if (rc != 0)
        return rc;

    rc = test_pattern_to_pdus(pattern, pdus, &matches);
    if (rc != 0)
        return rc;

    if (match_fields != NULL)
        *match_fields = matches;

    *tmpl_out = tmpl;

    return 0;
}

asn_value *
test_tmpl_copy_with_changed_fields(const asn_value *tmpl,
                                   uint32_t fields,
                                   struct test_pkt_addresses *addrs,
                                   struct test_pkt_addresses *ifrm_addrs)
{
    asn_value *tmpl_copy = NULL;
    asn_value *pdus;
    asn_value *pdu;
    asn_value *pdu_choice;
    asn_tag_value pdu_choice_tag;
    int32_t val;
    te_bool is_ifrm = FALSE;
    const uint8_t *addr;
    int i;
    int rc;

#define TEST_ASN_WRITE_NEW_ADDR(_asn_val, _addr, _name, _size)  \
    do {                                                        \
        size_t __size = _size;                                  \
        uint8_t __buf[_size];                                   \
                                                                \
        CHECK_RC(asn_read_value_field(_asn_val, __buf, &__size, \
                                      _name ".#plain"));        \
                                                                \
        if (memcmp(_addr, __buf, __size) == 0)                  \
            return NULL;                                        \
                                                                \
        CHECK_RC(asn_write_value_field(_asn_val, _addr, __size, \
                                       _name ".#plain"));       \
    } while (0)

#define TEST_ASN_WRITE_NEW_INT(_asn_val, _name)                 \
    do {                                                        \
        int32_t __val;                                          \
                                                                \
        CHECK_RC(asn_read_int32(_asn_val, &__val,               \
                                _name ".#plain"));              \
        CHECK_RC(asn_write_int32(_asn_val, __val + 1,           \
                                 _name ".#plain"));             \
    } while (0)

    tmpl_copy = asn_copy_value(tmpl);
    CHECK_RC(asn_get_subvalue(tmpl_copy, &pdus, "pdus"));

    for (i = pdus->len - 1; i >= 0; i--)
    {
        rc = asn_get_indexed(pdus, &pdu, i, "");
        if (rc != 0)
            return NULL;

        rc = asn_get_choice_value(pdu, &pdu_choice, NULL, &pdu_choice_tag);
        if (rc != 0)
            return NULL;

        switch (pdu_choice_tag)
        {
            case TE_PROTO_ETH:
                if (fields & TEST_TMPL_SRC_MAC)
                {
                    CHECK_NOT_NULL(addrs->src_mac);
                    TEST_ASN_WRITE_NEW_ADDR(pdu_choice, addrs->src_mac, "src-addr",
                                            ETHER_ADDR_LEN);
                }
                if ((!is_ifrm && (fields & TEST_TMPL_DST_MAC)) ||
                    (is_ifrm && (fields & TEST_TMPL_IFRM_DST_MAC)) ||
                    (!is_ifrm && (fields & TEST_TMPL_UN_UCAST_DST)) ||
                    (is_ifrm && (fields & TEST_TMPL_IFRM_UN_UCAST_DST)) ||
                    (!is_ifrm && (fields & TEST_TMPL_UN_MCAST_DST)) ||
                    (is_ifrm && (fields & TEST_TMPL_IFRM_UN_MCAST_DST)))
                {
                    if (is_ifrm)
                    {
                        CHECK_NOT_NULL(ifrm_addrs->dst_mac);
                        addr = ifrm_addrs->dst_mac;
                    }
                    else
                    {
                        CHECK_NOT_NULL(addrs->dst_mac);
                        addr = addrs->dst_mac;
                    }
                    TEST_ASN_WRITE_NEW_ADDR(pdu_choice, addr, "dst-addr",
                                            ETHER_ADDR_LEN);
                }
                if (fields & TEST_TMPL_ETHER_TYPE)
                {
                    asn_value *ethertype;
                    rc = asn_get_subvalue(pdu_choice, &ethertype,
                                          "length-type");
                    if (rc == 0)
                        TEST_ASN_WRITE_NEW_INT(pdu_choice, "length-type");
                    else if (rc != TE_EASNINCOMPLVAL)
                        return NULL;
                }
                if (fields & TEST_TMPL_OUTER_VID)
                {
                    asn_value *vlan_header;

                    rc = asn_get_subvalue(pdu_choice, &vlan_header,
                                          "tagged.#tagged");
                    if (rc == 0)
                        TEST_ASN_WRITE_NEW_INT(vlan_header, "vlan-id");
                    else if (rc == TE_EASNOTHERCHOICE)
                        TEST_ASN_WRITE_NEW_INT(pdu_choice,
                                               "tagged.#double-tagged.outer.vid");
                    else
                        return NULL;
                }
                if (fields & TEST_TMPL_INNER_VID)
                {
                    TEST_ASN_WRITE_NEW_INT(pdu_choice,
                                           "tagged.#double-tagged.inner.vid");
                }
                break;

            case TE_PROTO_ARP:
            case TE_PROTO_PPPOE:
                if (fields & TEST_TMPL_ETHER_TYPE)
                {
                    CHECK_RC(asn_remove_indexed(pdus, i, ""));
                    CHECK_RC(asn_get_indexed(pdus, &pdu, i, ""));
                    CHECK_RC(asn_write_int32(pdu, TEST_DEF_ETHER_TYPE,
                                             "#eth.length-type.#plain"));
                }
                break;

            case TE_PROTO_IP4:
                if (fields & TEST_TMPL_SRC_HOST)
                {
                    CHECK_NOT_NULL(addrs->src_ip);
                    CHECK_RC(asn_read_int32(pdu_choice, &val, "src-addr.#plain"));
                    if (CONST_SIN(addrs->src_ip)->sin_addr.s_addr == (uint32_t)val)
                        return NULL;
                    CHECK_RC(asn_write_int32(pdu_choice,
                                             CONST_SIN(addrs->src_ip)->sin_addr.s_addr,
                                             "src-addr.#plain"));
                }
                if (fields & TEST_TMPL_DST_HOST)
                {
                    CHECK_NOT_NULL(addrs->dst_ip);
                    CHECK_RC(asn_read_int32(pdu_choice, &val, "dst-addr.#plain"));
                    if (CONST_SIN(addrs->dst_ip)->sin_addr.s_addr == (uint32_t)val)
                        return NULL;
                    CHECK_RC(asn_write_int32(pdu_choice,
                                             CONST_SIN(addrs->dst_ip)->sin_addr.s_addr,
                                             "dst-addr.#plain"));
                }
                if (fields & TEST_TMPL_IP_PROTO)
                {
                    asn_value *protocol;
                    rc = asn_get_subvalue(pdu_choice, &protocol, "protocol");
                    if (rc == 0)
                        TEST_ASN_WRITE_NEW_INT(pdu_choice, "protocol");
                    else if (rc != TE_EASNINCOMPLVAL)
                        return NULL;
                }
                break;

            case TE_PROTO_IP6:
                if (fields & TEST_TMPL_SRC_HOST)
                {
                    CHECK_NOT_NULL(addrs->src_ip);
                    TEST_ASN_WRITE_NEW_ADDR(pdu_choice,
                                            CONST_SIN6(addrs->src_ip)->sin6_addr.s6_addr,
                                            "src-addr", TEST_IP6_ADDR_LEN);
                }
                if (fields & TEST_TMPL_DST_HOST)
                {
                    CHECK_NOT_NULL(addrs->dst_ip);
                    TEST_ASN_WRITE_NEW_ADDR(pdu_choice,
                                            CONST_SIN6(addrs->dst_ip)->sin6_addr.s6_addr,
                                            "dst-addr", TEST_IP6_ADDR_LEN);
                }
                if (fields & TEST_TMPL_IP_PROTO)
                {
                    TEST_ASN_WRITE_NEW_INT(pdu_choice, "next-header");
                }
                break;

            case TE_PROTO_ICMP4:
                if (fields & TEST_TMPL_IP_PROTO)
                {
                    CHECK_RC(asn_remove_indexed(pdus, i, ""));
                    CHECK_RC(asn_get_indexed(pdus, &pdu, i, ""));
                    CHECK_RC(asn_write_int32(pdu, TEST_DEF_IP_PROTO,
                                             "#ip4.protocol.#plain"));
                }
                break;

            case TE_PROTO_TCP:
            case TE_PROTO_UDP:
                if (fields & TEST_TMPL_SRC_PORT)
                    TEST_ASN_WRITE_NEW_INT(pdu_choice, "src-port");
                if (fields & TEST_TMPL_DST_PORT)
                    TEST_ASN_WRITE_NEW_INT(pdu_choice, "dst-port");
                break;

            case TE_PROTO_VXLAN:
            case TE_PROTO_GENEVE:
            case TE_PROTO_GRE:
                if (fields & TEST_TMPL_VNI_OR_VSID)
                {
                    if (pdu_choice_tag == TE_PROTO_GRE)
                        TEST_ASN_WRITE_NEW_INT(pdu_choice, "opt-key.#nvgre.vsid");
                    else
                        TEST_ASN_WRITE_NEW_INT(pdu_choice, "vni");
                }
                is_ifrm = TRUE;
                break;
        }

        if (is_ifrm && pdu_choice_tag == TE_PROTO_ETH)
            break;
    }
#undef TEST_ASN_WRITE_NEW_ADDR
#undef TEST_ASN_WRITE_NEW_INT

    return tmpl_copy;
}

/* Template fields that can be changed */
const enum test_tmpl_fields tmpl_fields[] = {
    TEST_TMPL_SRC_MAC,
    TEST_TMPL_DST_MAC,
    TEST_TMPL_ETHER_TYPE,
    TEST_TMPL_OUTER_VID,
    TEST_TMPL_INNER_VID,
    TEST_TMPL_SRC_HOST,
    TEST_TMPL_DST_HOST,
    TEST_TMPL_IP_PROTO,
    TEST_TMPL_SRC_PORT,
    TEST_TMPL_DST_PORT,
    TEST_TMPL_VNI_OR_VSID,
    TEST_TMPL_IFRM_DST_MAC,
    TEST_TMPL_IFRM_UN_MCAST_DST,
    TEST_TMPL_IFRM_UN_UCAST_DST,
    TEST_TMPL_UN_MCAST_DST,
    TEST_TMPL_UN_UCAST_DST
};

te_errno
test_mk_tmpls_with_change_one_field(const asn_value *tmpl,
                                    uint32_t fields,
                                    struct test_pkt_addresses *addrs,
                                    struct test_pkt_addresses *ifrm_addrs,
                                    asn_value *(*tmpls_out[]),
                                    unsigned int *tmpls_nb_out)
{
    asn_value **tmpls = NULL;
    unsigned int tmpls_nb = 0;
    unsigned int i;

    for (i = 0; i < TE_ARRAY_LEN(tmpl_fields); i++)
    {
        if ((fields & tmpl_fields[i]) == 0)
            continue;

        tmpls = tapi_realloc(tmpls, ((tmpls_nb + 1) * sizeof(asn_value *)));

        tmpls[tmpls_nb] = test_tmpl_copy_with_changed_fields(tmpl, tmpl_fields[i],
                                                             addrs, ifrm_addrs);
        CHECK_NOT_NULL(tmpls[tmpls_nb]);
        tmpls_nb++;
    }

    *tmpls_out = tmpls;
    *tmpls_nb_out = tmpls_nb;
    return 0;
}

/* Mapping between ASN.1 representation of RSS HF and RPC flags */
static struct test_asn2rpc_rss_hf_map {
    asn_tag_value asn_tag;
    uint64_t      rpc_flag;
} test_asn2rpc_rss_hf_map[] = {
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_IPV4,
      TEST_ETH_RSS_IPV4 },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_FRAG_IPV4,
      TEST_ETH_RSS_FRAG_IPV4 },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_NONFRAG_IPV4_TCP,
      TEST_ETH_RSS_NONFRAG_IPV4_TCP },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_NONFRAG_IPV4_UDP,
      TEST_ETH_RSS_NONFRAG_IPV4_UDP },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_NONFRAG_IPV4_SCTP,
      TEST_ETH_RSS_NONFRAG_IPV4_SCTP },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_NONFRAG_IPV4_OTHER,
      TEST_ETH_RSS_NONFRAG_IPV4_OTHER },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_IPV6,
      TEST_ETH_RSS_IPV6 },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_NONFRAG_IPV6_TCP,
      TEST_ETH_RSS_NONFRAG_IPV6_TCP },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_NONFRAG_IPV6_UDP,
      TEST_ETH_RSS_NONFRAG_IPV6_UDP },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_NONFRAG_IPV6_SCTP,
      TEST_ETH_RSS_NONFRAG_IPV6_SCTP },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_NONFRAG_IPV6_OTHER,
      TEST_ETH_RSS_NONFRAG_IPV6_OTHER },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_L2_PAYLOAD,
      TEST_ETH_RSS_L2_PAYLOAD },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_IPV6_EX,
      TEST_ETH_RSS_IPV6_EX },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_IPV6_TCP_EX,
      TEST_ETH_RSS_IPV6_TCP_EX },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_IPV6_UDP_EX,
      TEST_ETH_RSS_IPV6_UDP_EX },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_PORT,
      TEST_ETH_RSS_PORT },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_VXLAN,
      TEST_ETH_RSS_VXLAN },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_GENEVE,
      TEST_ETH_RSS_GENEVE },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_NVGRE,
      TEST_ETH_RSS_NVGRE },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_IP,
      TEST_ETH_RSS_IP },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_TCP,
      TEST_ETH_RSS_TCP },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_UDP,
      TEST_ETH_RSS_UDP },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_SCTP,
      TEST_ETH_RSS_SCTP },
    { NDN_FLOW_ACTION_CONF_RSS_OPT_HF_TUNNEL,
      TEST_ETH_RSS_TUNNEL },
};

static te_errno
test_get_rss_hf_by_rss_conf(const asn_value *rss_conf,
                            uint64_t        *rss_hf_out)
{
    const asn_value *rss_hf;
    uint64_t         rss_hf_rpc = 0;
    te_errno         rc;
    unsigned int     i;

    rc = asn_get_subvalue(rss_conf, (asn_value **)&rss_hf, "rss-hf");
    if (rc == TE_EASNINCOMPLVAL)
        goto out;
    else if (rc != 0)
        return rc;

    for (i = 0; i < TE_ARRAY_LEN(test_asn2rpc_rss_hf_map); ++i)
    {
        const asn_value *hf;

        rc = asn_get_child_value(rss_hf, &hf,
                                 PRIVATE, test_asn2rpc_rss_hf_map[i].asn_tag);
        if (rc == 0)
            rss_hf_rpc |= test_asn2rpc_rss_hf_map[i].rpc_flag;
        else if (rc != TE_EASNINCOMPLVAL)
            return rc;
    }

out:
    *rss_hf_out = rss_hf_rpc;

    return 0;
}

static te_errno
test_get_rss_settings_by_action_conf(const asn_value  *fr_action_conf_rss,
                                     uint64_t         *rss_hf_out,
                                     uint8_t         **rss_key_out)
{
    asn_value *rss_conf = NULL;
    asn_value *rss_key = NULL;
    uint64_t   rss_hf = 0;
    uint8_t   *rss_key_bytes = NULL;
    size_t     d_len = RPC_RSS_HASH_KEY_LEN_DEF;
    te_errno   rc = 0;

    rc = asn_get_subvalue(fr_action_conf_rss, &rss_conf, "rss-conf");
    if (rc == TE_EASNINCOMPLVAL)
        goto out;
    else if (rc != 0)
        return rc;

    rc = test_get_rss_hf_by_rss_conf(rss_conf, &rss_hf);
    if (rc != 0)
        return rc;

    rc = asn_get_subvalue(rss_conf, &rss_key, "rss-key");
    if (rc == TE_EASNINCOMPLVAL)
        goto out;
    else if (rc != 0)
        return rc;

    if (asn_get_length(rss_key, "") != (int)d_len)
        return rc;

    rss_key_bytes = TE_ALLOC(d_len);
    if (rss_key_bytes == NULL)
        return TE_ENOMEM;

    rc = asn_read_value_field(rss_key, rss_key_bytes, &d_len, "");
    if (rc != 0)
    {
        free(rss_key);
        return rc;
    }

out:
    *rss_hf_out = rss_hf;
    *rss_key_out = rss_key_bytes;

    return 0;
}

te_errno
test_get_rx_info_by_rss_action(const asn_value  *flow_rule_rss,
                               uint16_t        **rss_queues_out,
                               uint16_t         *nb_rss_queues_out,
                               uint16_t         *nb_queues_out,
                               uint64_t         *rss_hf_out,
                               uint8_t         **rss_key_out)
{
    int            nb_fr_actions;
    asn_value     *fr_action_conf_rss = NULL;
    int            nb_entries;
    uint16_t      *rss_queues = NULL;
    uint16_t       queue_index_max = 0;
    uint64_t       rss_hf = 0;
    uint8_t       *rss_key = NULL;
    te_errno       rc = 0;
    int            i;

    if ((flow_rule_rss == NULL) || (rss_queues_out == NULL) ||
        (nb_rss_queues_out == NULL) || (nb_queues_out == NULL) ||
        (rss_hf_out == NULL) || (rss_key_out == NULL))
        return TE_EINVAL;

    nb_fr_actions = asn_get_length(flow_rule_rss, "actions");
    if (nb_fr_actions <= 0)
        return TE_EINVAL;

    for (i = 0; i < nb_fr_actions; ++i)
    {
        asn_value     *fr_action;
        asn_value     *fr_action_conf;
        asn_value     *fr_action_conf_choice;
        asn_tag_value  fr_action_conf_choice_tag;

        rc = asn_get_indexed(flow_rule_rss, &fr_action, i, "actions");
        if (rc != 0)
            return rc;

        rc = asn_get_subvalue(fr_action, &fr_action_conf, "conf");
        if (rc != 0)
            return rc;

        rc = asn_get_choice_value(fr_action_conf, &fr_action_conf_choice,
                                  NULL, &fr_action_conf_choice_tag);
        if (rc != 0)
            return rc;

        if (fr_action_conf_choice_tag == NDN_FLOW_ACTION_CONF_RSS)
        {
            fr_action_conf_rss = fr_action_conf_choice;
            break;
        }
    }

    if (fr_action_conf_rss == NULL)
        return TE_EINVAL;

    nb_entries = asn_get_length(fr_action_conf_rss, "queue");
    if (nb_entries <= 0)
        return TE_EINVAL;

    rss_queues = TE_ALLOC(nb_entries * sizeof(*rss_queues));
    if (rss_queues == NULL)
        return TE_EINVAL;

    for (i = 0; i < nb_entries; ++i)
    {
        asn_value *entry;
        uint16_t   queue_index;
        size_t     d_len = sizeof(queue_index);

        rc = asn_get_indexed(fr_action_conf_rss, &entry, i, "queue");
        if (rc != 0)
            goto fail;

        rc = asn_read_value_field(entry, &queue_index, &d_len, "");
        if (rc != 0)
            goto fail;

        rss_queues[i] = queue_index;

        if (queue_index > queue_index_max)
            queue_index_max = queue_index;
    }

    rc = test_get_rss_settings_by_action_conf(fr_action_conf_rss,
                                              &rss_hf, &rss_key);

    *rss_queues_out = rss_queues;
    *nb_rss_queues_out = nb_entries;
    *nb_queues_out = queue_index_max + 1;
    *rss_hf_out = rss_hf;
    *rss_key_out = rss_key;

    return 0;

fail:
    free(rss_queues);

    return rc;
}

te_errno
test_mk_rte_flow_attr_ingress(rcf_rpc_server *rpcs,
                              rpc_rte_flow_attr_p *attr)
{
    asn_value *attr_pdu;

    attr_pdu = asn_init_value(ndn_rte_flow_attr);
    if (attr_pdu == NULL)
        return TE_ENOMEM;

    CHECK_RC(asn_write_int32(attr_pdu, 1, "ingress"));

    rpc_rte_mk_flow_rule_components(rpcs, attr_pdu, attr,
                                    NULL, NULL);

    asn_free_value(attr_pdu);
    return 0;
}

void
test_add_ndn_action_queue(asn_value *ndn_actions, int action_index,
                          uint16_t queue)
{
    asn_value *queue_action;

    queue_action = asn_init_value(ndn_rte_flow_action);
    CHECK_NOT_NULL(queue_action);

    CHECK_RC(asn_write_int32(queue_action, NDN_FLOW_ACTION_TYPE_QUEUE,
                             "type"));
    CHECK_RC(asn_write_int32(queue_action, queue, "conf.#index"));

    CHECK_RC(asn_insert_indexed(ndn_actions, queue_action, action_index, ""));
}

void
test_add_ndn_action_drop(asn_value *ndn_actions, int action_index)
{
    asn_value *drop_action;

    drop_action = asn_init_value(ndn_rte_flow_action);
    CHECK_NOT_NULL(drop_action);

    CHECK_RC(asn_write_int32(drop_action, NDN_FLOW_ACTION_TYPE_DROP, "type"));

    CHECK_RC(asn_insert_indexed(ndn_actions, drop_action, action_index, ""));
}


te_errno
test_mk_rte_flow_action_drop(rcf_rpc_server *rpcs,
                             rpc_rte_flow_action_p *actions)
{
    asn_value *ndn_actions;

    ndn_actions = asn_init_value(ndn_rte_flow_actions);
    if (ndn_actions == NULL)
        return TE_ENOMEM;

    test_add_ndn_action_drop(ndn_actions, 0);

    rpc_rte_mk_flow_rule_components(rpcs, ndn_actions, NULL,
                                    NULL, actions);

    asn_free_value(ndn_actions);
    return 0;
}

te_errno
test_mk_txmode_txconf(struct test_ethdev_config   *tecp,
                      uint64_t                     test_offloads,
                      struct tarpc_rte_eth_txmode *txmodep,
                      struct tarpc_rte_eth_txconf *txconfp)
{
    int                            dv = rpc_dpdk_get_version(tecp->rpcs);
    struct tarpc_rte_eth_dev_info *di = &tecp->dev_info;
    struct tarpc_rte_eth_txmode    txmode;
    struct tarpc_rte_eth_txconf    txconf;
    uint32_t                       novlan;
    uint64_t                       test_offload_tso;
    uint64_t                       test_offload_vxlan_tso;
    uint64_t                       test_offload_geneve_tso;
    uint64_t                       test_offload_vlan;
    uint64_t                       test_offload_multiseg;
    uint64_t                       test_offload_cksum_ip;
    uint64_t                       test_offload_cksum_tcp;
    uint64_t                       test_offload_cksum_udp;
    uint64_t                       test_offload_cksum_ip_outer;
    uint64_t                       test_offload_cksum_udp_outer;

    memset(&txmode, 0, sizeof(txmode));
    memcpy(&txconf, &di->default_txconf, sizeof(txconf));

    novlan = (1U << TARPC_RTE_ETH_TXQ_FLAGS_NOVLANOFFL_BIT);

    test_offload_vlan = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_VLAN_INSERT_BIT);
    test_offload_tso = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_TCP_TSO_BIT);
    test_offload_vxlan_tso =
                        (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_VXLAN_TNL_TSO_BIT);
    test_offload_geneve_tso =
                        (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_GENEVE_TNL_TSO_BIT);
    test_offload_multiseg = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_MULTI_SEGS_BIT);
    test_offload_cksum_ip = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_IPV4_CKSUM_BIT);
    test_offload_cksum_tcp = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_TCP_CKSUM_BIT);
    test_offload_cksum_udp = (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_UDP_CKSUM_BIT);
    test_offload_cksum_ip_outer =
                    (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_OUTER_IPV4_CKSUM_BIT);
    test_offload_cksum_udp_outer =
                    (1ULL << TARPC_RTE_ETH_TX_OFFLOAD_OUTER_UDP_CKSUM_BIT);

    if ((test_offloads & test_offload_vlan) == 0)
    {
        txconf.txq_flags |= novlan;
    }
    else if ((di->tx_offload_capa & test_offload_vlan) == 0)
    {
        WARN_VERDICT("TX VLAN insertion is not available");
        return TE_EINVAL;
    }
    else
    {
        txconf.txq_flags &= ~novlan;
    }

    if ((test_offloads & test_offload_tso) == test_offload_tso &&
        (di->tx_offload_capa & test_offload_tso) == 0)
    {
        WARN_VERDICT("TCP TSO is not available");
        return TE_EINVAL;
    }

    if ((test_offloads & test_offload_vxlan_tso) != 0 &&
        (di->tx_offload_capa & test_offload_vxlan_tso) == 0)
    {
        WARN_VERDICT("VxLAN TSO is not available");
        return TE_EINVAL;
    }

    if ((test_offloads & test_offload_geneve_tso) != 0 &&
        (di->tx_offload_capa & test_offload_geneve_tso) == 0)
    {
        WARN_VERDICT("Geneve TSO is not available");
        return TE_EINVAL;
    }

    if ((test_offloads & test_offload_cksum_ip) == test_offload_cksum_ip &&
        (di->tx_offload_capa & test_offload_cksum_ip) == 0)
    {
        WARN_VERDICT("Tx IP checksum offload is unsupported");
        return TE_EINVAL;
    }

    if ((test_offloads & test_offload_cksum_tcp) == test_offload_cksum_tcp &&
        (di->tx_offload_capa & test_offload_cksum_tcp) == 0)
    {
        WARN_VERDICT("Tx TCP checksum offload is unsupported");
        return TE_EINVAL;
    }

    if ((test_offloads & test_offload_cksum_udp) == test_offload_cksum_udp &&
        (di->tx_offload_capa & test_offload_cksum_udp) == 0)
    {
        WARN_VERDICT("Tx UDP checksum offload is unsupported");
        return TE_EINVAL;
    }

    if ((test_offloads & test_offload_cksum_ip_outer) ==
        test_offload_cksum_ip_outer &&
        (di->tx_offload_capa & test_offload_cksum_ip_outer) == 0)
    {
        WARN_VERDICT("Tx outer IP checksum offload is unsupported");
        return TE_EINVAL;
    }

    if ((test_offloads & test_offload_cksum_udp_outer) ==
        test_offload_cksum_udp_outer &&
        (di->tx_offload_capa & test_offload_cksum_udp_outer) == 0)
    {
        WARN_VERDICT("Tx outer UDP checksum offload is unsupported");
        return TE_EINVAL;
    }

    if (dv < TAPI_RTE_VERSION_NUM(17,11,0,1))
    {
        uint32_t noms = (1U << TARPC_RTE_ETH_TXQ_FLAGS_NOMULTSEGS_BIT);
        uint32_t noxtcp = (1U << TARPC_RTE_ETH_TXQ_FLAGS_NOXSUMTCP_BIT);
        uint32_t noxudp = (1U << TARPC_RTE_ETH_TXQ_FLAGS_NOXSUMUDP_BIT);

        if ((test_offloads & test_offload_multiseg) == 0)
        {
            txconf.txq_flags |= noms;
        }
        else if ((di->default_txconf.txq_flags & noms) == noms)
        {
            WARN_VERDICT("Mbuf segmentation is prohibited");
            return TE_EINVAL;
        }
        else
        {
            txconf.txq_flags &= ~noms;
        }

        if ((test_offloads & test_offload_cksum_tcp) == test_offload_cksum_tcp)
            txconf.txq_flags &= ~noxtcp;
        else
            txconf.txq_flags |= noxtcp;

        if ((test_offloads & test_offload_cksum_udp) == test_offload_cksum_udp)
            txconf.txq_flags &= ~noxudp;
        else
            txconf.txq_flags |= noxudp;
    }
    else
    {
        if ((test_offloads & test_offload_multiseg) == test_offload_multiseg &&
            (di->tx_offload_capa & test_offload_multiseg) == 0)
        {
            WARN_VERDICT("Mbuf segmentation is prohibited");
            return TE_EINVAL;
        }

        txmode.offloads = test_offloads & ~di->tx_queue_offload_capa;
        txconf.offloads = test_offloads & di->tx_queue_offload_capa;

        test_workaround_tx_fixed_offloads(tecp->rpcs, &txmode);

        txconf.txq_flags = 0;
        if (dv < TAPI_RTE_VERSION_NUM(18,11,0,0))
            txconf.txq_flags |= (1U << TARPC_RTE_ETH_TXQ_FLAGS_IGNORE_BIT);
    }

    if (txmodep != NULL)
        memcpy(txmodep, &txmode, sizeof(*txmodep));

    if (txconfp != NULL)
        memcpy(txconfp, &txconf, sizeof(*txconfp));

    return 0;
}


te_errno
test_mk_rte_flow_action_queue(rcf_rpc_server *rpcs,
                              uint16_t queue,
                              rpc_rte_flow_action_p *actions)
{
    asn_value *ndn_actions;

    ndn_actions = asn_init_value(ndn_rte_flow_actions);
    if (ndn_actions == NULL)
        return TE_ENOMEM;

    test_add_ndn_action_queue(ndn_actions, 0, queue);

    rpc_rte_mk_flow_rule_components(rpcs, ndn_actions, NULL,
                                    NULL, actions);

    asn_free_value(ndn_actions);
    return 0;
}

void
test_get_rss_reta(rcf_rpc_server *rpcs,
                  uint16_t port_id,
                  uint64_t *reta_size,
                  struct tarpc_rte_eth_rss_reta_entry64 **reta_conf)
{
    struct tarpc_rte_eth_dev_info dev_info;
    unsigned int i;
    te_errno rc;

    TEST_SUBSTEP("Refresh device info to pick up RSS reta size changes");
    rpc_rte_eth_dev_info_get(rpcs, port_id, &dev_info);

    TEST_SUBSTEP("Try to get entire RSS RETA");
    *reta_conf = tapi_calloc(TE_DIV_ROUND_UP(dev_info.reta_size,
                                             RPC_RTE_RETA_GROUP_SIZE),
                             sizeof(**reta_conf));
    for (i = 0;
         i < TE_DIV_ROUND_UP(dev_info.reta_size, RPC_RTE_RETA_GROUP_SIZE);
         i++)
    {
        (*reta_conf)[i].mask = ~0;
    }

    RPC_AWAIT_IUT_ERROR(rpcs);
    rc = rpc_rte_eth_dev_rss_reta_query(rpcs, port_id, *reta_conf,
                                        dev_info.reta_size);

    if (-rc == TE_RC(TE_RPC, TE_EOPNOTSUPP))
    {
        WARN("RSS RETA query operation is not supported");
        TEST_SUBSTEP("If RSS RETA query is not supported, fill in dummy table");
        for (i = 0; i < dev_info.reta_size; i++)
        {
            unsigned int group_index = i / RPC_RTE_RETA_GROUP_SIZE;
            unsigned int entry_index = i % RPC_RTE_RETA_GROUP_SIZE;

            (*reta_conf)[group_index].reta[entry_index] =
                i % dev_info.nb_rx_queues;
        }
    }
    else if (rc < 0)
    {
        TEST_VERDICT("Query RSS RETA operation failed");
    }

    *reta_size = dev_info.reta_size;
}

extern void
test_start_tx_queue(rcf_rpc_server *rpcs, uint16_t port_id, uint16_t queue_id)
{
    te_errno rc;

    RPC_AWAIT_IUT_ERROR(rpcs);
    rc = rpc_rte_eth_dev_tx_queue_start(rpcs, port_id, queue_id);
    if (-rc == TE_RC(TE_RPC, TE_EOPNOTSUPP))
    {
        TEST_SKIP("Tx queue start operation is not supported");
    }
    else if (rc != 0)
    {
        TEST_VERDICT("Tx queue start operation failed");
    }
}

extern void
test_start_rx_queue(rcf_rpc_server *rpcs, uint16_t port_id, uint16_t queue_id)
{
    te_errno rc;

    RPC_AWAIT_IUT_ERROR(rpcs);
    rc = rpc_rte_eth_dev_rx_queue_start(rpcs, port_id, queue_id);
    if (-rc == TE_RC(TE_RPC, TE_EOPNOTSUPP))
    {
        TEST_SKIP("Rx queue start operation is not supported");
    }
    else if (rc != 0)
    {
        TEST_VERDICT("Rx queue start operation failed");
    }
}

extern void
test_setup_rss_configuration(tarpc_rss_hash_protos_t hf,
                             te_bool regular,
                             struct tarpc_rte_eth_rss_conf *rss_conf)
{
    unsigned int i;
    static const uint8_t regular_key[RPC_RSS_HASH_KEY_LEN_DEF] = {
        0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
        0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
        0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
        0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
        0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
    };

    rss_conf->rss_hf = hf;
    rss_conf->rss_key.rss_key_len = RPC_RSS_HASH_KEY_LEN_DEF;
    rss_conf->rss_key.rss_key_val = tapi_malloc(RPC_RSS_HASH_KEY_LEN_DEF);
    rss_conf->rss_key_len = RPC_RSS_HASH_KEY_LEN_DEF;

    if (regular)
    {
        memcpy(rss_conf->rss_key.rss_key_val, regular_key,
               RPC_RSS_HASH_KEY_LEN_DEF);
    }
    else
    {
        for (i = 0; i < RPC_RSS_HASH_KEY_LEN_DEF; i++)
            rss_conf->rss_key.rss_key_val[i] = rand();
    }
}

extern struct tarpc_rte_eth_rss_conf *
test_try_get_rss_hash_conf(rcf_rpc_server *rpcs, uint16_t port_id)
{
    te_errno rc;
    struct tarpc_rte_eth_rss_conf *rss_conf;

    rss_conf = tapi_malloc(sizeof(*rss_conf));
    rss_conf->rss_key.rss_key_val = tapi_malloc(RPC_RSS_HASH_KEY_LEN_DEF);
    rss_conf->rss_key.rss_key_len = RPC_RSS_HASH_KEY_LEN_DEF;
    rss_conf->rss_key_len = RPC_RSS_HASH_KEY_LEN_DEF;

    RPC_AWAIT_IUT_ERROR(rpcs);
    rc = rpc_rte_eth_dev_rss_hash_conf_get(rpcs, port_id, rss_conf);

    if (rc == 0)
        return rss_conf;
    else if (-rc != TE_RC(TE_RPC, TE_EOPNOTSUPP))
        TEST_VERDICT("RSS hash configuration get operation failed");

    free(rss_conf->rss_key.rss_key_val);
    free(rss_conf);
    return NULL;
}


extern void
test_send_and_match_one_packet_custom_verdicts(rcf_rpc_server *rpcs,
                                               uint16_t port_id,
                                               const char *tst_ta,
                                               const char *tst_if_name,
                                               rpc_rte_mbuf_p *mbufs,
                                               asn_value *tmpl,
                                               unsigned int queue,
                                               unsigned int packet_expected,
                                               const char *verdict_no_pkts,
                                               const char *verdict_known_unexp)
{
    asn_value      *ptrn = NULL;
    unsigned int    i;

    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_ta, 0, tst_if_name,
                                                tmpl, NULL, &ptrn));

    CHECK_RC(test_rx_burst_match_pattern_custom_verdicts(rpcs, port_id, queue,
                        mbufs, BURST_SIZE, packet_expected, ptrn, TRUE,
                        verdict_no_pkts, verdict_known_unexp));
    for (i = 0; i < packet_expected; i++)
    {
        if (mbufs[i] != RPC_NULL)
            rpc_rte_pktmbuf_free(rpcs, mbufs[i]);
    }
}

extern void
test_send_and_match_one_packet(rcf_rpc_server *rpcs, uint16_t port_id,
                               const char *tst_ta, const char *tst_if_name,
                               rpc_rte_mbuf_p *mbufs, asn_value *tmpl,
                               unsigned int queue, unsigned int packet_expected)
{
    test_send_and_match_one_packet_custom_verdicts(rpcs, port_id, tst_ta,
                                                   tst_if_name, mbufs, tmpl,
                                                   queue, packet_expected,
                                                   NULL, NULL);
}

extern const char *
test_get_ethdev_state_name(test_ethdev_state state)
{
    unsigned int i;
    static const struct param_map_entry map[] = { ETHDEV_STATE_MAPPING_LIST };

    for(i = 0; i < TE_ARRAY_LEN(map); i++)
    {
        if (map[i].num_val == (int)state)
            return map[i].str_val;
    }

    return NULL;
}

uint32_t
test_get_template_packet_length(rcf_rpc_server *rpcs, asn_value *tmpl,
                                rpc_rte_mempool_p mp)
{
    rpc_rte_mbuf_p *mbufs;
    unsigned int n_mbufs;
    uint32_t packet_length;
    unsigned int i;

    (void)rpc_rte_mk_mbuf_from_template(rpcs, tmpl, mp, &mbufs, &n_mbufs);

    if (n_mbufs < 1)
        TEST_VERDICT("Could not create mbufs from template");

    packet_length = rpc_rte_pktmbuf_get_pkt_len(rpcs, mbufs[0]);

    for (i = 0; i < n_mbufs; i++)
    {
        if (mbufs[i] != RPC_NULL)
            rpc_rte_pktmbuf_free(rpcs, mbufs[i]);
    }

    return packet_length;
}

extern unsigned int
test_get_template_header_length(rcf_rpc_server *rpcs, asn_value *tmpl,
                                rpc_rte_mempool_p mp)
{
    rpc_rte_mbuf_p *mbufs;
    unsigned int n_mbufs;
    struct tarpc_rte_pktmbuf_tx_offload tx_offload;
    unsigned int i;

    (void)rpc_rte_mk_mbuf_from_template(rpcs, tmpl, mp, &mbufs, &n_mbufs);

    if (n_mbufs < 1)
        TEST_VERDICT("Could not create mbufs from template");

    memset(&tx_offload, 0, sizeof(tx_offload));
    rpc_rte_pktmbuf_get_tx_offload(rpcs, mbufs[0], &tx_offload);

    for (i = 0; i < n_mbufs; i++)
    {
        if (mbufs[i] != RPC_NULL)
            rpc_rte_pktmbuf_free(rpcs, mbufs[i]);
    }

    return tx_offload.l2_len + tx_offload.l3_len + tx_offload.l4_len;
}

te_bool
test_conf_tx_offload_supported(unsigned long long offload_bit)
{
    te_errno rc;
    const char *offload_name = rpc_dpdk_offloads_tx_get_name(offload_bit);

    rc = cfg_find_fmt(NULL, "/local:/dpdk:/offloads:/dev:/tx:/supported:%s",
                      offload_name);
    if (rc != 0)
    {
        if (TE_RC_GET_ERROR(rc) != TE_ENOENT)
        {
            TEST_VERDICT("Failed to find out supported by IUT offload %s",
                      offload_name);
        }

        return FALSE;
    }

    return TRUE;
}

te_bool
test_conf_rx_offload_supported(unsigned long long offload_bit)
{
    te_errno rc;
    const char *offload_name = rpc_dpdk_offloads_rx_get_name(offload_bit);

    rc = cfg_find_fmt(NULL, "/local:/dpdk:/offloads:/dev:/rx:/supported:%s",
                      offload_name);
    if (rc != 0)
    {
        if (TE_RC_GET_ERROR(rc) != TE_ENOENT)
        {
            TEST_VERDICT("Failed to find out supported by IUT offload %s",
                      offload_name);
        }

        return FALSE;
    }

    return TRUE;
}

void
test_mk_pattern_and_tmpl_by_flow_rule_pattern(rcf_rpc_server *rpcs,
                                     const asn_value *flow_rule_pattern,
                                     rpc_rte_flow_item_p *pattern,
                                     asn_value **tmpl, uint32_t *match_fields)
{
    asn_value *flow_rule_pattern_copy;
    rpc_rte_flow_item_p ptrn;
    te_errno rc;

    flow_rule_pattern_copy = asn_copy_value(flow_rule_pattern);

    RPC_AWAIT_IUT_ERROR(rpcs);
    rc = rpc_rte_mk_flow_rule_components(rpcs, flow_rule_pattern, NULL,
                                         &ptrn, NULL);
    if (rc == -TE_RC(TE_RPCS, TE_EPROTONOSUPPORT))
        TEST_SKIP("The protocol used in the flow rule is not supported");
    if (rc != 0)
        TEST_VERDICT("Failed to make a flow rule pattern");

    CHECK_RC(test_mk_template_get_match_fields_from_flow_ndn(
                flow_rule_pattern_copy, tmpl, match_fields));
    *pattern = ptrn;
}

te_errno
test_generate_changed_flow_patterns(const asn_value *flow_rule_pattern,
                                    const char *field_path,
                                    size_t n_changed_patterns,
                                    asn_value **changed_patterns)
{
    asn_syntax supported_syntaxes[] = { INTEGER, UINTEGER, OCT_STRING };
    uint8_t field_data[32];
    size_t n_created = 0;
    uint8_t orig_data;
    size_t data_len = sizeof(field_data);
    asn_syntax field_syntax;
    const asn_type *type;
    te_bool is_supported;
    size_t i;
    te_errno rc;

    rc = asn_get_subtype(asn_get_type(flow_rule_pattern), &type, field_path);
    if (rc != 0)
        goto err;

    field_syntax = asn_get_syntax_of_type(type);
    for (i = 0, is_supported = FALSE; i < TE_ARRAY_LEN(supported_syntaxes); i++)
    {
        if (field_syntax == supported_syntaxes[i])
        {
            is_supported = TRUE;
            break;
        }
    }

    if (!is_supported)
    {
        ERROR("ASN.1 syntax %d is not supported", field_syntax);
        rc = TE_EINVAL;
        goto err;
    }

    rc = asn_read_value_field(flow_rule_pattern, field_data, &data_len, field_path);
    if (rc != 0)
        goto err;

    orig_data = field_data[0];
    for (i = 0; i < n_changed_patterns; i++)
    {
        if (++field_data[0] == orig_data)
        {
            ERROR("Too many changed patterns requested");
            rc = TE_EINVAL;
            goto err;
        }

        changed_patterns[i] = asn_copy_value(flow_rule_pattern);
        if (changed_patterns[i] == NULL)
        {
            rc = TE_ENOMEM;
            goto err;
        }
        n_created++;

        rc = asn_write_value_field(changed_patterns[i], field_data, data_len, field_path);
        if (rc != 0)
            goto err;
    }

    return 0;

err:

    for (i = 0; i < n_created; i++)
    {
        asn_free_value(changed_patterns[i]);
        changed_patterns[i] = NULL;
    }

    return rc;
}

asn_value *
test_concatenate_tmpl_ptrn_pdus(const asn_value *dst, const asn_value *src,
                                const char *label)
{
    asn_value *result;
    asn_value *pdu;
    int n_src;
    int i;

    CHECK_NOT_NULL(result = asn_copy_value(dst));

    n_src = asn_get_length(src, label);
    if (n_src < 0)
        TEST_VERDICT("Failed to get number of PDUs in a ASN.1 value");

    for (i = 0; i < n_src; i++)
    {
        CHECK_RC(asn_get_indexed(src, &pdu, i, label));
        CHECK_RC(asn_insert_indexed(result, asn_copy_value(pdu), i, label));
    }

    return result;
}

asn_value *
test_decap_tmpl_ptrn_pdus(const asn_value *val_orig, const char *label)
{
    asn_value     *result;
    asn_value     *pdus;
    unsigned int   nb_pdus_i;
    asn_value    **pdus_i;
    unsigned int   i;

    CHECK_NOT_NULL(result = asn_copy_value(val_orig));
    CHECK_RC(asn_free_subvalue(result, label));

    CHECK_RC(asn_get_descendent(val_orig, &pdus, label));
    CHECK_RC(tapi_tad_pdus_relist_outer_inner(pdus, NULL, NULL,
                                              &nb_pdus_i, &pdus_i));

    for (i = 0; i < nb_pdus_i; ++i) {
        asn_value *pdu_copy;

        CHECK_NOT_NULL(pdu_copy = asn_copy_value(pdus_i[i]));
        CHECK_RC(asn_insert_indexed(result, pdu_copy, i, label));
    }

    free(pdus_i);

    return result;
}

void
test_check_flow_query_data(const tarpc_rte_flow_query_data *data,
                           te_bool check_hits, uint64_t hits,
                           te_bool check_bytes, uint64_t bytes)
{
    if (check_hits)
    {
        if (!data->tarpc_rte_flow_query_data_u.count.hits_set)
            TEST_VERDICT("Counter hits field is not set");

        if (data->tarpc_rte_flow_query_data_u.count.hits != hits)
        {
            TEST_VERDICT("Wrong number of hits, actual: %lu, expected: %lu",
                         data->tarpc_rte_flow_query_data_u.count.hits, hits);
        }
    }

    if (check_bytes)
    {
        if (!data->tarpc_rte_flow_query_data_u.count.bytes_set)
            TEST_VERDICT("Counter bytes field is not set");

        if (data->tarpc_rte_flow_query_data_u.count.bytes != bytes)
        {
            TEST_VERDICT("Wrong number of bytes, actual: %lu, expected: %lu",
                         data->tarpc_rte_flow_query_data_u.count.bytes, bytes);
        }
    }
}

void
test_add_and_mk_rte_flow_action_count(uint32_t counter_id,
                                      int action_id, rcf_rpc_server *rpcs,
                                      asn_value *ndn_actions,
                                      rpc_rte_flow_action_p *rte_actions)
{
    asn_value *new_actions;

    tapi_rte_flow_add_ndn_action_count(ndn_actions, action_id, counter_id);

    CHECK_NOT_NULL(new_actions = asn_init_value(ndn_rte_flow_actions));
    tapi_rte_flow_add_ndn_action_count(new_actions, 0, counter_id);

    rpc_rte_mk_flow_rule_components(rpcs, new_actions, NULL,
                                    NULL, rte_actions);

}

struct test_transceiver *
test_transceiver_net_init(const char *ta, const char *if_name)
{
    struct test_transceiver *result;

    CHECK_NOT_NULL(ta);
    CHECK_NOT_NULL(if_name);

    result = tapi_malloc(sizeof(*result));

    result->type = TEST_TRANSCEIVER_NET;
    result->trsc.net.ta = tapi_strdup(ta);
    result->trsc.net.if_name = tapi_strdup(if_name);
    result->trsc.net.rx_csap = CSAP_INVALID_HANDLE;

    return result;
}

struct test_transceiver *
test_transceiver_dpdk_init(rcf_rpc_server *rpcs, uint16_t port_id,
                           rpc_rte_mempool_p mp)
{
    struct test_transceiver *result;

    CHECK_NOT_NULL(rpcs);

    result = tapi_malloc(sizeof(*result));

    result->type = TEST_TRANSCEIVER_DPDK;
    result->trsc.dpdk.rpcs = rpcs;
    result->trsc.dpdk.port_id = port_id;
    result->trsc.dpdk.mp = mp;

    return result;
}

void
test_transceiver_free(struct test_transceiver *trsc)
{
    if (trsc == NULL)
        return;

    switch (trsc->type)
    {
        case TEST_TRANSCEIVER_DPDK:
            break;
        case TEST_TRANSCEIVER_NET:
            free(trsc->trsc.net.ta);
            free(trsc->trsc.net.if_name);
            break;
        default:
            WARN("%s: Invalid transceiver type", __FUNCTION__);
            break;
    }

    free(trsc);
}

struct test_transceiver_exchange *
test_transceiver_exchange_init(const asn_value *tmpl_tx,
                               test_transceiver_transform_tmpl transform_tmpl,
                               void *tmpl_data,
                               test_transceiver_transform_ptrn transform_ptrn,
                               void *ptrn_data)
{
    struct test_transceiver_exchange *result;

    result = tapi_calloc(1, sizeof(*result));

    CHECK_NOT_NULL(result->tmpl_tx = asn_copy_value(tmpl_tx));

    result->transform_tmpl = transform_tmpl;
    result->tmpl_data = tmpl_data;
    result->transform_ptrn = transform_ptrn;
    result->ptrn_data = ptrn_data;

    return result;
}

void
test_transceiver_exchange_set_verdicts(
                                struct test_transceiver_exchange *exchange,
                                const char *verdict_no_pkts,
                                const char *verdict_known_unexp)
{
    free(exchange->verdict_no_pkts);
    free(exchange->verdict_known_unexp);

    exchange->verdict_no_pkts = verdict_no_pkts == NULL ? NULL :
                                tapi_strdup(verdict_no_pkts);
    exchange->verdict_known_unexp = verdict_known_unexp == NULL ? NULL :
                                    tapi_strdup(verdict_known_unexp);
}


void
test_transceiver_exchange_free(struct test_transceiver_exchange *exchange)
{
    if (exchange == NULL)
        return;

    free(exchange->verdict_no_pkts);
    free(exchange->verdict_known_unexp);
    free(exchange->tmpl_tx);
    free(exchange);
}

static asn_value *
test_transceiver_do_transform_tmpl(test_transceiver_transform_tmpl transform,
                                   void *tmpl_data,
                                   const asn_value *tmpl_tx)
{
    asn_value *result;

    if (transform == NULL)
        CHECK_NOT_NULL(result = asn_copy_value(tmpl_tx));
    else
        CHECK_NOT_NULL(result = transform(tmpl_tx, tmpl_data));

    return result;
}

static asn_value *
test_transceiver_do_transform_ptrn(test_transceiver_transform_ptrn transform,
                                   void *ptrn_data,
                                   const asn_value *ptrn_tx)
{
    asn_value *result;

    if (transform == NULL)
        CHECK_NOT_NULL(result = asn_copy_value(ptrn_tx));
    else
        CHECK_NOT_NULL(result = transform(ptrn_tx, ptrn_data));

    return result;
}

void
test_transceiver_exchange_commit(const struct test_transceiver_exchange *exchange,
                                 struct test_transceiver *tx,
                                 unsigned int n_tx_pkts, unsigned int tx_queue,
                                 struct test_transceiver *rx,
                                 unsigned int n_rx_pkts, unsigned int rx_queue)
{
    csap_handle_t rx_csap = CSAP_INVALID_HANDLE;
    asn_value *rx_tmpl = NULL;
    asn_value *rx_ptrn = NULL;
    asn_value *tx_ptrn = NULL;
    rpc_rte_mbuf_p *mbufs = NULL;
    unsigned int n_mbufs = 0;


    if (rx->type == TEST_TRANSCEIVER_NET && tx->type == TEST_TRANSCEIVER_NET &&
        exchange->transform_tmpl != NULL && exchange->transform_ptrn != NULL)
    {
        TEST_VERDICT("Net-Net exchange with transformations is not supported");
    }

    switch (tx->type)
    {
        case TEST_TRANSCEIVER_DPDK:
            tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(tx->trsc.dpdk.rpcs, exchange->tmpl_tx,
                                             tx->trsc.dpdk.mp, NULL,
                                             &mbufs, &n_mbufs, &tx_ptrn);
            if (n_mbufs != n_tx_pkts)
            {
                TEST_VERDICT("Failed to create %u mbufs, %u created", n_tx_pkts,
                          n_mbufs);
            }

            rx_tmpl = test_transceiver_do_transform_tmpl(exchange->transform_tmpl,
                            exchange->tmpl_data, exchange->tmpl_tx);
            rx_ptrn = test_transceiver_do_transform_ptrn(exchange->transform_ptrn,
                            exchange->ptrn_data, tx_ptrn);
            break;
        case TEST_TRANSCEIVER_NET:
            if (rx->type == TEST_TRANSCEIVER_NET)
            {
                /* Only no transformations is supported */
                CHECK_NOT_NULL(rx_tmpl = asn_copy_value(exchange->tmpl_tx));
                CHECK_NOT_NULL(rx_ptrn = tapi_tad_mk_pattern_from_template(rx_tmpl));
            }
            break;
        default:
            TEST_VERDICT("Invalid trsc type");
    }


    switch (rx->type)
    {
        case TEST_TRANSCEIVER_NET:
        {
            if (rx_tmpl == NULL || rx_ptrn == NULL)
                TEST_VERDICT("Template or pattern was not prepared before csap create");

            CHECK_RC(tapi_eth_based_csap_create_by_tmpl(rx->trsc.net.ta, 0,
                                                        rx->trsc.net.if_name,
                                                        TAD_ETH_RECV_DEF,
                                                        rx_tmpl, &rx_csap));

            CHECK_RC(tapi_tad_trrecv_start(rx->trsc.net.ta, 0, rx_csap, rx_ptrn,
                                           TAD_TIMEOUT_INF, 0,
                                           RCF_TRRECV_PACKETS |
                                           RCF_TRRECV_MISMATCH));
            break;
        }
        case TEST_TRANSCEIVER_DPDK:
            break;
        default:
            TEST_VERDICT("Invalid trsc type");
    }

    switch (tx->type)
    {
        case TEST_TRANSCEIVER_NET:
            CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tx->trsc.net.ta, 0,
                                           tx->trsc.net.if_name,
                                           exchange->tmpl_tx, NULL, &tx_ptrn));
            if (tx_ptrn == NULL)
                TEST_VERDICT("Failed to sniff pattern");

            break;
        case TEST_TRANSCEIVER_DPDK:
        {
            if (n_mbufs == 0)
                TEST_VERDICT("Packet mbufs were not created before Tx burst");

            if (test_tx_prepare_and_burst(tx->trsc.dpdk.rpcs,
                                          tx->trsc.dpdk.port_id,
                                          tx_queue, mbufs, n_mbufs) != n_mbufs)
            {
                TEST_VERDICT("Tx prepare and burst failed");
            }
            break;
        }
        default:
            TEST_VERDICT("Invalid trsc type");
    }

    switch (rx->type)
    {
        case TEST_TRANSCEIVER_NET:
        {
            unsigned int received;

            if (rx_csap == CSAP_INVALID_HANDLE)
                TEST_VERDICT("Failed to receive a packet when Rx CSAP is not running");

            CHECK_RC(test_rx_await_pkts(rx->trsc.net.ta, rx_csap, n_rx_pkts, 0));
            CHECK_RC(tapi_tad_trrecv_stop(rx->trsc.net.ta, 0, rx_csap, NULL,
                                          &received));

            if (received > n_rx_pkts)
            {
                ERROR_VERDICT("%s", exchange->verdict_known_unexp == NULL ?
                              "Known but unexpected packets received" :
                              exchange->verdict_known_unexp);
            }
            if (n_rx_pkts != 0 && received == 0)
            {
                ERROR_VERDICT("%s", exchange->verdict_no_pkts == NULL ?
                              "There are no received packets" :
                              exchange->verdict_no_pkts);
            }
            CHECK_MATCHED_PACKETS_NUM(received, n_rx_pkts);
            break;
        }
        case TEST_TRANSCEIVER_DPDK:
        {
            rpc_rte_mbuf_p *rx_mbufs;
            unsigned int i;
            unsigned int burst_size = TE_ALIGN(n_rx_pkts + 1, BURST_SIZE);

            rx_mbufs = tapi_calloc(burst_size, sizeof(*rx_mbufs));

            if (rx_ptrn == NULL)
            {
                rx_ptrn = test_transceiver_do_transform_ptrn(exchange->transform_ptrn,
                                exchange->ptrn_data, tx_ptrn);
            }

            CHECK_RC(test_rx_burst_match_pattern_custom_verdicts(
                                             rx->trsc.dpdk.rpcs,
                                             rx->trsc.dpdk.port_id,
                                             rx_queue, rx_mbufs,
                                             burst_size, n_rx_pkts,
                                             rx_ptrn, TRUE,
                                             exchange->verdict_no_pkts,
                                             exchange->verdict_known_unexp));

            for (i = 0; i < burst_size; i++)
            {
                if (rx_mbufs[i] != RPC_NULL)
                    rpc_rte_pktmbuf_free(rx->trsc.dpdk.rpcs, rx_mbufs[i]);
            }

            break;
        }
        default:
            TEST_VERDICT("Invalid trsc type");
    }

    asn_free_value(rx_tmpl);
    asn_free_value(rx_ptrn);
    asn_free_value(tx_ptrn);
}

void
test_transciever_simple_exchange_commit(const asn_value *tmpl,
                                        struct test_transceiver *tx,
                                        unsigned int n_tx_pkts,
                                        unsigned int tx_queue,
                                        struct test_transceiver *rx,
                                        unsigned int n_rx_pkts,
                                        unsigned int rx_queue,
                                        const char *verdict_no_pkts,
                                        const char *verdict_known_unexp)
{
    struct test_transceiver_exchange *exchange;

    exchange = test_transceiver_exchange_init(tmpl, NULL, NULL, NULL, NULL);
    test_transceiver_exchange_set_verdicts(exchange, verdict_no_pkts,
                                           verdict_known_unexp);

    test_transceiver_exchange_commit(exchange, tx, n_tx_pkts, tx_queue, rx,
                                     n_rx_pkts, rx_queue);

    test_transceiver_exchange_free(exchange);
}


void
test_check_xstat(const char *name, uint64_t value,
                 struct tarpc_rte_eth_xstat_name *xstats_names,
                 struct tarpc_rte_eth_xstat *xstats, int nb_xstats,
                 te_string *wrong_xstats, te_string *zero_xstats)
{
    uint64_t xstat_value;

    if (test_get_xstat_by_name(name, xstats_names, xstats,
                               nb_xstats, &xstat_value) != 0)
    {
        RING_VERDICT("Extended statistic %s is not supported", name);
    }
    else
    {
        if (xstat_value != value)
        {
            if (xstat_value == 0)
            {
                if (zero_xstats->len > 0)
                    te_string_append(zero_xstats, ", ");
                te_string_append(zero_xstats, "%s",  name);
            }
            else
            {
                if (wrong_xstats->len > 0)
                    te_string_append(wrong_xstats, ", ");
                te_string_append(wrong_xstats, "%s",  name);
            }
        }
    }
}

static te_bool
test_pdus_mac_is_set(const asn_value *pdus, const char *src_or_dst)
{
    te_errno rc;
    const asn_value *mac;

    mac = asn_find_descendant(pdus, &rc, "0.#eth.%s-addr", src_or_dst);

    if (mac != NULL)
        return TRUE;

    if (rc == 0 || rc == TE_EASNINCOMPLVAL || rc == TE_EASNOTHERCHOICE)
        return FALSE;

    TEST_VERDICT("Failed to get MAC address from template");
    return FALSE;
}

te_bool
test_pdus_dst_mac_is_set(const asn_value *pdus)
{
    return test_pdus_mac_is_set(pdus, "dst");
}

void
test_pdus_set_eth_src_dst(asn_value *container, const char *pdus_path,
                          const uint8_t *src, const uint8_t *dst)
{
    unsigned int i;
    asn_value *pdus;
    struct mac_map_t {
        const char *str;
        const uint8_t *mac;
    } mac_map[] = {{"src", src}, {"dst", dst}};

    CHECK_NOT_NULL(pdus = asn_find_descendant(container, NULL, "%s",
                                              pdus_path));

    for (i = 0; i < TE_ARRAY_LEN(mac_map); i++)
    {
        if (mac_map[i].mac != NULL)
        {
            if (test_pdus_mac_is_set(pdus, mac_map[i].str))
            {
                CHECK_RC(asn_free_subvalue_fmt(pdus, "0.#eth.%s-addr",
                                               mac_map[i].str));
            }

            CHECK_RC(asn_write_value_field_fmt(pdus, mac_map[i].mac,
                                               ETHER_ADDR_LEN,
                                               "0.#eth.%s-addr.#plain",
                                               mac_map[i].str));
        }
    }
}

enum test_rte_eth_mode {
    TEST_RTE_ETH_MODE_PROMISCUOUS,
    TEST_RTE_ETH_MODE_ALL_MULTICAST,
};

static void
test_rte_eth_toggle_mode(rcf_rpc_server *rpcs, uint16_t port_id,
                         enum test_rte_eth_mode mode, te_bool toggle,
                         enum test_op_required required)
{
    const char *operation;
    te_errno rc;
    int got;

    RPC_AWAIT_IUT_ERROR(rpcs);
    switch (mode)
    {
        case TEST_RTE_ETH_MODE_PROMISCUOUS:
            if (toggle)
            {
                rc = rpc_rte_eth_promiscuous_enable(rpcs, port_id);
                operation = "rte_eth_promiscuous_enable";
            }
            else
            {
                rc = rpc_rte_eth_promiscuous_disable(rpcs, port_id);
                operation = "rte_eth_promiscuous_disable";
            }
            break;
        case TEST_RTE_ETH_MODE_ALL_MULTICAST:
            if (toggle)
            {
                rc = rpc_rte_eth_allmulticast_enable(rpcs, port_id);
                operation = "rte_eth_allmulticast_enable";
            }
            else
            {
                rc = rpc_rte_eth_allmulticast_disable(rpcs, port_id);
                operation = "rte_eth_allmulticast_disable";
            }
            break;
        default:
            TEST_VERDICT("Invalid mode");
    }


    if (rc == -TE_RC(TE_RPC, TE_EOPNOTSUPP))
    {
        if (required == TEST_OP_REQUIRED)
        {
            TEST_SKIP("'%s' operation is not supported", operation);
        }
        else
        {
            WARN("'%s' operation is not supported", operation);
            return;
        }
    }
    else if (rc != 0)
    {
        TEST_VERDICT("'%s' operation failed: %r", operation, rc);
    }

    /*
     * Before 19.11-rc1 promiscuous mode controls returned void and it
     * was unclear if requested changes were actially done or not.
     * The same is true for all-multicast.
     */
    switch (mode)
    {
        case TEST_RTE_ETH_MODE_PROMISCUOUS:
            got = rpc_rte_eth_promiscuous_get(rpcs, port_id);
            break;
        case TEST_RTE_ETH_MODE_ALL_MULTICAST:
            got = rpc_rte_eth_allmulticast_get(rpcs, port_id);
            break;
        default:
            TEST_VERDICT("Invalid mode");

    }
    if (got != (toggle ? 1 : 0))
        TEST_VERDICT("'%s' operation result is inconsistent", operation);
}

void
test_rte_eth_promiscuous_enable(rcf_rpc_server *rpcs, uint16_t port_id,
                                enum test_op_required required)
{
    test_rte_eth_toggle_mode(rpcs, port_id, TEST_RTE_ETH_MODE_PROMISCUOUS,
                             TRUE, required);
}

void
test_rte_eth_promiscuous_disable(rcf_rpc_server *rpcs, uint16_t port_id,
                                 enum test_op_required required)
{
    test_rte_eth_toggle_mode(rpcs, port_id, TEST_RTE_ETH_MODE_PROMISCUOUS,
                             FALSE, required);
}

void
test_rte_eth_allmulticast_enable(rcf_rpc_server *rpcs, uint16_t port_id,
                                 enum test_op_required required)
{
    test_rte_eth_toggle_mode(rpcs, port_id, TEST_RTE_ETH_MODE_ALL_MULTICAST,
                             TRUE, required);
}

void
test_rte_eth_allmulticast_disable(rcf_rpc_server *rpcs, uint16_t port_id,
                                  enum test_op_required required)
{
    test_rte_eth_toggle_mode(rpcs, port_id, TEST_RTE_ETH_MODE_ALL_MULTICAST,
                             FALSE, required);
}

asn_value *
test_mk_pattern_by_unit(const asn_value *ptrn, int index)
{
    asn_value *unit;
    asn_value *unit_copy;
    asn_value *new_ptrn;
    int len = asn_get_length(ptrn, "");

    if (len < 0)
        TEST_VERDICT("Get length of pattern failed");

    CHECK_RC(asn_get_indexed(ptrn, &unit, index, ""));
    CHECK_NOT_NULL(unit_copy = asn_copy_value(unit));
    CHECK_NOT_NULL(new_ptrn = asn_init_value(ndn_traffic_pattern));
    CHECK_RC(asn_insert_indexed(new_ptrn, unit_copy, 0, ""));

    return new_ptrn;
}


unsigned int
test_get_extra_tx_descs_per_pkt(void)
{
    const char *path = "/local:/dpdk:/extra_tx_descs_per_packet:";
    cfg_val_type val_type = CVT_INTEGER;
    te_errno rc;
    int val;

    rc = cfg_get_instance_str(&val_type, &val, path);
    if (rc != 0 && TE_RC_GET_ERROR(rc) != TE_ENOENT)
        TEST_VERDICT("Failed to get '%s': %r", path, rc);

    return rc == 0 ? val : 0;
}

unsigned int
test_get_tso_payload_cutoff_barrier(unsigned int hdrs_len)
{
    const char *path = "/local:/dpdk:/tso_cutoff_barrier:";
    cfg_val_type val_type = CVT_INTEGER;
    te_errno rc;
    int val;
    int barrier;

    rc = cfg_get_instance_str(&val_type, &val, path);
    if (rc != 0 && TE_RC_GET_ERROR(rc) != TE_ENOENT)
        TEST_VERDICT("Failed to get '%s': %r", path, rc);

    if (rc == 0)
    {
        barrier = val - hdrs_len;
        return barrier > 0 ? barrier : 0;
    }

    return 0;
}

void
test_get_vf_pci_addrs_by_node(const cfg_net_node_t *node,
                              unsigned int *n_vf_addrs,
                              char ***vf_addrs,
                              unsigned int **vf_ids)
{
    unsigned int n_pfs;
    char **pf_oids;
    cfg_oid **vf_oids;
    unsigned int i;

    CHECK_RC(tapi_cfg_net_node_get_pci_oids(node, &n_pfs, &pf_oids));

    if (n_pfs != 1)
        TEST_VERDICT("Cannot hotplug VFs: only one PCI device per node is supported");

    CHECK_RC(tapi_cfg_pci_get_vfs_of_pf(pf_oids[0], TRUE, n_vf_addrs,
                                        &vf_oids, vf_ids));
    CHECK_RC(tapi_cfg_pci_addr_by_oid_array(*n_vf_addrs,
                                            (const cfg_oid **)vf_oids,
                                            vf_addrs));

    for (i = 0; i < n_pfs; i++)
        free(pf_oids[i]);
    free(pf_oids);

    for (i = 0; i < *n_vf_addrs; i++)
        cfg_free_oid(vf_oids[i]);
    free(vf_oids);
}


/**
 * Find specified @p id in ID list (@p ids).
 *
 * @param id    ID to find
 * @param ids   ID list
 * @param size  Size of the ID list
 *
 * @return index of the entry with the same id in the list or @c UINT_MAX
 */
static unsigned int
test_find_id(unsigned int id, const unsigned int *ids, unsigned int size)
{
    unsigned int i;

    for (i = 0; i < size; i++)
    {
        if (ids[i] == id)
            return i;
    }

    return UINT_MAX;
}

/**
 * Hotplug a PCI device by its address and init a ethdev configuration
 * that represents it. Device arguments for the device are retrieved
 * from configurator.
 *
 * @param[in]  rpcs         RPC server handle
 * @param[in]  env          Environment binding
 * @param[in]  pci_addr     PCI address of the device
 * @param[out] config       Device configuration
 */
static void
test_hotplug_vf(rcf_rpc_server *rpcs, tapi_env *env,
                const char *pci_addr, struct test_ethdev_config *config)
{
    uint16_t port_id;
    char *dev_args;

    CHECK_RC(tapi_rte_get_dev_args_by_pci_addr(rpcs->ta, pci_addr, &dev_args));
    CHECK_RC(tapi_rte_eal_hotplug_add(rpcs, "pci", pci_addr, dev_args));
    rpc_rte_eth_dev_get_port_by_name(rpcs, pci_addr, &port_id);

    test_prepare_config_mk(env, rpcs, pci_addr, port_id, config);

    free(dev_args);
}

void
test_hotplug_vfs_by_ids(rcf_rpc_server *rpcs, tapi_env *env,
                 const cfg_net_node_t *node, unsigned int n_vfs,
                 const unsigned int *vf_ids, struct test_ethdev_config **vfs)
{
    unsigned int n_vf_addrs;
    unsigned int i;
    char **vf_pci_addrs;
    unsigned int *ids;
    unsigned int vf_id;

    test_get_vf_pci_addrs_by_node(node, &n_vf_addrs, &vf_pci_addrs, &ids);

    *vfs = tapi_calloc(n_vf_addrs, sizeof(**vfs));

    for (i = 0; i < n_vfs; i++)
    {
        vf_id = test_find_id(vf_ids[i], ids, n_vf_addrs);
        if (vf_id == UINT_MAX)
            TEST_VERDICT("Failed to find VF with index %u", vf_ids[i]);

        test_hotplug_vf(rpcs, env, vf_pci_addrs[i], &(*vfs)[i]);
    }

    for (i = 0; i < n_vf_addrs; i++)
        free(vf_pci_addrs[i]);

    free(vf_pci_addrs);
    free(ids);
}

/**
 * Build device argument that specifies required port representors.
 * Representors IDs correspond to virtual function IDs.
 *
 * @param[in]  n_rep    Number of representor IDs
 * @param[in]  rep_ids  Representor IDs
 * @param[out] dev_arg  The built device argument
 *
 * @return Status code
 */
static te_errno
test_build_representor_dev_arg(unsigned int n_rep, const unsigned int *rep_ids,
                               char **dev_arg)
{
    te_string result = TE_STRING_INIT;
    unsigned int i;
    te_errno rc;

    for (i = 0; i < n_rep; i++)
    {
        rc = te_string_append(&result, "%s%u%s", i == 0 ? "representor=[" : "",
                              rep_ids[i], i == (n_rep - 1) ? "]" : "");
        if (rc != 0)
        {
            te_string_free(&result);
            return rc;
        }
    }

    *dev_arg = result.ptr;

    return 0;
}

void
test_hotplug_reps(rcf_rpc_server *rpcs, tapi_env *env,
                  const cfg_net_node_t *node,
                  unsigned int n_rep,
                  const unsigned int *rep_ids,
                  struct test_ethdev_config **reps)
{
    unsigned int n_pfs;
    unsigned int n_rep_found;
    char **pf_oid_strs;
    cfg_oid *pf_oid;
    char *pf_addr;
    char *dev_args_pci;
    char *dev_args_rep;
    char *dev_args;
    char **names;
    uint16_t *rep_port_ids;
    unsigned int i;

    *reps = tapi_calloc(n_rep, sizeof(**reps));
    names = tapi_calloc(n_rep, sizeof(*names));

    CHECK_RC(tapi_cfg_net_node_get_pci_oids(node, &n_pfs, &pf_oid_strs));

    if (n_pfs != 1)
        TEST_VERDICT("Hotplug representors: only one PCI device per node supported");

    CHECK_NOT_NULL(pf_oid = cfg_convert_oid_str(pf_oid_strs[0]));
    CHECK_RC(tapi_cfg_pci_addr_by_oid(pf_oid, &pf_addr));
    CHECK_RC(tapi_rte_get_dev_args_by_pci_addr(rpcs->ta, pf_addr,
                                               &dev_args_pci));
    CHECK_RC(test_build_representor_dev_arg(n_rep, rep_ids, &dev_args_rep));

    te_asprintf(&dev_args, "%s%s%s",
                dev_args_pci == NULL ? "" : dev_args_pci,
                dev_args_pci == NULL ? "" : ",", dev_args_rep);
    CHECK_NOT_NULL(dev_args);
    CHECK_RC(tapi_rte_eal_hotplug_add(rpcs, "pci", pf_addr, dev_args));

    rpc_dpdk_find_representors(rpcs, &n_rep_found, &rep_port_ids);
    if (n_rep_found != n_rep)
    {
        TEST_VERDICT("Expected representors spawned: %u, actual: %u",
                  n_rep, n_rep_found);
    }

    for (i = 0; i < n_rep; i++)
    {
        names[i] = tapi_calloc(1, RPC_RTE_ETH_NAME_MAX_LEN);
        rpc_rte_eth_dev_get_name_by_port(rpcs, rep_port_ids[i],
                                         names[i]);
        test_prepare_config_mk(env, rpcs, names[i], rep_port_ids[i],
                               &(*reps)[i]);
    }

    cfg_free_oid(pf_oid);
    free(pf_addr);
    free(rep_port_ids);
    for (i = 0; i < n_pfs; i++)
        free(pf_oid_strs[i]);
    free(pf_oid_strs);
    free(dev_args_pci);
    free(dev_args_rep);
    free(dev_args);
    free(names);
}

te_errno
test_create_traffic_generator_params(const char *ta,
                                     char * arg_prefix,
                                     const char *command_prefix,
                                     const char *mode,
                                     const char *txpkts, te_bool multi_flow,
                                     unsigned int txq,
                                     unsigned int txd, unsigned int burst,
                                     unsigned int txfreet, te_kvpair_h **params,
                                     unsigned int *n_cores)
{
    char *buf;
    te_errno rc;
    te_kvpair_h *result;

    result = tapi_calloc(1, sizeof(*result));

    buf = tapi_malloc(strlen(arg_prefix) + TESTPMD_ARG_MAX_LEN);
    te_kvpair_init(result);

    strcat(strcpy(buf, arg_prefix), "stats_period");
    rc = te_kvpair_add(result, buf, "1");
    if (rc != 0)
        goto cleanup;

    strcat(strcpy(buf, arg_prefix), "forward_mode");
    rc = te_kvpair_add(result, buf, mode);
    if (rc != 0)
        goto cleanup;

    if (strcmp(mode, "txonly") == 0 && multi_flow)
    {
        strcat(strcpy(buf, arg_prefix), "txonly_multi_flow");
        rc = te_kvpair_add(result, buf, "TRUE");
        if (rc != 0)
            goto cleanup;
    }

    if (txq == 0)
    {
        size_t n_threads;

        rc = tapi_cfg_get_all_threads(ta, &n_threads, NULL);
        if (rc != 0)
            goto cleanup;

        if (n_threads == 0)
            TEST_VERDICT("No CPU threads found on %s", ta);

        /* One core is used by testpmd for house-keeping */
        txq = MIN(n_threads - 1, TEST_PERF_TXQ_MAX);
    }

    if (n_cores != NULL)
    {
        /* Use dedicated core for each TxQ */
        *n_cores = txq;
    }

    strcat(strcpy(buf, arg_prefix), "txq");
    rc = te_kvpair_add(result, buf, "%u", txq);
    if (rc != 0)
        goto cleanup;

    if (txq > 1)
    {
        /*
         * Set number of RxQs equal to the number of TxQs to
         * enable all specified forwarding cores.
         */
        strcat(strcpy(buf, arg_prefix), "rxq");
        rc = te_kvpair_add(result, buf, "%u", txq);
        if (rc != 0)
            goto cleanup;
    }

    strcat(strcpy(buf, arg_prefix), "txd");
    rc = te_kvpair_add(result, buf, "%u", txd);
    if (rc != 0)
        goto cleanup;

    strcat(strcpy(buf, command_prefix), "txpkts");
    rc = te_kvpair_add(result, buf, "%s", txpkts);
    if (rc != 0)
        goto cleanup;

    strcat(strcpy(buf, arg_prefix), "burst");
    rc = te_kvpair_add(result, buf, "%u", burst);
    if (rc != 0)
        goto cleanup;

    strcat(strcpy(buf, arg_prefix), "txfreet");
    rc = te_kvpair_add(result, buf, "%u", txfreet);
    if (rc != 0)
        goto cleanup;

    strcat(strcpy(buf, arg_prefix), "no_lsc_interrupt");
    rc = te_kvpair_add(result, buf, "TRUE");
    if (rc != 0)
        goto cleanup;

    *params = result;
cleanup:
    free(buf);

    return rc;
}

te_errno
test_create_traffic_receiver_params(const char *arg_prefix,
                                    const char *command_prefix,
                                    unsigned int rxq,
                                    unsigned int packet_size,
                                    te_kvpair_h **params)
{
    te_string buf = TE_STRING_INIT;
    unsigned int mbuf_size;
    te_kvpair_h *result;
    unsigned int mtu;

    result = tapi_calloc(1, sizeof(*result));
    te_kvpair_init(result);

    te_string_reset(&buf);
    CHECK_RC(te_string_append(&buf, "%s%s", arg_prefix, "rxq"));
    CHECK_RC(te_kvpair_add(result, buf.ptr, "%u", rxq));

    te_string_reset(&buf);
    CHECK_RC(te_string_append(&buf, "%s%s", arg_prefix, "txq"));
    CHECK_RC(te_kvpair_add(result, buf.ptr, "%u", rxq));

    te_string_reset(&buf);
    CHECK_RC(te_string_append(&buf, "%s%s", arg_prefix, "stats_period"));
    CHECK_RC(te_kvpair_add(result, buf.ptr, "1"));

    te_string_reset(&buf);
    CHECK_RC(te_string_append(&buf, "%s%s", arg_prefix, "forward_mode"));
    CHECK_RC(te_kvpair_add(result, buf.ptr, "rxonly"));

    /* This forces testpmd to wait some time for link up before start */
    te_string_reset(&buf);
    CHECK_RC(te_string_append(&buf, "%s%s", arg_prefix, "no_lsc_interrupt"));
    CHECK_RC(te_kvpair_add(result, buf.ptr, "TRUE"));

    /* Flow control is turned off to not limit transmitter performance */
    te_string_reset(&buf);
    CHECK_RC(te_string_append(&buf, "%s%s",
                              command_prefix, "flow_ctrl_autoneg"));
    CHECK_RC(te_kvpair_add(result, buf.ptr, "%s", "off"));

    te_string_reset(&buf);
    CHECK_RC(te_string_append(&buf, "%s%s", command_prefix, "flow_ctrl_rx"));
    CHECK_RC(te_kvpair_add(result, buf.ptr, "%s", "off"));

    te_string_reset(&buf);
    CHECK_RC(te_string_append(&buf, "%s%s", command_prefix, "flow_ctrl_tx"));
    CHECK_RC(te_kvpair_add(result, buf.ptr, "%s", "off"));

    if (tapi_dpdk_mtu_by_pkt_size(packet_size, &mtu))
    {
        te_string_reset(&buf);
        CHECK_RC(te_string_append(&buf, "%s%s", command_prefix, "mtu"));
        CHECK_RC(te_kvpair_add(result, buf.ptr, "%u", mtu));
    }
    if (tapi_dpdk_mbuf_size_by_pkt_size(packet_size, &mbuf_size))
    {
        te_string_reset(&buf);
        CHECK_RC(te_string_append(&buf, "%s%s", arg_prefix, "mbuf_size"));
        CHECK_RC(te_kvpair_add(result, buf.ptr, "%u", mbuf_size));
    }

    *params = result;
    te_string_free(&buf);

    return 0;
}

uint16_t
test_rte_af_packet_on_tst_if_deploy(rcf_rpc_server            *tst_rpcs,
                                    const struct if_nameindex *tst_if,
                                    size_t                     frame_size,
                                    unsigned int               nb_frames)
{
    te_string                 dn = TE_STRING_INIT;
    size_t                    framesz;
    size_t                    blocksz;
    unsigned int              framecnt;
    cfg_val_type              cvt = CVT_INTEGER;
    int                       cv;
    te_string                 da = TE_STRING_INIT;
    uint16_t                  port_id;
    int                       sid;
    struct tarpc_rte_eth_conf dc = {0};
    rpc_rte_mempool_p         mp;

    CHECK_RC(te_string_append(&dn, "net_af_packet%u", tst_if->if_index));

    /* Derive device-specific argument values. */
    framecnt = te_round_up_pow2(nb_frames);
    framesz = te_round_up_pow2(MAX(TPACKET2_HDRLEN + frame_size,
                                   TEST_MIN_PAGE_SIZE));
    blocksz = framesz;

    CHECK_RC(cfg_get_instance_fmt(&cvt, &cv,
                                  "/agent:%s/interface:%s/ring:/rx:/current:",
                                  tst_rpcs->ta, tst_if->if_name));
    if (cv != -1)
    {
        if (cv < (int)nb_frames)
            TEST_SKIP("TST will unlikely manage to receive that many packets");
    }

    CHECK_RC(te_string_append(&da,
                              "iface=%s,framesz=%zu,blocksz=%zu,framecnt=%u",
                              tst_if->if_name, framesz, blocksz, framecnt));

    CHECK_RC(rpc_rte_eal_hotplug_add(tst_rpcs, "vdev", dn.ptr, da.ptr));
    te_string_free(&da);

    CHECK_RC(rpc_rte_eth_dev_get_port_by_name(tst_rpcs, dn.ptr, &port_id));
    te_string_free(&dn);

    sid = rpc_rte_eth_dev_socket_id(tst_rpcs, port_id);

    CHECK_RC(rpc_rte_eth_dev_configure(tst_rpcs, port_id, 1, 1, &dc));

    CHECK_RC(rpc_rte_eth_tx_queue_setup(tst_rpcs, port_id, 0, 0, sid, NULL));

    mp = test_rte_pktmbuf_pool_create(tst_rpcs, TEST_PKTS_MEMPOOL_NAME,
                     MAX(framecnt, TEST_RTE_MEMPOOL_DEF_CACHE * 2),
                     TEST_RTE_MEMPOOL_DEF_CACHE, TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                     framesz + TEST_RTE_MEMPOOL_DATA_ROOM_OVERHEAD, sid);

    CHECK_RC(rpc_rte_eth_rx_queue_setup(tst_rpcs, port_id, 0, 0, sid, NULL, mp));

    /*
     * Promiscuous mode was enabled by the test suite prologue for the whole
     * test session. Theoretically, 'af_packet' should just pick the setting,
     * but this is not the case. It assumes that the mode is disabled unless
     * the user has invoked rte_eth_promiscuous_enable(), and this status of
     * the mode is enforced by rte_eth_dev_start(), which cannot be bypassed.
     *
     * Make sure to enable promiscuous mode using DPDK means before starting
     * the device. This will result in setting IFF_PROMISC on the underlying
     * interface, and the Linux driver will simply do nothing in response to
     * that since the mode is already enabled. Then rte_eth_dev_start() will
     * enforce enabling the mode, and, once again, this won't cause anything.
     */
    CHECK_RC(rpc_rte_eth_promiscuous_enable(tst_rpcs, port_id));

    CHECK_RC(rpc_rte_eth_dev_start(tst_rpcs, port_id));


    return port_id;
}

void
test_rte_af_packet_on_tst_if_release(rcf_rpc_server            *tst_rpcs,
                                     const struct if_nameindex *tst_if)
{
    te_string dn = TE_STRING_INIT;
    uint16_t  port_id;
    te_errno  rc;

    CHECK_RC(te_string_append(&dn, "net_af_packet%u", tst_if->if_index));

    rc = rpc_rte_eth_dev_get_port_by_name(tst_rpcs, dn.ptr, &port_id);
    if (rc == -TE_RC(TE_RPC, TE_ENODEV))
        return;

    CHECK_RC(rc);

    rpc_rte_eth_dev_stop(tst_rpcs, port_id);

    CHECK_RC(rpc_rte_eal_hotplug_remove(tst_rpcs, "vdev", dn.ptr));
    te_string_free(&dn);
}

static te_errno
test_mbuf_append_data(rcf_rpc_server *rpcs,
                      const uint8_t  *data,
                      size_t          size,
                      rpc_rte_mbuf_p  m)
{
    rpc_rte_mempool_p mp = rpc_rte_pktmbuf_get_pool(rpcs, m);
    rpc_rte_mbuf_p    m_seg = m;
    size_t            ofst = 0;
    int               ret;

    while (size > 0)
    {
        uint16_t m_seg_tailroom = rpc_rte_pktmbuf_tailroom(rpcs, m_seg);
        size_t   append_size;

        if (m_seg_tailroom == 0)
        {
            m_seg = rpc_rte_pktmbuf_alloc(rpcs, mp);

            ret = rpc_rte_pktmbuf_chain(rpcs, m, m_seg);
            if (ret != 0)
                return TE_EFAULT;

            m_seg_tailroom = rpc_rte_pktmbuf_tailroom(rpcs, m_seg);
        }

        append_size = MIN(size, m_seg_tailroom);

        ret = rpc_rte_pktmbuf_append_data(rpcs, m, data + ofst,
                                          append_size);
        if (ret != 0)
            return TE_EFAULT;

        ofst += append_size;
        size -= append_size;
    }

    return 0;
}

te_errno
test_clone_mbuf(rcf_rpc_server    *rpcs_src,
                rpc_rte_mbuf_p     m_src,
                rcf_rpc_server    *rpcs_dst,
                rpc_rte_mempool_p  mp_dst,
                rpc_rte_mbuf_p    *m_dstp)
{
    uint8_t                             *m_data = NULL;
    struct tarpc_rte_pktmbuf_tx_offload  m_tx_offload;
    uint16_t                             vlan_tci;
    uint64_t                             m_flags;
    uint32_t                             m_size;
    te_errno                             rc = 0;
    rpc_rte_mbuf_p                       m_dst;
    int                                  ret;

    if (rpcs_src == NULL || rpcs_dst == NULL || m_dstp == NULL)
        return TE_EINVAL;

    m_size = rpc_rte_pktmbuf_get_pkt_len(rpcs_src, m_src);

    m_data = TE_ALLOC(m_size);
    if (m_data == NULL)
        return TE_ENOMEM;

    ret = rpc_rte_pktmbuf_read_data(rpcs_src, m_src, 0, m_size, m_data, m_size);
    if ((size_t)ret != m_size)
    {
        rc = TE_EFAULT;
        goto out;
    }

    m_dst = rpc_rte_pktmbuf_alloc(rpcs_dst, mp_dst);

    ret = test_mbuf_append_data(rpcs_dst, m_data, m_size, m_dst);
    if (ret != 0)
    {
        rc = TE_EFAULT;
        goto out;
    }

    m_flags = rpc_rte_pktmbuf_get_flags(rpcs_src, m_src);
    ret = rpc_rte_pktmbuf_set_flags(rpcs_dst, m_dst, m_flags);
    if (ret != 0)
    {
        rc = TE_EFAULT;
        goto out;
    }

    rpc_rte_pktmbuf_get_tx_offload(rpcs_src, m_src, &m_tx_offload);
    rpc_rte_pktmbuf_set_tx_offload(rpcs_dst, m_dst, &m_tx_offload);

    vlan_tci = rpc_rte_pktmbuf_get_vlan_tci_outer(rpcs_src, m_src);
    rpc_rte_pktmbuf_set_vlan_tci_outer(rpcs_dst, m_dst, vlan_tci);

    vlan_tci = rpc_rte_pktmbuf_get_vlan_tci(rpcs_src, m_src);
    rpc_rte_pktmbuf_set_vlan_tci(rpcs_dst, m_dst, vlan_tci);

    *m_dstp = m_dst;

out:
    free(m_data);

    return rc;
}

te_errno
test_tx_mbuf_set_random_payload(rcf_rpc_server *rpcs,
                                size_t          size,
                                rpc_rte_mbuf_p  m)
{
    struct tarpc_rte_pktmbuf_tx_offload  tx_offload = {0};
    size_t                               non_header_size;
    uint32_t                             m_size;
    te_errno                             rc = 0;
    uint8_t                             *data;
    int                                  ret;
    unsigned int                         i;

    if (size == 0)
        return 0;

    if (rpcs == NULL || m == RPC_NULL)
        return TE_EINVAL;

    rpc_rte_pktmbuf_get_tx_offload(rpcs, m, &tx_offload);
    m_size = rpc_rte_pktmbuf_get_pkt_len(rpcs, m);

    non_header_size = m_size - tx_offload.outer_l2_len -
                      tx_offload.outer_l3_len - tx_offload.l2_len -
                      tx_offload.l3_len - tx_offload.l4_len;

    if (non_header_size > UINT16_MAX)
        return TE_ERANGE;

    if (non_header_size != 0)
    {
        ret = rpc_rte_pktmbuf_trim(rpcs, m, (uint16_t)non_header_size);
        if (ret != 0)
            return TE_EFAULT;
    }

    data = TE_ALLOC(size);
    if (data == NULL)
        return TE_ENOMEM;

    for (i = 0; i < size; ++i)
        data[i] = rand();

    rc = test_mbuf_append_data(rpcs, data, size, m);

    free(data);

    return rc;
}

static te_errno
test_get_pci_fn_prop_inst(rcf_rpc_server *rpcs,
                          const struct if_nameindex *port,
                          char *prop, char **oid)
{
    char *vendor = NULL;
    char *device = NULL;
    char *inst = tapi_malloc(CFG_OID_MAX);
    te_errno rc;

    rc = tapi_cfg_pci_get_pci_vendor_device(rpcs->ta, port->if_name,
                                            &vendor, &device);
    if (rc != 0)
    {
        ERROR("Failed to get vendor ID or device ID of %s on agent %s: %r",
              port->if_name, rpcs->ta, rc);
        goto out;
    }

    snprintf(inst, CFG_OID_MAX, "/local:%s/dpdk:/%s:pci_fn:%s:%s:",
             rpcs->ta, prop, vendor, device);
    *oid = inst;

out:
    free(vendor);
    free(device);

    return rc;

}

te_errno
test_get_pci_fn_prop(rcf_rpc_server *rpcs, const struct if_nameindex *port,
                char *prop, unsigned int *value)
{
    char *inst;
    te_errno rc;

    rc = test_get_pci_fn_prop_inst(rpcs, port, prop, &inst);
    if (rc != 0)
        goto out;

    rc = cfg_get_instance_str(NULL, value, inst);
    if (rc != 0)
    {
        ERROR("Failed to get %s instance from Configurator tree: %r", inst, rc);
        goto out;
    }

out:
    free(inst);

    return rc;

}

te_errno
test_add_pci_fn_prop(rcf_rpc_server *rpcs, const struct if_nameindex *port,
                char *prop, unsigned int value)
{
    char *inst;
    te_errno rc;

    rc = test_get_pci_fn_prop_inst(rpcs, port, prop, &inst);
    if (rc != 0)
        goto out;

    rc = cfg_add_instance_str(inst, NULL, CVT_INTEGER, value);
    if (rc != 0)
    {
        ERROR("Failed to add %s instance: %r", inst, rc);
        goto out;
    }

out:
    free(inst);

    return rc;

}

void
test_check_mtu(rcf_rpc_server *rpcs, const struct if_nameindex *port,
               unsigned int packet_size)
{
    unsigned int min_mtu;
    unsigned int max_mtu;
    unsigned int mtu;

    (void)tapi_dpdk_mtu_by_pkt_size(packet_size, &mtu);
    CHECK_RC(test_get_pci_fn_prop(rpcs, port, "min_mtu", &min_mtu));
    CHECK_RC(test_get_pci_fn_prop(rpcs, port, "max_mtu", &max_mtu));
    RING("Packet size %u requires MTU %u vs min/max %u/%u",
         packet_size, mtu, min_mtu, max_mtu);
    if ((mtu > ETHER_DATA_LEN && mtu < min_mtu) || mtu > max_mtu)
    {
        TEST_SKIP("Required MTU is out of reported min/max %u/%u",
                  min_mtu, max_mtu);
    }
}

void
test_set_mtu(rcf_rpc_server *rpcs, uint16_t port_id, uint16_t mtu,
             struct test_ethdev_config *ethdev_config)
{
    uint16_t min_mtu = ethdev_config->dev_info.min_mtu;
    uint16_t max_mtu = ethdev_config->dev_info.max_mtu;
    te_bool out_range_fail_expected;
    te_bool unconfigured_fail_expected;
    te_errno rc;

    out_range_fail_expected = mtu < min_mtu || mtu > max_mtu;
    unconfigured_fail_expected = ethdev_config->cur_state <
                                 TEST_ETHDEV_CONFIGURED;

    RPC_AWAIT_ERROR(rpcs);
    rc = rpc_rte_eth_dev_set_mtu(rpcs, port_id, mtu);
    if (rc == 0)
    {
        if (out_range_fail_expected)
            TEST_VERDICT("Set MTU out of reported min/max %u/%u unexpectedly"
                         " succeed", min_mtu, max_mtu);
        if (unconfigured_fail_expected)
            TEST_VERDICT("Set MTU in unconfigured state unexpectedly succeed");
    }
    else if (out_range_fail_expected && TE_RC_GET_ERROR(-rc) == TE_EINVAL)
    {
        TEST_SKIP("Cannot set MTU out of reported min/max %u/%u",
                  min_mtu, max_mtu);
    }
    else if (unconfigured_fail_expected && TE_RC_GET_ERROR(-rc) == TE_EINVAL)
    {
        RING("Cannot set MTU in unconfigured state as expected");
        TEST_SUCCESS;
    }
    else
    {
        TEST_VERDICT("rpc_rte_eth_dev_set_mtu() failed: %r", -rc);
    }
}

void
test_default_template_prepare(struct test_default_tmpl_prepare *p)
{
    if (p->mac_name != NULL)
        CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(p->rpcs, p->port_id,
                                                      p->params, p->mac_name));
    CHECK_RC(asn_write_int32(p->template, 1,
                             "arg-sets.0.#simple-for.begin"));
    CHECK_RC(asn_write_int32(p->template, p->packets_num,
                             "arg-sets.0.#simple-for.end"));
    CHECK_RC(tapi_ndn_subst_env(p->template, p->params, p->env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&(p->template), FALSE, NULL,
                                                  p->payload_len));
}

void
test_wait_stats_update()
{
    te_motivated_msleep(TEST_STATS_UPDATE_WAIT_MS,
                        "Wait for statistics update");
}

te_bool
test_desc_nb_violates_limits(unsigned int desc_nb,
                             const struct tarpc_rte_eth_desc_lim *desc_lim)
{
    te_bool res = FALSE;

    if (desc_lim->nb_max != 0 && desc_nb > desc_lim->nb_max)
    {
        WARN("Descriptors number is greater than reported maximum %u",
             desc_lim->nb_max);
        res = TRUE;
    }
    if (desc_nb < desc_lim->nb_min)
    {
        WARN("Descriptors number is less than reported minimum %u",
             desc_lim->nb_min);
        res = TRUE;
    }
    if (desc_lim->nb_align != 0 && (desc_nb % desc_lim->nb_align) != 0)
    {
        WARN("Descriptors number is not aligned to %u",
             desc_lim->nb_align);
        res = TRUE;
    }
    return res;
}

void
test_check_rss_queues(rcf_rpc_server *rpcs, unsigned int port_id,
                      uint16_t nb_rx_queues, uint16_t reta_size,
                      const struct tarpc_rte_eth_rss_reta_entry64 *reta_conf,
                      asn_value *ptrn,
                      uint32_t expected_hash, uint16_t expected_queue)
{
    rpc_rte_mbuf_p mbufs[BURST_SIZE] = {};
    uint16_t qid;
    int rc;

    for (qid = 0; qid < nb_rx_queues; ++qid)
    {
        uint16_t nb_rx;
        uint16_t i;

        if (qid == expected_queue)
            continue;

        nb_rx = rpc_rte_eth_rx_burst(rpcs, port_id, qid,
                                     mbufs, TE_ARRAY_LEN(mbufs));
        if (nb_rx != 0)
        {
            unsigned int ret;

            if (nb_rx != 1)
                ERROR("Too many packets received from unexpected queue");

            rc = tapi_rte_mbuf_match_pattern_seq(rpcs, ptrn,
                                                 mbufs, nb_rx, NULL, &ret);
            if (rc != 0)
            {
                ERROR("Failed to match packet received from unexpected queue");
            }
            else if (ret == 0)
            {
                ERROR("Unexpected packets from queue %u", qid);
            }
        }
        for (i = 0; i < nb_rx; ++i)
        {
            uint64_t    ol_flags;

            ol_flags = rpc_rte_pktmbuf_get_flags(rpcs, mbufs[i]);
            if ((ol_flags & (1UL << TARPC_RTE_MBUF_F_RX_RSS_HASH)) != 0)
            {
                uint32_t    rss_hash;
                int         reta_indx;
                int         reta_nb;
                uint16_t    rss_qid;

                rss_hash = rpc_rte_pktmbuf_get_rss_hash(rpcs, mbufs[i]);
                if (rss_hash == expected_hash)
                    RING("Packet %u RSS hash matches expected hash value", i);
                /*
                 * Zero RSS hash typically means that the hash is not
                 * actually calculated. Highlight it in test results.
                 */
                else if (rss_hash == 0)
                    RING_ARTIFACT("RSS hash value is 0");
                else
                    RING("Packet %u RSS hash does not match expected hash value", i);

                reta_nb = (rss_hash % reta_size) / RPC_RTE_RETA_GROUP_SIZE;
                reta_indx = (rss_hash % reta_size) % RPC_RTE_RETA_GROUP_SIZE;
                rss_qid = reta_conf[reta_nb].reta[reta_indx];
                if (rss_qid == qid)
                    RING("Rx queue matches expected in accordance with RSS hash value and RETA");
                else
                    ERROR("Rx queue %u does not match expected %u in accordance with RSS hash value and RETA",
                          qid, rss_qid);
            }
            rpc_rte_pktmbuf_free(rpcs, mbufs[i]);
            mbufs[i] = RPC_NULL;
        }
    }
}

void
test_check_mbuf_rss_hash_value(rcf_rpc_server *rpcs, rpc_rte_mbuf_p mbuf,
                               uint32_t expected_hash, uint32_t symmetric_hash)
{
    uint64_t ol_flags;

    ol_flags = rpc_rte_pktmbuf_get_flags(rpcs, mbuf);
    if ((ol_flags & (1UL << TARPC_RTE_MBUF_F_RX_RSS_HASH)) != 0)
    {
        uint32_t    rss_hash;

        rss_hash = rpc_rte_pktmbuf_get_rss_hash(rpcs, mbuf);
        if (rss_hash == expected_hash)
        {
            RING("Packet RSS hash matches expected hash value");
        }
        else if (rss_hash == symmetric_hash)
        {
            RING("Packet RSS hash matches symmetric hash value");
        }
        else
        {
            /*
             * Zero RSS hash typically means that the hash is not
             * actually calculated. Highlight it in test results.
             */
            if (rss_hash == 0)
                RING_ARTIFACT("RSS hash value is 0");
            TEST_VERDICT("Packet RSS hash does not match expected hash value");
        }
    }
}
