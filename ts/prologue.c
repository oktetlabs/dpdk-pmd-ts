/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief Test Suite prologue
 *
 * DPDK PMD Test Suite
 *
 * @author Andrew Rybchenko <Andrew.Rybchenko@oktetlabs.ru>
 */

#ifndef DOXYGEN_TEST_SPEC

/** Logging subsystem entity name */
#define TE_TEST_NAME    "prologue"

#include "dpdk_pmd_test.h"

#if HAVE_NET_ETHERNET_H
#include <net/ethernet.h>
#endif

#include "te_string.h"
#include "te_defs.h"

#include "tapi_cfg_base.h"
#include "tapi_cfg_if.h"
#include "tapi_cfg_cpu.h"
#include "tapi_cfg_net.h"
#include "tapi_cfg_sys.h"
#include "logger_ten.h"
#include "tapi_test.h"
#include "tapi_cfg_modules.h"
#include "tapi_rpc_rte_eal.h"
#include "tapi_sh_env.h"
#include "tapi_cfg.h"

#define VFIO_ENABLE_IOMMU_OID_FMT \
            "/agent:%s/module:vfio/parameter:enable_unsafe_noiommu_mode"

static te_errno
disalbe_offload(const char *iface, const char *offload)
{
    te_errno rc;

    rc = cfg_set_instance_fmt(CFG_VAL(INTEGER, 0),
                              "%s/feature:%s", iface, offload);
    if (TE_RC_GET_ERROR(rc) == TE_ENOENT)
        rc = 0;

    return rc;
}

static te_errno
disable_offload_list(const char *iface, ...)
{
    te_errno rc;
    va_list ap;
    const char *offload;

    va_start(ap, iface);
    while ((offload = va_arg(ap, const char *)) != NULL)
    {
        rc = disalbe_offload(iface, offload);
        if (rc != 0)
            break;
    }
    va_end(ap);

    return rc;
}

static tapi_cfg_net_node_cb disable_offloads;
static te_errno
disable_offloads(cfg_net_t *net, cfg_net_node_t *node,
                 const char *oid_str, cfg_oid *oid, void *cookie)
{
    UNUSED(net);
    UNUSED(node);
    UNUSED(cookie);

    if (strcmp(cfg_oid_inst_subid(oid, 1), "agent") != 0 ||
        strcmp(cfg_oid_inst_subid(oid, 2), "interface") != 0)
        return 0;

    return disable_offload_list(oid_str,
                                /* Want to see segments as they sent */
                                "rx-gro",
                                "rx-lro",
                                /* Want to send packets with bad checksums */
                                "tx-checksum-ip-generic",
                                "tx-checksum-ipv4",
                                "tx-checksum-ipv6",
                                NULL /* sentinel */);

}

/*
 * TODO: disable_ipv6_and_ipv4() shall also turn off ARP but
 * has to avoid this until bug 77365 is resolved
 */
static tapi_cfg_net_node_cb disable_ipv6_and_ipv4;
static te_errno
disable_ipv6_and_ipv4(cfg_net_t *net, cfg_net_node_t *node,
                      const char *oid_str, cfg_oid *oid, void *cookie)
{
    te_errno rc;
    const char *agent;
    const char *interface;
    char *iface_alloced = NULL;
    struct sockaddr_in dummy_ip4;

    UNUSED(net);
    UNUSED(cookie);

    switch (tapi_cfg_net_get_node_rsrc_type(node))
    {
        case NET_NODE_RSRC_TYPE_INTERFACE:
            agent = CFG_OID_GET_INST_NAME(oid, 1);
            interface = CFG_OID_GET_INST_NAME(oid, 2);
            break;

        case NET_NODE_RSRC_TYPE_PCI_FN:
        {
            char *pci_fn = NULL;

            agent = CFG_OID_GET_INST_NAME(oid, 1);
            rc = cfg_get_string(&pci_fn, "%s", oid_str);
            if (rc != 0)
            {
                ERROR("Cannot get PCI function OID by %s: %r", oid_str, rc);
                return rc;
            }
            rc = tapi_cfg_pci_get_net_if(pci_fn, &iface_alloced);
            free(pci_fn);
            if (rc != 0)
                return (TE_RC_GET_ERROR(rc) == TE_ENOENT) ? 0 : rc;
            if (strlen(iface_alloced) == 0)
            {
                /*
                 * Bound PCI device driver does not provide network interface.
                 * So, nothing to do here.
                 */
                free(iface_alloced);
                return 0;
            }
            interface = iface_alloced;
            break;
        }

        default:
            RING("ARB: unknown type");
            return 0;
    }

    rc = tapi_cfg_sys_set_int(agent, 1, NULL,
                              "net/ipv6/conf/%s/disable_ipv6", interface);
    if (TE_RC_GET_ERROR(rc) == TE_ENOENT)
    {
        WARN("Cannot disable IPv6 since node "
             "/agent:%s/sys:/net:/ipv6:/conf:%s/disable_ipv6 is missing",
             agent, interface);
        rc = 0;
    }

    if (rc == 0)
    {
        memset(&dummy_ip4, 0, sizeof(dummy_ip4));
        dummy_ip4.sin_family = AF_INET;
        dummy_ip4.sin_addr.s_addr = INADDR_ANY;

        rc = tapi_cfg_del_if_ip4_addresses(agent, interface, SA(&dummy_ip4));
    }

    free(iface_alloced);

    return rc;
}

static tapi_cfg_net_node_cb configure_interface;
static te_errno
configure_interface(cfg_net_t *net, cfg_net_node_t *node,
                    const char *oid_str, cfg_oid *oid, void *cookie)
{
    char interface_path[RCF_MAX_PATH];
    char *pci_path = NULL;
    char *interface = NULL;
    const char *agent;
    te_errno rc = 0;

    UNUSED(cookie);
    UNUSED(net);

    if (strcmp(cfg_oid_inst_subid(oid, 1), "agent") != 0 ||
        strcmp(cfg_oid_inst_subid(oid, 2), "hardware") != 0)
    {
        goto out;
    }

    agent = CFG_OID_GET_INST_NAME(oid, 1);

    rc = cfg_get_instance_str(NULL, &pci_path, oid_str);
    if (rc != 0)
    {
        ERROR("Failed to get TST PCI device path");
        goto out;
    }

    rc = tapi_cfg_pci_get_net_if(pci_path, &interface);
    if (rc != 0)
    {
        if (TE_RC_GET_ERROR(rc) != TE_ENOENT)
            ERROR("Failed to get network interface from the PCI device");
        else
            rc = 0;
        goto out;
    }

    if (strlen(interface) == 0)
    {
        if (node->type == NET_NODE_TYPE_AGENT)
        {
            ERROR("Interface name on TST must not be empty");
            rc = TE_EFAIL;
        }
        goto out;
    }

    rc = tapi_cfg_base_if_add_rsrc(agent, interface);
    if (rc != 0)
    {
        ERROR("Cannot grab network interface '%s' resource on TA '%s': %r",
              interface, agent, rc);
        goto out;
    }

    /*
     * Try to disable FW LLDP using private flag if it is
     * present. FW LLDP generates packets from Intel X710 which
     * randomly break tests because of unexpected packets observed.
     */
    rc = tapi_cfg_if_priv_flag_set(agent, interface,
                                   "disable-fw-lldp", TRUE);
    if (rc != 0 && rc != TE_RC(TE_CS, TE_ENOENT))
    {
        TEST_VERDICT("Attempt to disable FW LLDP using private "
                     "flag failed unexpectedly: %r", rc);
    }

    /* Update network configuration to use interface on TST only */
    if (node->type == NET_NODE_TYPE_AGENT)
    {
        snprintf(interface_path, sizeof(interface_path),
                 "/agent:%s/interface:%s", agent, interface);

        rc = cfg_set_instance(node->handle, CFG_VAL(STRING, interface_path));
        if (rc != 0)
            ERROR("Failed to assign TST network node to TST interface");
    }

out:
    free(pci_path);
    free(interface);

    return rc;
}

static rcf_ta_cb load_required_modules;
static te_errno
load_required_modules(const char *ta, void *cookie)
{
    /*
     * Driver that was built with DPDK and needs to be loaded
     * from TA working directory.
     */
    const char *dpdk_built_driver = "igb_uio";
    te_errno rc;
    char *driver;

    UNUSED(cookie);

    rc = tapi_cfg_pci_get_ta_driver(ta, NET_DRIVER_TYPE_DPDK, &driver);
    if (rc != 0)
        return rc;

    if (driver != NULL)
    {
        if (strcmp(driver, dpdk_built_driver) == 0)
            rc = tapi_cfg_module_add_from_ta_dir(ta, dpdk_built_driver, TRUE);
        else
            rc = tapi_cfg_module_add(ta, driver, TRUE);

        if (rc == 0)
            rc = tapi_cfg_module_change_finish(ta, driver);

        free(driver);

        if (rc != 0)
            return rc;
    }

    rc = tapi_cfg_pci_get_ta_driver(ta, NET_DRIVER_TYPE_NET, &driver);
    if (rc != 0)
        return rc;

    if (driver != NULL)
    {
        /* Both sfc and sfc_ef100 drivers are in sfc.ko module */
        if (strcmp_start("sfc", driver) == 0) {
            /* sfc drivers depend on the sfc_driverlink module */
            rc = tapi_cfg_module_add_from_ta_dir_or_fallback(ta, "sfc_driverlink", TRUE);
            if (rc == 0)
                rc = tapi_cfg_module_change_finish(ta, "sfc_driverlink");

            if (rc == 0)
                rc = tapi_cfg_module_add_from_ta_dir_or_fallback(ta, "sfc", TRUE);
            if (rc == 0)
                rc = tapi_cfg_module_change_finish(ta, "sfc");
        } else {
            rc = tapi_cfg_module_add_from_ta_dir_or_fallback(ta, driver, TRUE);
            if (rc == 0)
                rc = tapi_cfg_module_change_finish(ta, driver);
        }

        free(driver);

        if (rc != 0)
            return rc;
    }

    return 0;
}

static te_errno
is_prepare_vfio_module_required(const char *ta, te_bool *res)
{
    char *driver_name = NULL;
    te_errno rc;

    rc = tapi_cfg_pci_get_ta_driver(ta, NET_DRIVER_TYPE_DPDK, &driver_name);
    if (rc != 0)
        return rc;

    *res = driver_name != NULL && strcmp(driver_name, "vfio-pci") == 0;

    return 0;
}

static te_errno
prepare_vfio_module_iommu_disabled(const char *ta)
{
    char *enable_unsafe_noiommu_mode;
    cfg_val_type type;
    int shared_lock;
    te_errno rc_finish;
    te_errno rc;

    rc = tapi_cfg_module_add(ta, "vfio", true);
    if (rc != 0)
    {
        ERROR("Failed to load vfio module on agent %s: %r", ta, rc);
        goto out;
    }

    type = CVT_STRING;
    rc = cfg_get_instance_fmt(&type, &enable_unsafe_noiommu_mode,
                              VFIO_ENABLE_IOMMU_OID_FMT, ta);
    if (rc != 0)
    {
        if (TE_RC_GET_ERROR(rc) == TE_ENOENT)
        {
            WARN("The agent %s IOMMU is disabled and vfio module does not"
                 " support enable_unsafe_noiommu_mode parameter", ta);
            rc = 0;
            goto finish_module_change;
        }
        else
        {
            ERROR("Failed to read enable_unsafe_noiommu_mode parameter"
                  " value of vfio module on agent %s: %r", ta, rc);
            goto out;
        }
    }

    if (strcmp(enable_unsafe_noiommu_mode, "Y") == 0)
    {
        rc = 0;
        goto finish_module_change;
    }

    type = CVT_INTEGER;
    rc = cfg_get_instance_fmt(&type, &shared_lock,
                              "/agent:%s/rsrc:module:vfio/shared:", ta);
    if (rc != 0)
    {
        ERROR("Failed to get if vfio module loaded as shared"
              " on agent %s: %r", ta, rc);
        goto out;
    }

    if (!shared_lock)
    {
        rc = cfg_set_instance_fmt(CFG_VAL(STRING, "Y"),
                                  VFIO_ENABLE_IOMMU_OID_FMT, ta);
        if (rc != 0)
        {
            ERROR("Failed to set enable_unsafe_noiommu_mode parameter"
                  " of vfio module on agent %s: %r", ta, rc);
            goto out;
        }
        goto finish_module_change;
    }
    else
    {
        ERROR("Failed to set enable_unsafe_noiommu_mode parameter since"
              " vfio module is not locked exclusively");
        rc = TE_EPERM;
        goto out;
    }

finish_module_change:
    rc_finish = tapi_cfg_module_change_finish(ta, "vfio");
    if (rc_finish != 0)
        ERROR("Failed to finish change of vfio module on agent %s: %r",
              ta, rc_finish);
    rc = rc == 0 ? rc_finish : rc;
out:
    return rc;
}

static rcf_ta_cb prepare_vfio_module;
static te_errno
prepare_vfio_module(const char *ta, void *cookie)
{
    te_bool prepare_required;
    char *iommu_enabled;
    cfg_val_type type;
    te_errno rc;

    UNUSED(cookie);

    rc = is_prepare_vfio_module_required(ta, &prepare_required);
    if (rc != 0)
        return rc;

    if (!prepare_required)
        return 0;

    type = CVT_STRING;
    rc = cfg_get_instance_fmt(&type, &iommu_enabled,
                              "/agent:%s/hardware:/iommu:", ta);
    if (rc != 0)
    {
        ERROR("Failed to check IOMMU support on agent %s: %s",
              ta, te_rc_err2str(rc));
        return rc;
    }

    if (strcmp(iommu_enabled, "off") == 0)
        return prepare_vfio_module_iommu_disabled(ta);

    return 0;
}

static te_errno
detect_af_xdp(const char  *ta,
              te_bool     *detectedp)
{
    cfg_handle    *handles = NULL;
    unsigned int   count;
    te_errno       rc;

    *detectedp = FALSE;

    rc = cfg_find_pattern_fmt(&count, &handles,
                              "/local:%s/dpdk:/vdev:net_af_xdp*", ta);
    if (rc != 0)
    {
        ERROR("Cannot query af_xdp in vdev registry");
        goto exit;
    }

    if (count == 0)
        goto exit;

    *detectedp = TRUE;

exit:
    free(handles);

    return rc;
}

static te_errno
prepare_af_xdp(rcf_rpc_server  *rpcs)
{
    te_string      iface_oid = TE_STRING_INIT;
    cfg_val_type   cvt = CVT_STRING;
    char          *slave_oid = NULL;
    cfg_handle    *handles = NULL;
    char          *pci_oid = NULL;
    char          *iface = NULL;
    char          *vdev = NULL;
    unsigned int   count;
    te_errno       rc;

    rc = cfg_find_pattern_fmt(&count, &handles,
                              "/local:%s/dpdk:/vdev:net_af_xdp*", rpcs->ta);
    if (rc != 0)
    {
        ERROR("Cannot query af_xdp in vdev registry");
        goto exit;
    }

    if (count == 0)
        goto exit;

    if (count != 1)
    {
        ERROR("Cannot work with multiple af_xdp ports");
        rc = TE_ETOOMANY;
        goto exit;
    }

    rc = cfg_get_inst_name(handles[0], &vdev);
    if (rc != 0)
    {
        ERROR("Cannot get af_xdp port name");
        goto exit;
    }

    free(handles);

    rc = cfg_find_pattern_fmt(&count, &handles,
                              "/local:%s/dpdk:/vdev:%s/slave:*",
                              rpcs->ta, vdev);
    if (rc != 0 || count == 0)
    {
        ERROR("Cannot get handle of sub-device of af_xdp port");
	handles = NULL;
        rc = TE_ENODEV;
        goto exit;
    }

    if (count != 1)
    {
        ERROR("Cannot use more than 1 sub-device with af_xdp port");
        rc = TE_EINVAL;
        goto exit;
    }

    rc = cfg_get_instance(handles[count - 1], &cvt, &slave_oid);
    if (rc != 0)
    {
        ERROR("Cannot get OID of sub-device of af_xdp port");
        goto exit;
    }

    rc = cfg_get_instance_string_fmt(&pci_oid, "%s", slave_oid);
    if (rc != 0)
    {
        ERROR("Cannot get OID of PCI FN of sub-device of af_xdp port");
        goto exit;
    }

    rc = cfg_get_instance_string_fmt(&iface, "%s/net:", pci_oid);
    if (rc != 0)
    {
        ERROR("Cannot get name of linux net interface of IUT");
        goto exit;
    }

    rc = te_string_append(&iface_oid, "/agent:%s/interface:%s",
                          rpcs->ta, iface);
    if (rc != 0)
    {
        ERROR("Cannot make OID of linux net interface of IUT");
        goto exit;
    }

    rc = cfg_add_instance_fmt(NULL, CVT_STRING, iface_oid.ptr,
                              "/agent:%s/rsrc:%s", rpcs->ta, iface);
    if (rc != 0)
    {
        ERROR("Cannot reserve linux net interface of IUT");
        goto exit;
    }

    /*
     * The PMD does not support RSS controls. Set the number
     * of channels on the backing NIC to 1 to disable RSS.
     */
    rc = cfg_set_instance_fmt(CFG_VAL(INTEGER, 1),
                              "/agent:%s/interface:%s"
                              "/channels:/combined:/current:",
                              rpcs->ta, iface);
    if (rc != 0)
    {
        ERROR("Cannot quit multi-queue on linux net interface of IUT: %r", rc);
        goto exit;
    }

    rc = disable_offload_list(iface_oid.ptr,
                              /* Want to receive all VLAN packets */
                              "rx-vlan-filter",
                              /* Want to retain TPID and TCI in packet data */
                              "rx-vlan-hw-parse",
                              NULL /* sentinel */);
    if (rc != 0)
    {
        ERROR("Cannot disable offloads on linux net interface of IUT: %r", rc);
        goto exit;
    }

    (void)tapi_cfg_sys_set_int(rpcs->ta, 1, NULL,
                               "net/ipv6/conf/%s/disable_ipv6", iface);

    rc = tapi_cfg_base_if_up(rpcs->ta, iface);
    if (rc != 0)
        ERROR("Cannot start linux net interface of IUT");

exit:
    te_string_free(&iface_oid);
    free(slave_oid);
    free(handles);
    free(pci_oid);
    free(iface);
    free(vdev);

    return rc;
}

/**
 * Reserve network interfaces and assign test IP addresses for test
 * networks.
 *
 * @retval EXIT_SUCCESS     success
 * @retval EXIT_FAILURE     failure
 */
int
main(int argc, char **argv)
{
/* Redefine as empty to avoid environment processing here */
#undef TEST_START_VARS
#define TEST_START_VARS
#undef TEST_START_SPECIFIC
#define TEST_START_SPECIFIC
#undef TEST_END_SPECIFIC
#define TEST_END_SPECIFIC

    te_bool           xdp_detected = FALSE;
    unsigned int      service_core_count;
    rcf_rpc_server   *iut_rpcs = NULL;
    rcf_rpc_server   *tst_rpcs = NULL;
    int               peer_max_mtu;
    tapi_cpu_index_t  cpu_index;
    unsigned int      i;

    TEST_START_ENV_VARS;
    TEST_START;

    CHECK_RC(tapi_expand_path_all_ta(NULL));

    if ((rc = tapi_cfg_net_remove_empty()) != 0)
        TEST_VERDICT("Failed to remove /net instances with empty interfaces");

    rc = tapi_cfg_net_reserve_all();
    if (rc != 0)
        TEST_VERDICT("Failed to reserve all interfaces mentioned in networks "
                     "configuration: %r", rc);

    CHECK_RC(rcf_foreach_ta(load_required_modules, NULL));
    CHECK_RC(rcf_foreach_ta(prepare_vfio_module, NULL));

    rc = tapi_cfg_net_bind_driver_by_node(NET_NODE_TYPE_AGENT,
                                          NET_DRIVER_TYPE_NET);
    if (rc != 0)
        TEST_VERDICT("Failed to bind net driver on agent net node");

    /*
     * There's no RPC server to query the agent name from at this
     * stage, so pass a literal constant, which ought to be valid.
     */
    rc = detect_af_xdp("DPDK", &xdp_detected);
    if (rc != 0)
        TEST_VERDICT("Failed to detect AF_XDP configuration on agent DPDK");

    rc = tapi_cfg_net_bind_driver_by_node(NET_NODE_TYPE_NUT,
                                          xdp_detected ? NET_DRIVER_TYPE_NET :
                                                         NET_DRIVER_TYPE_DPDK);
    if (rc != 0)
        TEST_VERDICT("Failed to bind driver on nut net node");

    /*
     * If a net driver was binded, synchronize configuration tree to discover
     * network interfaces that are associated with that driver.
     */
    CHECK_RC(rc = cfg_synchronize("/:", TRUE));

    rc = tapi_cfg_net_foreach_node(configure_interface, NULL);
    if (rc != 0)
    {
        TEST_VERDICT("Failed to configure interfaces mentioned in networks "
                  "configuration: %r", rc);
    }

    rc = tapi_cfg_net_foreach_node(disable_offloads, NULL);
    if (rc != 0)
        TEST_VERDICT("Failed to disable offloads on interfaces mentioned in "
                     "networks configuration: %r", rc);

    rc = tapi_cfg_net_all_up(FALSE);
    if (rc != 0)
        TEST_VERDICT("Failed to up all interfaces mentioned in networks "
                     "configuration: %r", rc);

    rc = tapi_cfg_net_delete_all_ip4_addresses();
    if (rc != 0)
        TEST_VERDICT("Failed to delete all IPv4 addresses from all "
                     "interfaces mentioned in networks configuration: %r",
                     rc);

    rc = tapi_cfg_net_all_assign_ip(AF_INET);
    if (rc != 0)
        TEST_VERDICT("Failed to assign IPv4 subnets: %r", rc);

    rc = tapi_cfg_net_all_assign_ip(AF_INET6);
    if (rc != 0)
        TEST_VERDICT("Failed to assign IPv6 subnets: %r", rc);

    /*
     * Disable IPv6 and delete IPv4 addresses to prevent sending
     * unexpected packets
     */
    rc = tapi_cfg_net_foreach_node(disable_ipv6_and_ipv4, NULL);
    if (rc != 0)
        TEST_VERDICT("Failed to disable IPv6 and delete IPv4 addresses on "
                     "interfaces mentioned in networks configuration: %r", rc);

    TEST_START_ENV;

    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);

    rc = prepare_af_xdp(iut_rpcs);
    if (rc != 0)
        TEST_VERDICT("Failed to prepare AF_XDP");

    rc = tapi_eal_get_nb_required_service_cores_rpcs(&env, iut_rpcs,
                                                     &service_core_count);
    if (rc != 0)
        TEST_VERDICT("Failed to get the number of required service cores on IUT");

    for (i = 0; i < service_core_count + 1; ++i)
    {
        rc = tapi_cfg_cpu_grab_by_prop(iut_rpcs->ta, NULL, &cpu_index);
        if (rc != 0)
            TEST_VERDICT("Failed to grab a CPU on IUT");
    }

    rc = tapi_eal_get_nb_required_service_cores_rpcs(&env, tst_rpcs,
                                                     &service_core_count);
    if (rc != 0)
        TEST_VERDICT("Failed to get the number of required service cores on TST");

    for (i = 0; i < service_core_count + 1; ++i)
    {
        rc = tapi_cfg_cpu_grab_by_prop(tst_rpcs->ta, NULL, &cpu_index);
        if (rc != 0)
            TEST_VERDICT("Failed to grab a CPU on TST");
    }

    /*
     * Set maximum MTU on TST interface beforehand so
     * that tests skip setting MTU and save much time.
     */
    rc = cfg_get_instance_str(NULL, &peer_max_mtu,
                              "/local:/dpdk:/peer_max_mtu:");
    if (rc == 0)
    {
        tapi_env_host             *tst_host = NULL;
        const struct if_nameindex *tst_if = NULL;

        TEST_GET_HOST(tst_host);
        TEST_GET_IF(tst_if);
        CHECK_RC(tapi_cfg_base_if_set_mtu_leastwise(tst_host->ta,
                                                    tst_if->if_name,
                                                    peer_max_mtu));

        /*
         * Enable promiscuous mode on Tester side to workaround sfc
         * driver behaviour which set Rx mode asynchronously and we
         * can send packets before the promisuous mode is actually
         * enabled.
         */
        CHECK_RC(tapi_cfg_base_if_set_promisc(tst_host->ta, tst_if->if_name,
                                              TRUE));
    }

    CFG_WAIT_CHANGES;

    CHECK_RC(rc = cfg_synchronize("/:", TRUE));
    CHECK_RC(rc = cfg_tree_print(NULL, TE_LL_RING, "/:"));

    TEST_SUCCESS;

cleanup:

    TEST_END;
}

#endif /* !DOXYGEN_TEST_SPEC */
