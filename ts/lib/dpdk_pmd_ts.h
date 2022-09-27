/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief DPDK PMD Test Suite
 *
 * Declarations of common functions.
 *
 * @author Denis Pryazhennikov <Denis.Pryazhennikov@oktetlabs.ru>
 */

#ifndef __TS_DPDK_PMD_TS_H__
#define __TS_DPDK_PMD_TS_H__

#include "tapi_env.h"
#include "tapi_tad.h"
#include "tapi_rpc_rte_mbuf.h"
#include "tapi_test.h"
#include "te_toeplitz.h"
#include "rpc_dpdk_defs.h"

/**
 * Default number of elements in RTE mempool to be used by tests
 */
#define TEST_RTE_MEMPOOL_DEF_SIZE       (4 << 10)

/**
 * Extra elements in default RTE mempool for Tx mbufs.
 *
 * Tests which allocate more mbufs for Tx should care about mempool
 * size itself.
 */
#define TEST_RTE_MEMPOOL_DEF_EXTRA      32

/**
 * Default cache size of RTE mempool to be used by tests
 * (TEST_RTE_MEMPOOL_DEF_SIZE % TEST_RTE_MEMPOOL_DEF_CACHE = 0)
 */
#define TEST_RTE_MEMPOOL_DEF_CACHE      256

/**
 * Default size of RTE mbuf private area
 */
#define TEST_RTE_MEMPOOL_DEF_PRIV_SIZE  0

/**
 * Default size of RTE mbuf data buffer, including RTE_PKTMBUF_HEADROOM
 */
#define TEST_RTE_MEMPOOL_DEF_DATA_ROOM  (2 << 10)

/**
 * Overhead of RTE mbuf data buffer sufficient to cover unused part of
 * the buffer
 */
#define TEST_RTE_MEMPOOL_DATA_ROOM_OVERHEAD 256

/**
 * Default size of RTE mbuf data buffer for receiving Jumbo frames
 */
#define TEST_RTE_MEMPOOL_DEF_JUMBO_DATA_ROOM  (1 << 14)

/**
 * Default number of Rx descriptors to be taken into account when
 * mempool is sized if driver does not provide the minimum.
 *
 * It matches RTE_ETH_DEV_FALLBACK_RX_RINGSIZE.
 */
#define TEST_RTE_ETHDEV_DEF_NB_RX_DESCS 512

/**
 * Maximum VLAN ID value
 */
#define TEST_MAX_VLAN_ID                4095

/**
 * Minimum page size in bytes on most architectures
 */
#define TEST_MIN_PAGE_SIZE 4096

/**
 * Ethernet encapsulated protocol opaque ID
 */
#define TEST_ETHER_ENCAP_PROTO_OPAQUE   0x1111

/**
 * IP encapsulated protocol opaque ID
 */
#define TEST_IP_ENCAP_PROTO_OPAQUE      0x8F
/**
 * Name of mempool for packets
 */
#define TEST_PKTS_MEMPOOL_NAME "pkts"

/**
 * Default size of rx/tx burst
 */
#define BURST_SIZE 32

/**
 * Vector RX limitation
 */
#define RX_VEC_LIM 4

/**
 * Name of iut_port MAC
 */
 #define TEST_IUT_PORT_MAC_NAME "iut_mac"

/**
 * IPv4 address length
 */
#define TEST_IP4_ADDR_LEN 4

/**
 * IPv6 address length
 */
#define TEST_IP6_ADDR_LEN 16

/** RSS hash protocols */
#define TEST_ETH_RSS_IPV4               (1ULL << TARPC_RTE_ETH_FLOW_IPV4)
#define TEST_ETH_RSS_FRAG_IPV4          (1ULL << TARPC_RTE_ETH_FLOW_FRAG_IPV4)
#define TEST_ETH_RSS_NONFRAG_IPV4_TCP   (1ULL << TARPC_RTE_ETH_FLOW_NONFRAG_IPV4_TCP)
#define TEST_ETH_RSS_NONFRAG_IPV4_UDP   (1ULL << TARPC_RTE_ETH_FLOW_NONFRAG_IPV4_UDP)
#define TEST_ETH_RSS_NONFRAG_IPV4_SCTP  (1ULL << TARPC_RTE_ETH_FLOW_NONFRAG_IPV4_SCTP)
#define TEST_ETH_RSS_NONFRAG_IPV4_OTHER (1ULL << TARPC_RTE_ETH_FLOW_NONFRAG_IPV4_OTHER)
#define TEST_ETH_RSS_IPV6               (1ULL << TARPC_RTE_ETH_FLOW_IPV6)
#define TEST_ETH_RSS_FRAG_IPV6          (1ULL << TARPC_RTE_ETH_FLOW_FRAG_IPV6)
#define TEST_ETH_RSS_NONFRAG_IPV6_TCP   (1ULL << TARPC_RTE_ETH_FLOW_NONFRAG_IPV6_TCP)
#define TEST_ETH_RSS_NONFRAG_IPV6_UDP   (1ULL << TARPC_RTE_ETH_FLOW_NONFRAG_IPV6_UDP)
#define TEST_ETH_RSS_NONFRAG_IPV6_SCTP  (1ULL << TARPC_RTE_ETH_FLOW_NONFRAG_IPV6_SCTP)
#define TEST_ETH_RSS_NONFRAG_IPV6_OTHER (1ULL << TARPC_RTE_ETH_FLOW_NONFRAG_IPV6_OTHER)
#define TEST_ETH_RSS_L2_PAYLOAD         (1ULL << TARPC_RTE_ETH_FLOW_L2_PAYLOAD)
#define TEST_ETH_RSS_IPV6_EX            (1ULL << TARPC_RTE_ETH_FLOW_IPV6_EX)
#define TEST_ETH_RSS_IPV6_TCP_EX        (1ULL << TARPC_RTE_ETH_FLOW_IPV6_TCP_EX)
#define TEST_ETH_RSS_IPV6_UDP_EX        (1ULL << TARPC_RTE_ETH_FLOW_IPV6_UDP_EX)
#define TEST_ETH_RSS_PORT               (1ULL << TARPC_RTE_ETH_FLOW_PORT)
#define TEST_ETH_RSS_VXLAN              (1ULL << TARPC_RTE_ETH_FLOW_VXLAN)
#define TEST_ETH_RSS_GENEVE             (1ULL << TARPC_RTE_ETH_FLOW_GENEVE)
#define TEST_ETH_RSS_NVGRE              (1ULL << TARPC_RTE_ETH_FLOW_NVGRE)

#define TEST_ETH_RSS_IP ( \
        TEST_ETH_RSS_IPV4 | \
        TEST_ETH_RSS_FRAG_IPV4 | \
        TEST_ETH_RSS_NONFRAG_IPV4_OTHER | \
        TEST_ETH_RSS_IPV6 | \
        TEST_ETH_RSS_FRAG_IPV6 | \
        TEST_ETH_RSS_NONFRAG_IPV6_OTHER | \
        TEST_ETH_RSS_IPV6_EX)

#define TEST_ETH_RSS_UDP ( \
        TEST_ETH_RSS_NONFRAG_IPV4_UDP | \
        TEST_ETH_RSS_NONFRAG_IPV6_UDP | \
        TEST_ETH_RSS_IPV6_UDP_EX)

#define TEST_ETH_RSS_TCP ( \
        TEST_ETH_RSS_NONFRAG_IPV4_TCP | \
        TEST_ETH_RSS_NONFRAG_IPV6_TCP | \
        TEST_ETH_RSS_IPV6_TCP_EX)

#define TEST_ETH_RSS_SCTP ( \
        TEST_ETH_RSS_NONFRAG_IPV4_SCTP | \
        TEST_ETH_RSS_NONFRAG_IPV6_SCTP)

#define TEST_ETH_RSS_TUNNEL ( \
        TEST_ETH_RSS_VXLAN  | \
        TEST_ETH_RSS_GENEVE | \
        TEST_ETH_RSS_NVGRE)

#define TEST_ETH_RSS_PROTO_MASK ( \
        TEST_ETH_RSS_IPV4 | \
        TEST_ETH_RSS_FRAG_IPV4 | \
        TEST_ETH_RSS_NONFRAG_IPV4_TCP | \
        TEST_ETH_RSS_NONFRAG_IPV4_UDP | \
        TEST_ETH_RSS_NONFRAG_IPV4_SCTP | \
        TEST_ETH_RSS_NONFRAG_IPV4_OTHER | \
        TEST_ETH_RSS_IPV6 | \
        TEST_ETH_RSS_FRAG_IPV6 | \
        TEST_ETH_RSS_NONFRAG_IPV6_TCP | \
        TEST_ETH_RSS_NONFRAG_IPV6_UDP | \
        TEST_ETH_RSS_NONFRAG_IPV6_SCTP | \
        TEST_ETH_RSS_NONFRAG_IPV6_OTHER | \
        TEST_ETH_RSS_L2_PAYLOAD | \
        TEST_ETH_RSS_IPV6_EX | \
        TEST_ETH_RSS_IPV6_TCP_EX | \
        TEST_ETH_RSS_IPV6_UDP_EX | \
        TEST_ETH_RSS_PORT | \
        TEST_ETH_RSS_VXLAN | \
        TEST_ETH_RSS_GENEVE | \
        TEST_ETH_RSS_NVGRE)

/**
 * Infinitive timeout for traffic receive operation by default
 */
#define RECEIVE_TIMEOUT_DEF -1

/**
 * The number of attempts to check link status
 */
#define TEST_LINK_UP_MAX_CHECKS 500

/**
 * The number of milliseconds to wait for link status change
 * (link status check is done on port start and after MTU changes)
 */
#define TEST_LINK_UP_WAIT_MS 20

/**
 * A timeout which is required to elapse in certain cases
 * after link is established before the HW becomes ready
 */
#define TEST_POST_LINK_UP_TIMEOUT 300

/**
 * The number of attempts to configure tunnel UDP port
 */
#define TEST_TUNNEL_UDP_PORT_MAX_CHECKS 250

/**
 * The number of milliseconds to wait on each
 * attempt of tunnel UDP port configuration
 */
#define TEST_TUNNEL_UDP_PORT_WAIT_MS 20

/**
 * The number of milliseconds to wait for MC REBOOT after adding/deleting
 * the tunnel UDP port
 */
#define TEST_TUNNEL_UDP_PORT_AWAIT_MC_REBOOT_MS 1000

/**
 * Maximum number of different names of one extended statistic
 */
#define TEST_MAX_NB_XSTAT_NAMES 2

/**
 * The number of milliseconds to wait for statistics update
 */
#define TEST_STATS_UPDATE_WAIT_MS 1100

/**
 * Maximum timeout on packet reception
 */
#define TEST_RX_PKTS_WAIT_MAX_MS 1000

/**
 * Guard timeout to make sure that no unexpected packets arrive
 */
#define TEST_RX_UNEXP_PKTS_GUARD_TIMEOUT_MS 10

/**
 * Default Ether-Type in Ethernet header
 */
#define TEST_DEF_ETHER_TYPE 8111

/**
 * Default IP Protocol number in IPv4/IPv6 header
 */
#define TEST_DEF_IP_PROTO 143

/**
 * Default UDP destination port for GENEVE protocol
 */
#define TEST_DEF_GENEVE_UDP_PORT 6081

/**
 * Default UDP destination port for VXLAN protocol
 */
#define TEST_DEF_VXLAN_UDP_PORT 4789

/**
 * Default protocol type for GENEVE/GRE protocols
 */
#define TEST_DEF_PROTOCOL_TYPE 0x6558

/**
 * Default version for PPPOE protocol
 */
#define TEST_DEF_PPPOE_VER 1

/**
 * Default type for PPPOE protocol
 */
#define TEST_DEF_PPPOE_TYPE 1

/**
 * Default code for PPPOE protocol (PADI packet)
 */
#define TEST_DEF_PPPOE_PADI_CODE 9

/**
 * Maximum length of test-pmd arguments
 */
#define TESTPMD_ARG_MAX_LEN 30

/**
 * Maximum header size
 */
#define TEST_DEV_HEADER_SIZE 256


/** Template fields flags */
enum test_tmpl_fields {
    TEST_TMPL_SRC_MAC           = 0x00000001,   /* Source MAC address */
    TEST_TMPL_DST_MAC           = 0x00000002,   /* Destination MAC address */
    TEST_TMPL_ETHER_TYPE        = 0x00000004,   /* Ether-type */
    TEST_TMPL_OUTER_VID         = 0x00000008,   /* Outer VLAN ID */
    TEST_TMPL_INNER_VID         = 0x00000010,   /* Inner VLAN ID */
    TEST_TMPL_SRC_HOST          = 0x00000020,   /* Source IP address */
    TEST_TMPL_DST_HOST          = 0x00000040,   /* Destination IP address */
    TEST_TMPL_IP_PROTO          = 0x00000080,   /* IP transport protocol */
    TEST_TMPL_SRC_PORT          = 0x00000100,   /* Source TCP/UDP port */
    TEST_TMPL_DST_PORT          = 0x00000200,   /* Destination TCP/UDP port */
    TEST_TMPL_VNI_OR_VSID       = 0x00000400,   /* VNI or VSID */
    /* Inner frame source MAC address */
    TEST_TMPL_IFRM_SRC_MAC      = 0x00000800,
    /* Inner frame destination MAC address */
    TEST_TMPL_IFRM_DST_MAC      = 0x00001000,
    /* Inner frame ether-type */
    TEST_TMPL_IFRM_ETHER_TYPE   = 0x00002000,
    /* Inner frame outer VLAN ID */
    TEST_TMPL_IFRM_OUTER_VID    = 0x00004000,
    /* Inner frame inner VLAN ID */
    TEST_TMPL_IFRM_INNER_VID    = 0x00008000,
    /* Inner frame source IP address */
    TEST_TMPL_IFRM_SRC_HOST     = 0x00010000,
    /* Inner frame destination IP address */
    TEST_TMPL_IFRM_DST_HOST     = 0x00020000,
    /* Inner frame IP transport protocol */
    TEST_TMPL_IFRM_IP_PROTO     = 0x00040000,
    /* Inner frame source TCP/UDP port */
    TEST_TMPL_IFRM_SRC_PORT     = 0x00080000,
    /* Inner frame destination TCP/UDP port */
    TEST_TMPL_IFRM_DST_PORT     = 0x00100000,
    /* Inner frame unknown multicast destination MAC address */
    TEST_TMPL_IFRM_UN_MCAST_DST = 0x01000000,
    /* Inner frame unknown unicast destination MAC address */
    TEST_TMPL_IFRM_UN_UCAST_DST = 0x02000000,
    /* Unknown multicast destination MAC address */
    TEST_TMPL_UN_MCAST_DST      = 0x40000000,
    /* Unknown unicast destination MAC address */
    TEST_TMPL_UN_UCAST_DST      = 0x80000000,
};

/**
 * Information about the configuration of the Ethernet device
 */
struct test_ethdev_config {
    /* Common config */
    tapi_env                     *env;         /**< Environment binding */
    rcf_rpc_server               *rpcs;        /**< RPC server handle */
    test_ethdev_state             cur_state;   /**< The current device state */
    te_bool                       closed;      /**< Device close call
                                                    indicator */
    int                           argc;        /**< Number of additional EAL
                                                    arguments */
    const char                  **argv;        /**< Additional EAL arguments */
    struct tarpc_rte_eth_dev_info dev_info;    /**< Ethernet device information */
    struct tarpc_rte_eth_conf    *eth_conf;    /**< A structure used to configure
                                                    an Ethernet port */
    const char                   *bus_name;    /**< EAL generic bus name */
    char                          dev_name[RPC_RTE_ETH_NAME_MAX_LEN];
                                               /**< Device name */
    uint16_t                      port_id;     /**< The port identifier of
                                                    the device */
    int                           socket_id;   /**< The socket identifier where
                                                    the memory should be
                                                    allocated */
    uint16_t                      nb_rx_queue; /**< The number of receive queues */
    uint16_t                      nb_tx_queue; /**< The number of transmit queues */
    uint16_t                      min_rx_desc;  /**< The minimum number of
                                                     receive descriptors
                                                     per queue */
    uint16_t                      min_tx_desc;  /**< The minimum number of
                                                     transmit descriptors
                                                     per queue */
    uint16_t                      required_mtu; /**< The required MTU to which
                                                     default configuration
                                                     items will be adjusted */
    te_bool                       is_rx_setup; /**< The RX queues setup indicator */
    struct tarpc_rte_eth_rxconf **rx_confs;    /**< Array of structures used to
                                                    configure RX qeueus */
    struct tarpc_rte_eth_txconf **tx_confs;    /**< Array of structures used to
                                                    configure TX qeueus */
    rpc_rte_mempool_p             mp;          /**< RTE mempool pointer */
};

/** Test parameter to specify mbuf segmentation rules */
struct test_param_seg_ptrn {
    struct tarpc_pktmbuf_seg_group *seg_groups;     /**< Segmentation groups */
    unsigned int                    nb_seg_groups;  /**< Number of groups */
};

/** Addresses of the packet */
struct test_pkt_addresses {
    const uint8_t *src_mac;             /**< Source MAC address */
    const uint8_t *dst_mac;             /**< Destination MAC address */
    const struct sockaddr *src_ip;      /**< Source IP address */
    const struct sockaddr *dst_ip;      /**< Destination IP address */
};

/** Type of a transceiver */
enum test_transceiver_type {
    TEST_TRANSCEIVER_NET, /**< Transceiver that uses kernel net driver */
    TEST_TRANSCEIVER_DPDK, /**< Transceiver that uses DPDK driver */
};

/** DPDK transceiver */
struct test_transceiver_dpdk {
    rcf_rpc_server *rpcs; /**< RPC server handle */
    uint16_t port_id; /**< Port identifier */
    rpc_rte_mempool_p mp; /**< Memory pool handle */
};

/** Kernel interface */
struct test_transceiver_net {
    char *ta; /**< Test agent name */
    char *if_name; /**< Interface name */
    csap_handle_t rx_csap; /** CSAP-handle used to receive and match packets */
};

/** Transceiver entity used to commit send-receive exchanges */
struct test_transceiver {
    enum test_transceiver_type type; /**< Transceiver type */
    union {
        struct test_transceiver_dpdk dpdk; /**< DPDK transceiver */
        struct test_transceiver_net net; /**< Kernel net transceiver */
    } trsc;
};

/** Input for template prepare procedure test_default_template_prepare() */
struct test_default_tmpl_prepare {
    rcf_rpc_server *rpcs; /**< RPC server handle */
    uint16_t port_id; /**< Port identifier */
    te_kvpair_h *params; /**< List of kvpairs to store parameters */
    const char *mac_name; /**< MAC name in template */
    unsigned int packets_num; /**< Packets number */
    struct tapi_env *env; /**< Environment binding */
    size_t payload_len; /**< Payload length */
    asn_value *template; /**< Template to prepare */
};

/**
 * Prototype of a function used to transform template for transmittion into
 * template for receiving.
 *
 * @note Returned ASN.1 value for template must not be dependend on
 *       @p tmpl_rx
 *
 * @param[in] tmpl_rx   Template for transmittion
 * @param[in] tmpl_data User-provided data
 *
 * @return Template for receiving or @c NULL on error
 */
typedef asn_value * test_transceiver_transform_tmpl(const asn_value *tmpl_tx,
                                                    void *tmpl_data);

/**
 * Prototype of a function used to transform pattern that was sniffed on
 * transmittion into pattern for receiving and matching.
 *
 * @note Returned ASN.1 value for pattern must not be dependend on
 *       @p ptrn_rx
 *
 * @param[in] ptrn_rx   Pattern sniffed on transmittion
 * @param[in] ptrn_data User-provided data
 *
 * @return Pattern for receiving or @c NULL on error
 */
typedef asn_value * test_transceiver_transform_ptrn(const asn_value *ptrn_tx,
                                                    void *ptrn_data);

/** Exchange of packets between transceivers */
struct test_transceiver_exchange {
    asn_value *tmpl_tx; /**< Template for transmittion */
    test_transceiver_transform_tmpl *transform_tmpl; /**< Template
                                                          transformation */
    void *tmpl_data; /**< User-provided data for transform_tmpl */
    test_transceiver_transform_ptrn *transform_ptrn; /**< Pattern
                                                          transformation */
    void *ptrn_data; /**< User-provided data for transform_ptrn */
    char *verdict_no_pkts; /**< Custom verdict to use when no packets are
                                received. @c NULL means default verdict */
    char *verdict_known_unexp; /**< Custom verdict to use when unexpected
                                    matching packets are received. @c NULL
                                    means default verdict */
};

/**
 * Create kernel net driver transceiver.
 *
 * @note Jumps out on failure
 *
 * @param[in]  ta        Test agent name
 * @param[in]  if_name   Interface name
 *
 * @return Transceiver entity
 */
struct test_transceiver * test_transceiver_net_init(const char *ta,
                                                    const char *if_name);

/**
 * Create DPDK transceiver.
 *
 * @note Jumps out on failure
 *
 * @param[in]  rpcs      RPC server handle, only a pointer is stored in
 *                       transceiver
 * @param[in]  port_id   Port identifier
 * @param[in]  mp        Mem pool handle
 *
 * @return Transceiver entity
 */
struct test_transceiver * test_transceiver_dpdk_init(rcf_rpc_server *rpcs,
                                                     uint16_t port_id,
                                                     rpc_rte_mempool_p mp);

/**
 * Destroy transceiver entity.
 *
 * @param  trsc      Transceiver entity
 */
void test_transceiver_free(struct test_transceiver *trsc);

/**
 * Create exchange between transceivers.
 *
 * @note Jumps out on failure
 *
 * @param[in]  tmpl_tx          Template for transmittion
 * @param[in]  transform_tmpl   Template transformation, may be @c NULL
 *                              not to change the template
 * @param[in]  tmpl_data        User-provided data for @p transform_tmpl
 * @param[in]  transform_ptrn   Pattern transformation, may be @c NULL
 *                              not to change the pattern
 * @param[in]  ptrn_data        User-provided data for @p transform_ptrn
 *
 * @return Exchange entity
 */
struct test_transceiver_exchange * test_transceiver_exchange_init(
                               const asn_value *tmpl_tx,
                               test_transceiver_transform_tmpl transform_tmpl,
                               void *tmpl_data,
                               test_transceiver_transform_ptrn transform_ptrn,
                               void *ptrn_data);

/**
 * Set verdicts for an exchange object.
 *
 * @note @c NULL means use default verdict.
 *
 * @param[in] verdict_no_pkts       Verdict to use when no packets are received
 *                                  but at least one was expected
 * @param[in] verdict_known_unexp   Verdict to use when more that expected
 *                                  number of matching packets were received
 *
 * @note Jumps out on failure
 */
void test_transceiver_exchange_set_verdicts(
                                struct test_transceiver_exchange *exchange,
                                const char *verdict_no_pkts,
                                const char *verdict_known_unexp);

/**
 * Destroy exchange entity.
 *
 * @param  exchange     Exchange entity
 */
void test_transceiver_exchange_free(struct test_transceiver_exchange *exchange);

/**
 * Commit exchange between transceivers:
 * - send packets from @p tx
 * - sniff the packets pattern
 * - transform transmit template and sniffed pattern into template and pattern
 *   for receiving using user-provided callbacks in @p exchange (or leave any
 *   of them unchanged if its callback is @c NULL)
 * - reseive and match packets on @p rx
 *
 * The order of this actions is not strict due to limitations of underlying
 * APIs (CSAP API).
 *
 * @note Jumps out on failure
 *
 * @param[in]  exchange         Exchange entity
 * @param[in]  tx               Transmitting transceiver
 * @param[in]  n_tx_pkts        Number of packets to send
 * @param[in]  tx_queue         Transmit queue (or any value if n/a)
 * @param[in]  tx               Receiving transceiver
 * @param[in]  n_rx_pkts        Number of packets to receive and match
 * @param[in]  tx_queue         Receive queue (or any value if n/a)
 */
void test_transceiver_exchange_commit(const struct test_transceiver_exchange *exchange,
                                 struct test_transceiver *tx,
                                 unsigned int n_tx_pkts, unsigned int tx_queue,
                                 struct test_transceiver *rx,
                                 unsigned int n_rx_pkts, unsigned int rx_queue);

/**
 * Wrapper for test_transceiver_exchange_commit() that takes packet template
 * instead of exchange object - the packet is expected to be unchanged
 * from transmittion to receiving. Verdicts are set according to
 * test_transceiver_exchange_set_verdicts().
 */
void test_transciever_simple_exchange_commit(const asn_value *tmpl,
                                             struct test_transceiver *tx,
                                             unsigned int n_tx_pkts,
                                             unsigned int tx_queue,
                                             struct test_transceiver *rx,
                                             unsigned int n_rx_pkts,
                                             unsigned int rx_queue,
                                             const char *verdict_no_pkts,
                                             const char *verdict_known_unexp);

/**
 * Encompass Rx burst operation with one-by-one packet matching and
 * custom verdicts instead of generic.
 */
extern te_errno test_rx_burst_match_pattern_custom_verdicts(
                                rcf_rpc_server  *rpcs,
                                uint16_t         port_id,
                                uint16_t         queue_id,
                                rpc_rte_mbuf_p  *rx_pkts,
                                uint16_t         nb_pkts,
                                uint16_t         nb_expected,
                                const asn_value *pattern,
                                te_bool          seq_strict,
                                const char      *verdict_no_pkts,
                                const char      *verdict_known_unexp);

/** Encompass Rx burst operation with one-by-one packet matching */
extern te_errno test_rx_burst_match_pattern(rcf_rpc_server  *rpcs,
                                            uint16_t         port_id,
                                            uint16_t         queue_id,
                                            rpc_rte_mbuf_p  *rx_pkts,
                                            uint16_t         nb_pkts,
                                            uint16_t         nb_expected,
                                            const asn_value *pattern,
                                            te_bool          seq_strict);

/**
 * Wait for a certain amount of packets to be received.
 *
 * @note The function returns success even if specified number of
 *       packets is not received. Caller is responsible for checking
 *       the number of received expected and unexpected packets.
 */
extern te_errno test_rx_await_pkts(const char     *ta_name,
                                   csap_handle_t   csap_handle,
                                   unsigned int    nb_pkts,
                                   unsigned int    timeount_max_ms);

/**
 * Wait for a certain amount of packets to be received and execute
 * specified callback for each of them.
 *
 * @note See test_rx_await_pkts() notes
 */
extern te_errno test_rx_await_pkts_exec_cb(const char          *ta_name,
                                       csap_handle_t            csap_handle,
                                       unsigned int             nb_pkts,
                                       unsigned int             timeount_max_ms,
                                       tapi_tad_trrecv_cb_data *cb_data);

/** Prepare (validate) a burst of packets, then send */
extern uint16_t test_tx_prepare_and_burst(rcf_rpc_server *rpcs,
                                          uint16_t        port_id,
                                          uint16_t        queue_id,
                                          rpc_rte_mbuf_p *tx_pkts,
                                          uint16_t        nb_pkts);

/**
 * Wrapper for @b rpc_rte_pktmbuf_pool_create()
 *
 * It enforces minimum value restriction in @p data_room_size for AF_XDP.
 */
extern rpc_rte_mempool_p  test_rte_pktmbuf_pool_create(
                                                rcf_rpc_server  *rpcs,
                                                const char      *name,
                                                uint32_t         n,
                                                uint32_t         cache_size,
                                                uint16_t         priv_size,
                                                uint16_t         data_room_size,
                                                int              socket_id);

/**
 * Wrapper for @b rpc_rte_pktmbuf_pool_create() which creates packets
 * mempool taking Rx limitations into account and therefore suitable for
 * Rx queue setup.
 *
 * As well as test_rte_pktmbuf_pool_create() it enforces minimum value
 * restriction in @p data_room_size for AF_XDP.
 */
extern rpc_rte_mempool_p test_rte_pktmbuf_rx_pool_create(
                                rcf_rpc_server                *rpcs,
                                uint16_t                       port_id,
                                struct tarpc_rte_eth_dev_info *dev_info,
                                const char                    *name,
                                uint32_t                       n,
                                uint32_t                       cache_size,
                                uint16_t                       priv_size,
                                uint16_t                       data_room_size,
                                int                            socket_id);

/** Wrapper for @b tapi_rpc_rte_eth_make_eth_conf(), with test workarounds */
extern struct tarpc_rte_eth_conf *test_rpc_rte_eth_make_eth_conf(
                                          rcf_rpc_server             *rpcs,
                                          uint16_t                    port_id,
                                          struct tarpc_rte_eth_conf  *eth_conf);

/**
 * Perform Rx burst on a queue until expected number of
 * packets are received or a timeout (@c TEST_RX_PKTS_WAIT_MAX_MS)
 * passes.
 *
 * @param[in]  rpcs         RPC server handle
 * @param[in]  port_id      The port identifier of the device
 * @param[in]  queue_id     Queue to receive packets on
 * @param[in]  rx_pkts      Array of mbufs to use for received packets
 * @param[in]  nb_pkts      Number of avaiable mbufs
 * @param[in]  nb_expected  Expected number of packets that should be received
 *
 * @return number of received packets
 */
extern uint16_t test_rx_burst_with_retries(rcf_rpc_server *rpcs,
                                           uint16_t port_id,
                                           uint16_t queue_id,
                                           rpc_rte_mbuf_p *rx_pkts,
                                           uint16_t nb_pkts,
                                           uint16_t nb_expected);

/** Await link UP after port start/restart */
extern void test_await_link_up(rcf_rpc_server *rpcs,
                               uint16_t        port_id);

/** Set MTU on IUT and await link UP */
extern void test_rte_eth_dev_set_mtu_await_link_up(rcf_rpc_server *rpcs,
                                      uint16_t port_id, uint16_t mtu,
                                      struct test_ethdev_config *ethdev_config);

/**
 * Add tunnel UDP port and, if device state is STARTED,
 * cope with a possible failure caused by port restart
 */
extern te_errno test_tunnel_udp_port_add(
                        struct test_ethdev_config       *test_ethdev_config,
                        struct tarpc_rte_eth_udp_tunnel *tunnel_udp);

/**
 * Delete tunnel UDP port and, if device state is STARTED,
 * cope with a possible failure caused by port restart
 */
extern te_errno test_tunnel_udp_port_del(
                        struct test_ethdev_config       *test_ethdev_config,
                        struct tarpc_rte_eth_udp_tunnel *tunnel_udp);

/**
 * Prepare the required Ethernet device state
 *
 * @param  test_ethdev_config    Information about device configuration
 * @param  st                    The required state of device
 *
 * @retval  Status code
 */
extern te_errno test_prepare_ethdev(
    struct test_ethdev_config *test_ethdev_config,
    test_ethdev_state st);

/**
 * Prepare the required Ethernet device state using default configuration
 *
 * @param  env                 Environment binding
 * @param  rpcs                RPC server handle
 * @param  port_id             The port identifier of the device
 * @param  test_ethdev_config  Information about device configuration (out)
 * @param  st                  The required state of device
 *
 * @retval  Status code
 */
extern te_errno test_default_prepare_ethdev(
    tapi_env *env, rcf_rpc_server *rpcs, const struct if_nameindex *iut_port,
    struct test_ethdev_config *test_ethdev_config,
    test_ethdev_state st);

/**
 * Prepare the default Etheret device configuration for test
 *
 * @param  env                 Environment binding
 * @param  rpcs                RPC server handle
 * @param  port_id             The port identifier of the device
 * @param  test_ethdev_config  Information about device configuration (out)
 *
 * @retval  test_ethdev_config  Information about device configuration
 */
extern struct test_ethdev_config * test_prepare_config_def_mk(
    tapi_env *env, rcf_rpc_server *rpcs, const struct if_nameindex *iut_port,
    struct test_ethdev_config *test_ethdev_config);

/**
 * Variation of test_prepare_config_def_mk() that accepts port id and
 * device name as separate arguments.
 *
 * @param  env                  Environment binding
 * @param  rpcs                 RPC server handle
 * @param  device_name          Name of the device
 * @param  port_id              The port identifier of the device
 * @param  test_ethdev_config   Information about device configuration (out)
 *
 * @retval  test_ethdev_config  Information about device configuration
 */
extern struct test_ethdev_config * test_prepare_config_mk(tapi_env *env,
                                rcf_rpc_server *rpcs,
                                const char *dev_name,
                                uint16_t port_id,
                                struct test_ethdev_config *test_ethdev_config);

/**
 * Calculate the packet hash by the template taking into
 * account the hash function
 *
 * @param hf             Bitmask of RSS hash functions
 * @param rss_key        RSS key
 * @param rss_key_sz     RSS key size
 * @param tmpl           Template
 * @param hash_regular   Location for standard RSS hash or @c NULL
 * @param hash_symmetric Location for symmetric IP RSS hash (out) or @c NULL
 *
 * @note Symmetric hash implies that IP addresses (or low 4 bytes, for IPv6)
 *       get exclusively ORed together to produce the input for Toeplitz hash.
 *
 * @retval Status code
 */
extern te_errno test_calc_hash_by_tmpl_and_hf(tarpc_rss_hash_protos_t  hf,
                                              uint8_t                 *rss_key,
                                              size_t                   rss_key_sz,
                                              asn_value               *tmpl,
                                              uint32_t                *hash_regular,
                                              uint32_t                *hash_symmetric);

/**
 * @c test_calc_hash_by_tmpl_and_hf() variation to deal with a pattern unit
 */
extern te_errno test_rss_get_hash_by_pattern_unit(
                                  tarpc_rss_hash_protos_t  rss_hf,
                                  uint8_t                 *rss_key,
                                  size_t                   rss_key_sz,
                                  const asn_value         *pattern,
                                  int                      pattern_unit_index,
                                  uint32_t                *hash_regular,
                                  uint32_t                *hash_symmetric);

/**
 * Change source v4/v6 address by redirection table indexes to be sure that
 * the modified packet will be received on the proper queue calculated
 * using Toeplitz hash function
 *
 * @param toeplitz_hash_cache        Pre-constructed cache
 * @param hash                       RSS hash value
 * @param src_addr                   Pointer to source address
 * @param addr_size                  Size of @p src_addr in bytes
 * @param reta_size                  Redirection table size
 * @param indexes                    Array of required RETA indexes
 * @param nb_indexes                 Number of RETA indexes
 *
 * @retval Status code
 */
extern te_errno test_change_src_addr_by_reta_index(
    const te_toeplitz_hash_cache *toeplitz_hash_cache,
    unsigned int hash, uint8_t *src_addr, unsigned int addr_size,
    uint16_t reta_size, const unsigned int *indexes, unsigned int nb_indexes);

/**
 * Convert the string with several RSS hash protocols separated by commas
 * to an tarpc_rss_hash_protos_t protocols bitmask
 *
 * @param rss_hash_protos_str  String with RSS hash protocols
 *
 * @retval hash function bitmask
 */
extern tarpc_rss_hash_protos_t test_rss_hash_protos_str2bitmask(
    const char *rss_hash_protos_str);

/**
 * Anticipate RSS HF combination by traffic template.
 *
 * @param tmpl Traffic template
 * @param hf   RSS HF combination (out)
 *
 * @retval Status code
 */
extern te_errno test_get_rss_hf_by_tmpl(asn_value               *tmpl,
                                        tarpc_rss_hash_protos_t *hf);

/**
 * Parse mbuf segmentation rules.
 *
 * @param seg_ptrn_str          Segmentation rules in string format
 * @param segmentation          Segmentation rules to be filled in
 *
 * @note The function jumps out in the case of parsing faliure.
 */
extern void test_parse_mbuf_seg_ptrn(const char                 *seg_ptrn_str,
                                     struct test_param_seg_ptrn *segmentation);

/**
 * Check that tester and IUT link statuses match the expected link status
 *
 * @param tst_ta            Test Agent name
 * @param tst_if_name       Interface name
 * @param iut_rpcs          RPC server handle
 * @param iut_port_id       Port identifier (port number)
 * @param expected_status   Expected link status
 *
 * @retval status code
 */
extern te_errno test_check_iut_tst_link_status(
    const char *tst_ta, const char *tst_if_name, rcf_rpc_server *iut_rpcs,
    uint16_t iut_port_id, te_bool expected_status);

/**
 * Get xstat value by name
 *
 * @param xstat_name        Name of extended statistic
 * @param xstats_names      Array of extended statistics names
 * @param xstats            Array of extended statistics values
 * @param nb_xstats         Number of extended statistics
 * @param value             Value of extended statistic (OUT)
 *
 * @retval 0 on success and -1 on error
 */
extern int test_get_xstat_by_name(const char *xstat_name,
                                  struct tarpc_rte_eth_xstat_name *xstats_names,
                                  struct tarpc_rte_eth_xstat *xstats,
                                  unsigned nb_xstats, uint64_t *value);

/**
 * Get packet type from the pattern and check that it is supported
 *
 * @param pattern    Traffic pattern
 * @param supp       Packet types supported by the driver
 * @param supp_count The number of supported packet types
 *
 * @retval Packet type
 */
extern uint32_t test_ptype_from_pattern(const asn_value *pattern,
                                        uint32_t        *supp,
                                        unsigned int     supp_count);

/**
 * Compare expected packet type flag with an actual value
 * and provide a log message on mismatch
 *
 * @param pt_flag_expected  Expected packet type flag
 * @param pt_value_detected Detected packet type value
 * @param pt_flag_offset    Target flag group offset for the value
 *
 * @retval @c FALSE on success, @c TRUE on error
 */
extern te_bool test_check_packet_ptype(uint32_t pt_flag_expected,
                                       int      pt_value_detected,
                                       int      pt_flag_offset);

/**
 * Compare informations about Ethernet device
 *
 * @param dev_info_1    Firts device information
 * @param dev_info_2    Second device information to compare
 *
 * @retval 0 if they are equal
 */
extern te_errno test_dev_info_cmp(struct tarpc_rte_eth_dev_info *dev_info_1,
                                  struct tarpc_rte_eth_dev_info *dev_info_2);

/**
 * Change source v4/v6 address to be sure that the modified packet will be
 * received on the proper queue
 *
 * @param tmpl              Pointer to ASN.1 template
 * @param queue             The receive queue number Device redirection table size
 * @param reta_size         Device redirection table size
 * @param reta_conf         RSS RETA configuration
 * @param rss_conf          RSS hash configuration of the Ethernet device
 *
 * @retval Status code
 */
extern te_errno test_change_tmpl_ip_src_addr_by_queue_nb(
    asn_value *tmpl, uint16_t queue, uint64_t reta_size,
    struct tarpc_rte_eth_rss_reta_entry64 *reta_conf,
    struct tarpc_rte_eth_rss_conf *rss_conf);

/**
 * Convert an ASN.1 flow rule or pattern to a template containing the same set
 * of PDUs as ones in the flow rule and get flags of template match fields
 *
 * @param[in]   flow_ndn        ASN.1 flow rule or pattern
 * @param[out]  tmpl            Pointer to ASN.1 template
 * @param[out]  match_fields    Match fields flags if not @c NULL
 *
 * @retval status code
 */
extern te_errno test_mk_template_get_match_fields_from_flow_ndn(
    const asn_value *flow_ndn, asn_value **tmpl, uint32_t *match_fields);

/**
 * Get random IPv4/IPv6 address
 *
 * @param af                Address family
 *
 * @retval Address
 *
 * @note Function allocates memory, which should be freed by the caller.
 */
struct sockaddr * test_rand_ip_addr(int af);

/**
 * Set addresses of the packet
 *
 * @param[out] addrs    Packet addresses
 * @param[in] src_mac   Source MAC address
 * @param[in] dst_mac   Destination MAC address
 * @param[in] src_ip    Source IP address
 * @param[in] dst_ip    Destination IP address
 *
 * @note The function just fills struct test_pkt_addresses, so it must be
 *       allocated before function call.
 */
extern void test_set_pkt_addresses(struct test_pkt_addresses *addrs,
                                   const uint8_t *src_mac,
                                   const uint8_t *dst_mac,
                                   const struct sockaddr *src_ip,
                                   const struct sockaddr *dst_ip);

/**
 * Fill in ASN.1 template required fields
 *
 * @param tmpl          ASN.1 template
 * @param addrs         Packet outer frame addresses
 * @param ifrm_addrs    Packet inner frame addresses
 *
 * @retval status code
 */
extern te_errno test_fill_in_tmpl_req_fields(const asn_value *tmpl,
                                             struct test_pkt_addresses *addrs,
                                             struct test_pkt_addresses *ifrm_addrs);

/**
 * Copy template with changed specific fields. Addresses change to new values, other
 * fields are increased by one. New addresses could not match to values in the template.
 *
 * @param tmpl              Pointer to ASN.1 template
 * @param fields            Template fields flags that need to change
 * @param addrs             Packet outer frame addresses
 * @param ifrm_addrs        Packet inner frame addresses
 *
 * @retval status code
 */
extern asn_value *test_tmpl_copy_with_changed_fields(const asn_value *tmpl,
                                                     uint32_t fields,
                                                     struct test_pkt_addresses *addrs,
                                                     struct test_pkt_addresses *ifrm_addrs);

/**
 * Make copies of ASN.1 template with change only one field. Each copy
 * corresponds to a change of the one of fields.
 *
 * @param[in] tmpl          Pointer to ASN.1 template
 * @param[in] fields        Template fields flags that need to change
 * @param[in] addrs         Basic packet outer frame addresses
 * @param[in] ifrm_addrs    Basic packet inner frame addresses
 * @param[out] tmpls_out    Pointer to array of changed ASN.1 template copies
 * @param[out] tmpls_nb_out Number of tempaltes in the array
 *
 * @retval status code
 *
 * @note Function allocates memory for array of templates, which should be
 *       freed by the caller.
 */
extern te_errno test_mk_tmpls_with_change_one_field(const asn_value *tmpl,
                                                    uint32_t fields,
                                                    struct test_pkt_addresses *addrs,
                                                    struct test_pkt_addresses *ifrm_addrs,
                                                    asn_value *(*tmpls_out[]),
                                                    unsigned int *tmpls_nb_out);

/**
 * Pull out Rx information from an RSS flow rule
 */
extern te_errno test_get_rx_info_by_rss_action(
                               const asn_value  *flow_rule_rss,
                               uint16_t        **rss_queues_out,
                               uint16_t         *nb_rss_queues_out,
                               uint16_t         *nb_queues_out,
                               uint64_t         *rss_hf,
                               uint8_t         **rss_key_out,
                               size_t           *rss_key_sz_out);

/**
* Add the appropriate tunnel UDP port if VXLAN/GENEVE protocols are in the template
*/
extern te_errno test_add_tunnel_udp_port_from_tmpl(
                               struct test_ethdev_config *ethdev_config,
                               asn_value *tmpl, te_bool required);

/**
 * Spoil checksums across the PDUs and make notes on GOOD and BAD
 * checksum offload flags which _could be expected, respectively,
 * for the original packet and for the spoiled packet
 */
extern te_errno test_pdus_detect_and_spoil_cksums(unsigned int   nb_pdus,
                                                  asn_value    **pdus,
                                                  uint64_t      *flags_good_out,
                                                  uint64_t      *flags_bad_out,
                                                  int           *l4_type_out);

/**
 * Make RTE flow attributes with only one "ingress" attribute
 *
 * @param[in]   rpcs        RPC server handle
 * @param[out]  attr        RTE flow attr pointer
 */
extern te_errno test_mk_rte_flow_attr_ingress(rcf_rpc_server *rpcs,
                                              rpc_rte_flow_attr_p *attr);

/**
 * Make RTE flow actions with only one "drop" action
 *
 * @param[in]   rpcs        RPC server handle
 * @param[out]  actions     RTE flow actions pointer
 */
extern te_errno test_mk_rte_flow_action_drop(rcf_rpc_server *rpcs,
                                             rpc_rte_flow_action_p *actions);

/**
 * Make RTE flow actions with only one "queue" action
 *
 * @param[in]   rpcs        RPC server handle
 * @param[in]   queue       Queue number
 * @param[out]  actions     RTE flow actions pointer
 */
extern te_errno test_mk_rte_flow_action_queue(rcf_rpc_server *rpcs,
                                              uint16_t queue,
                                              rpc_rte_flow_action_p *actions);

/**
 * Validate Tx offload request and generate Tx mode
 * and Tx queue configuration based on DPDK version.
 *
 * @param[in]  tecp          Test ethdev configuration
 * @param[in]  test_offloads Offload request in terms of Tx offload
 *                           capability flags supported by TAPI RPC
 * @param[out] txmodep       Tx mode configuration to be filled in, or @c NULL
 * @param[out] txconfp       Tx queue configuration to be filled in, or @c NULL
 *
 * @return @c 0 on success, @c TE_EINVAL in the case of verdict,
 *         other non-zero value in the case of techical problem.
 */
extern te_errno test_mk_txmode_txconf(
                                struct test_ethdev_config   *tecp,
                                uint64_t                     test_offloads,
                                struct tarpc_rte_eth_txmode *txmodep,
                                struct tarpc_rte_eth_txconf *txconfp);
/**
 * Get RSS RETA configuration using rss_reta_query() RPC.
 * Fill RETA with default values and generate warning if the RPC
 * is not supported.
 *
 * @param[in]  rpcs          RPC server handle
 * @param[in]  port_id       The port identifier of the device
 * @param[in]  reta_size     Location for length of RETA table
 * @param[out] reta_conf     Location for RETA configuration
 */
extern void test_get_rss_reta(rcf_rpc_server *rpcs,
                              uint16_t port_id,
                              uint64_t *reta_size,
                              struct tarpc_rte_eth_rss_reta_entry64 **reta_conf);

/**
 * Start Tx queue with specified @p queue_id using rx_queue_start() RPC
 * Stop test with warning if the RPC is not supported
 *
 * @param[in]  rpcs          RPC server handle
 * @param[in]  port_id       The port identifier of the device
 * @param[in]  queue_id      The index of queue to start
 */
extern void test_start_tx_queue(rcf_rpc_server *rpcs,
                                uint16_t port_id,
                                uint16_t queue_id);

/**
 * Start Rx queue with specified @p queue_id using rx_queue_start() RPC
 * Stop test with warning if the RPC is not supported
 *
 * @param[in]  rpcs          RPC server handle
 * @param[in]  port_id       The port identifier of the device
 * @param[in]  queue_id      The index of queue to start
 */
extern void test_start_rx_queue(rcf_rpc_server *rpcs,
                                uint16_t port_id,
                                uint16_t queue_id);
/**
 * Fill RSS with provided @p hf and regular or random RSS key
 *
 * @param[in]  hf            Hash functions to include into RSS configuration
 * @param[in]  rss_key_size  RSS key size
 * @param[in]  regular       If @c TRUE - setup predefined regular RSS key,
 *                           setup random key otherwise
 * @param[out] rss_conf      RSS configuration to setup
 */
extern void test_setup_rss_configuration(
                                    tarpc_rss_hash_protos_t hf,
                                    size_t rss_key_sz, te_bool regular,
                                    struct tarpc_rte_eth_rss_conf *rss_conf);


/**
 * Try to get RSS configuration using rss_hash_conf_get() RPC
 *
 * @param[in]  rpcs          RPC server handle
 * @param[in]  rss_key_sz    Expected MAX RSS key size
 * @param[in]  port_id       The port identifier of the device
 *
 * @return  Pointer to allocated RSS configuration on heap on success
 *          @c NULL if the RPC is not supported
 */

extern struct tarpc_rte_eth_rss_conf *test_try_get_rss_hash_conf(
                                                        rcf_rpc_server *rpcs,
                                                        size_t rss_key_sz,
                                                        uint16_t port_id);
/**
 * Send one packet with @p tmpl, receive it on @p queue and match it
 *
 * @param[in]  rpcs              RPC server handle
 * @param[in]  port_id           The port identifier of the device
 * @param[in]  tst_ta            Test Agent name
 * @param[in]  tst_if_name       Interface name
 * @param[in]  mbufs             Array of mbufs to use for received packets
 * @param[in]  tmpl              Traffic template
 * @param[in]  queue             Queue to receive packets on
 * @param[in]  packets_expected  Number of expected received packets
 */
extern void test_send_and_match_one_packet(rcf_rpc_server *rpcs,
                                           uint16_t port_id,
                                           const char *tst_ta,
                                           const char *tst_if_name,
                                           rpc_rte_mbuf_p *mbufs,
                                           asn_value *tmpl,
                                           unsigned int queue,
                                           unsigned int packet_expected);

/**
 * Get string representation of a test_ethdev_state enum value
 *
 * @param[in]  state        Device state
 */
extern const char *test_get_ethdev_state_name(test_ethdev_state state);

/**
 * Get length of a single packet generated from a traffic template
 *
 * @param[in]  rpcs              RPC server handle
 * @param[in]  tmpl              Traffic template
 * @param[in]  mp                RTE mempool pointer
 */
extern uint32_t
test_get_template_packet_length(rcf_rpc_server *rpcs, asn_value *tmpl,
                                rpc_rte_mempool_p mp);

/**
 * Get header length of a traffic template
 *
 * @param[in]  rpcs              RPC server handle
 * @param[in]  tmpl              Traffic template
 * @param[in]  mp                RTE mempool pointer
 */
extern unsigned int test_get_template_header_length(rcf_rpc_server *rpcs,
                                                    asn_value *tmpl,
                                                    rpc_rte_mempool_p mp);

/**
 * Find out if a requested Tx offload is supported by looking into
 * configuration tree
 *
 * @param[in]  offload_bit       Bit of the offload to query
 */
extern te_bool test_conf_tx_offload_supported(unsigned long long offload_bit);

/**
 * Find out if a requested Rx offload is supported by looking into
 * configuration tree
 *
 * @param[in]  offload_bit       Bit of the offload to query
 */
extern te_bool test_conf_rx_offload_supported(unsigned long long offload_bit);

/**
 * Add a QUEUE action to an action list at specified index.
 *
 * @param[inout]  ndn_actions   Action list
 * @param[in]     action_index  Index at which QUEUE action is put to list
 * @param[in]     queue         Queue index of QUEUE action
 */
extern void test_add_ndn_action_queue(asn_value *ndn_actions, int action_index,
                                      uint16_t queue);

/**
 * Add a DROP action to an action list at specified index.
 *
 * @param[inout]  ndn_actions   Action list
 * @param[in]     action_index  Index at which DROP action is put to list
 */
extern void test_add_ndn_action_drop(asn_value *ndn_actions, int action_index);

/**
 * Convert an ASN value representing a flow rule pattern into
 * RTE flow rule pattern and a template that matches the pattern.
 *
 * @param[in]     rpcs                  RPC server handle
 * @param[in]     flow_rule_pattern     ASN.1 representation of a flow rule
 *                                      pattern
 * @param[out]    pattern               RTE pattern
 * @param[out]    tmpl                  Matching ASN.1 template
 * @param[out]    match_fields          Match fields flags if not @c NULL
 */
extern void test_mk_pattern_and_tmpl_by_flow_rule_pattern(rcf_rpc_server *rpcs,
                                     const asn_value *flow_rule_pattern,
                                     rpc_rte_flow_item_p *pattern,
                                     asn_value **tmpl, uint32_t *match_fields);

/**
 * Generate flow rule patterns with a changed field.
 *
 * @param[in]  flow_rule_pattern        Flow rule pattern to build other
 *                                      patterns upon
 * @param[in]  field_path               ASN.1 path to a field that needs to
 *                                      be changed
 * @param[in]  n_changed_patterns       Size of an array of changed patterns
 * @param[out] changed_patterns         Pointer to an array of changed patterns
 *
 * @return RTE flow pointer on success; jumps out on failure
 */
extern te_errno test_generate_changed_flow_patterns(
                                            const asn_value *flow_rule_pattern,
                                            const char *field_path,
                                            size_t n_changed_patterns,
                                            asn_value **changed_patterns);

/**
 * Concatenate PDU sequences in two packet templates/patterns.
 *
 * @param[in]     dst   Pattern or template to which PDUs are appended
 * @param[in]     src   Pattern or template from which PDUs are retrieved
 *
 * @return ASN.1 value with concatenated PDUs on success; jumps out on failure
 */
extern asn_value * test_concatenate_tmpl_ptrn_pdus(const asn_value *dst,
                                                   const asn_value *src,
                                                   const char *label);
/**
 * Remove outer frame from a PDU sequence in a template/pattern.
 *
 * @param[in]     val   Encapsulated pattern or template
 * @param[in]     label Path to PDU sequence in @p val
 *
 * @return Decapsulated ASN.1 value on success; jumps out on failure
 */
extern asn_value * test_decap_tmpl_ptrn_pdus(const asn_value *val,
                                             const char *label);

/**
 * Check that query data for COUNT action meets provided expectations.
 *
 * @note Jumps out on check fail
 *
 * @param[in]  data         RTE flow query counter data
 * @param[in]  check_hits   Check number of hits if @c TRUE
 * @param[in]  hits         Expected number of hits
 * @param[in]  check_bytes  Check number of bytes if @c TRUE
 * @param[in]  bytes        Expected nymber of bytes
 */
extern void test_check_flow_query_data(const tarpc_rte_flow_query_data *data,
                                       te_bool check_hits, uint64_t hits,
                                       te_bool check_bytes, uint64_t bytes);
/**
 * Add an ASN.1 flow rule action "count" to action list at specified index
 * and make RTE flow actions with only this action.
 *
 * @note Jumps out on failure
 *
 * @param[in]     counter_id    Counter index
 * @param[in]     action_id     Index at which the action is put to list
 * @param[in]     rpcs          RPC server handle
 * @param[inout]  ndn_actions   ASN.1 flow rule action list
 * @param[out]    rte_actions   RTE flow actions pointer
 */
extern void test_add_and_mk_rte_flow_action_count(uint32_t counter_id,
                                            int action_id,
                                            rcf_rpc_server *rpcs,
                                            asn_value *ndn_actions,
                                            rpc_rte_flow_action_p *rte_actions);

/**
 * Check that extended statistic is correct
 *
 * @param[in] name             String with extended statistic name
 * @param[in] value            Correct value
 * @param[in] xstats_names     Name of array of extended statistics names
 * @param[in] xstats           Name of array of extended statistics values
 * @param[in] nb_xstats        Name of variable with the number of extended
 *                             statistics
 * @param[in] wrong_xstats     String with wrong extended statistics names
 * @param[in] zero_xstats      String with always zero extended
 *                             statistics names
 */
extern void test_check_xstat(const char *name, uint64_t value,
                 struct tarpc_rte_eth_xstat_name *xstats_names,
                 struct tarpc_rte_eth_xstat *xstats, int nb_xstats,
                 te_string *wrong_xstats, te_string *zero_xstats);

/**
 * Check that PDU sequence has destination MAC set.
 *
 * @param[in] pdus              PDU sequence
 *
 * @return  @c TRUE if destination MAC is set
 */
extern te_bool test_pdus_dst_mac_is_set(const asn_value *pdus);

/**
 * Set source and destination MAC addresses of outer frame in a PDU sequence.
 *
 * @note PDU sequence must contain outer Ethernet PDU
 *
 * @param      container    ASN.1 container in which PDU sequence is changed
 * @param[in]  pdus_path    Path to PDU sequence in the @p container
 * @param[in]  src          Source MAC address to set (ignore if @c NULL)
 * @param[in]  dst          Destination MAC address to set (ignore if @c NULL)
 *
 * @note The function jumps out on failure
 */
extern void test_pdus_set_eth_src_dst(asn_value *pdus, const char *pdus_path,
                                      const uint8_t *src, const uint8_t *dst);

enum test_op_required {
    TEST_OP_OPTIONAL = 0,
    TEST_OP_REQUIRED,
};

/**
 * Enable promiscuous mode and check that rte_eth_promiscuous_get()
 * reports enabled promiscuous mode.
 *
 * @param[in]  rpcs             RPC server handle
 * @param[in]  port_id          The port identifier of the device
 * @param[in]  required         Is operation required?
 */
void test_rte_eth_promiscuous_enable(rcf_rpc_server *rpcs, uint16_t port_id,
                                     enum test_op_required required);

/**
 * Disable promiscuous mode and check that rte_eth_promiscuous_get()
 * reports disabled promiscuous mode.
 *
 * @param[in]  rpcs             RPC server handle
 * @param[in]  port_id          The port identifier of the device
 * @param[in]  required         Is operation required?
 */
void test_rte_eth_promiscuous_disable(rcf_rpc_server *rpcs, uint16_t port_id,
                                      enum test_op_required required);

/**
 * Enable all-multicast mode and check that rte_eth_allmulticast_get()
 * reports enabled all-multicast mode.
 *
 * @param[in]  rpcs             RPC server handle
 * @param[in]  port_id          The port identifier of the device
 * @param[in]  required         Is operation required?
 */
void test_rte_eth_allmulticast_enable(rcf_rpc_server *rpcs, uint16_t port_id,
                                      enum test_op_required required);

/**
 * Disable all-multicast mode and check that rte_eth_allmulticast_get()
 * reports disabled all-multicast mode.
 *
 * @param[in]  rpcs             RPC server handle
 * @param[in]  port_id          The port identifier of the device
 * @param[in]  required         Is operation required?
 */
void test_rte_eth_allmulticast_disable(rcf_rpc_server *rpcs, uint16_t port_id,
                                       enum test_op_required required);

/**
 * Create a new traffic pattern with only one unit, that is get from @p ptrn
 * by index.
 *
 * @note    Jumps out on failure
 *
 * @param[in] ptrn              Traffic pattern
 * @paran[in] index             Index of needed pattern unit
 *
 * @return New pattern that contains only one unit
 */
asn_value * test_mk_pattern_by_unit(const asn_value *ptrn, int index);

/**
 * Send one packet with @p tmpl, receive it on @p queue and match it. Report
 * custom verdicts.
 *
 * @param[in]  rpcs              RPC server handle
 * @param[in]  port_id           The port identifier of the device
 * @param[in]  tst_ta            Test Agent name
 * @param[in]  tst_if_name       Interface name
 * @param[in]  mbufs             Array of mbufs to use for received packets
 * @param[in]  tmpl              Traffic template
 * @param[in]  queue             Queue to receive packets on
 * @param[in]  packets_expected  Number of expected received packets
 */
extern void test_send_and_match_one_packet_custom_verdicts(rcf_rpc_server *rpcs,
                                               uint16_t port_id,
                                               const char *tst_ta,
                                               const char *tst_if_name,
                                               rpc_rte_mbuf_p *mbufs,
                                               asn_value *tmpl,
                                               unsigned int queue,
                                               unsigned int packet_expected,
                                               const char *verdict_no_pkts,
                                               const char *verdict_known_unexp);

/**
 * Get number of Tx descriptors which could be required per packet.
 */
extern unsigned int test_get_extra_tx_descs_per_pkt(void);

/**
 * Get TSO payload cutoff barrier (i.e. without net headers length).
 *
 * @param hdrs_len      Net headers length.
 */
extern unsigned int test_get_tso_payload_cutoff_barrier(unsigned int hdrs_len);

/**
 * Hotplug VFs with indices specified by @p vf_ids.
 *
 * @param[in]    rpcs       RPC server handle
 * @param[in]    env        Environment binding
 * @param[in]    node       Network node of the device (PF)
 * @param[in]    n_vfs      The number of VFs to hotplug
 * @param[in]    vf_ids     Indices of the VFs to hotplug
 * @param[out]   vfs        Configurations of the added VFs
 *
 * @note The function jumps out on failure
 */
extern void test_hotplug_vfs_by_ids(rcf_rpc_server *rpcs, tapi_env *env,
                                    const cfg_net_node_t *node,
                                    unsigned int n_vfs,
                                    const unsigned int *vf_ids,
                                    struct test_ethdev_config **vfs);

/**
 * Determine if a device is virtual by its name.
 *
 * @param[in]  device_name      Name of a device
 *
 * @return @c TRUE if device is virtual, @c FALSE otherwise
 */
extern te_bool test_is_vdev(const char *device_name);

/**
 * Add port representors of the device of a network @p node using hotplug
 * and create ethdev configurations for each of the representor.
 * A port representor is created for each index in @p rep_ids
 *
 * @param[in]  rpcs         RPC server handle
 * @param[in]  node         Network node of the device (PF)
 * @param[in]  n_rep        Size of @p rep_ids
 * @param[in]  rep_ids      Array of representor indexes to add
 * @param[out] reps         Configurations of the added port representors
 *
 * @note The function jumps out on failure
 */
extern void test_hotplug_reps(rcf_rpc_server *rpcs, tapi_env *env,
                              const cfg_net_node_t *node,
                              unsigned int n_rep,
                              const unsigned int *rep_ids,
                              struct test_ethdev_config **reps);

/**
 * Get VF PCI addresses of a PF associated with network node.
 *
 * @param[in]  node         Network node of the device (PF)
 * @param[out] n_vf_addrs   Number of VFs
 * @param[out] vf_addrs     VF PCI addresses
 * @param[out] vf_ids       Indices of the VFs
 *
 * @note The function jumps out on failure
 */
extern void test_get_vf_pci_addrs_by_node(const cfg_net_node_t *node,
                                          unsigned int *n_vf_addrs,
                                          char ***vf_addrs,
                                          unsigned int **vf_ids);

/**
 * Create parameters for traffic generation
 *
 * @param       ta                Test agent to run generator on
 * @param       arg_prefix        App-specific prefix for arguments
 * @param       command_prefix    App-specific prefix for commands
 * @param       txpkts            TX segment size
 * @param       multi_flow        Generate many flow to use RSS on Rx side
 * @param       txq               Number of Tx queues (0 for default number)
 * @param       txd               Number of Tx descriptors
 * @param       burst             Number of packets per burst
 * @param       txfreet           Number of transmit free threshold of TX rings
 * @param[out]  params            List of kvpairs to store parameters
 * @param[out]  n_cores           Location to put required number of testpmd
 *                                data cores
 */
extern te_errno test_create_traffic_generator_params(const char *ta,
                                                     char *arg_prefix,
                                                     const char *command_prefix,
                                                     const char *forward_mode,
                                                     const char *txpkts,
                                                     te_bool multi_flow,
                                                     unsigned int txq,
                                                     unsigned int txd,
                                                     unsigned int burst,
                                                     unsigned int txfreet,
                                                     te_kvpair_h **params,
                                                     unsigned int *n_cores);

/**
 * Create parameters for traffic reception
 *
 * @param       arg_prefix        App-specific prefix for arguments
 * @param       command_prefix    App-specific prefix for commands
 * @param       rxq               Number of Rx queues
 * @param       packet_size       Packet size in bytes (without l1 and FCS)
 * @param[out]  params            List of kvpairs to store parameters
 */
extern te_errno test_create_traffic_receiver_params(const char *arg_prefix,
                                                    const char *command_prefix,
                                                    unsigned int rxq,
                                                    unsigned int packet_size,
                                                    te_kvpair_h **params);

/**
 * Deploy RTE af_packet on top of a tester's regular network interface.
 *
 * @note One must invoke @c test_rte_af_packet_on_tst_if_release() on cleanup.
 * @note The function jumps out on failure.
 *
 * @param[in] tst_rpcs   RPC server handle
 * @param[in] tst_if     Network interface description
 * @param[in] frame_size The biggest packet's size
 * @param[in] nb_frames  The number of packets which need to be received
 *
 * @return RTE port ID which can be used to invoke @c rpc_rte_eth_rx_burst().
 */
extern uint16_t test_rte_af_packet_on_tst_if_deploy(
                                    rcf_rpc_server            *tst_rpcs,
                                    const struct if_nameindex *tst_if,
                                    size_t                     frame_size,
                                    unsigned int               nb_frames);

/**
 * Release RTE af_packet which has been previously deployed
 * on top of a tester's regular network interface.
 *
 * @note The function jumps out on failure.
 *
 * @param[in] tst_rpcs RPC server handle
 * @param[in] tst_if   Network interface description
 */
extern void test_rte_af_packet_on_tst_if_release(
                                     rcf_rpc_server            *tst_rpcs,
                                     const struct if_nameindex *tst_if);

/**
 * Clone the given mbuf and preserve its flags + Tx offload meta information.
 *
 * @param[in]  rpcs_src Source RPC server handle
 * @param[in]  m_src    The mbuf to be cloned
 * @param[in]  rpcs_dst Destination RPC server handle
 * @param[in]  mp_dst   Destination mempool handle
 * @param[out] m_dstp   Location for the new mbuf pointer
 *
 * @return Status code
 */
extern te_errno test_clone_mbuf(rcf_rpc_server    *rpcs_src,
                                rpc_rte_mbuf_p     m_src,
                                rcf_rpc_server    *rpcs_dst,
                                rpc_rte_mempool_p  mp_dst,
                                rpc_rte_mbuf_p    *m_dstp);

/**
 * Append randomised payload to the given mbuf.
 *
 * @note The function will trim existing non-header data.
 * @note The header will not be updated.
 *
 * @param[in] rpcs RPC server handle
 * @param[in] size Payload size
 * @param[in] m    The mbuf
 *
 * @return Status code
 */
extern te_errno test_tx_mbuf_set_random_payload(rcf_rpc_server *rpcs,
                                                size_t          size,
                                                rpc_rte_mbuf_p  m);

extern te_errno test_get_pci_fn_prop(rcf_rpc_server *rpcs,
                                     const struct if_nameindex *port,
                                     char *prop, unsigned int *value);

extern te_errno test_add_pci_fn_prop(rcf_rpc_server *rpcs,
                                     const struct if_nameindex *port,
                                     char *prop, unsigned int value);

/**
 * Skip the test if required packet size is out of MTU limitations
 * reported by the device and saved in local configuration tree.
 *
 * @param[in] rpcs              RPC server handle
 * @param[in] port              Network interface description
 * @param[in] packet_size       Maximum packet size required for the test
 */
extern void test_check_mtu(rcf_rpc_server *rpcs,
                           const struct if_nameindex *port,
                           unsigned int packet_size);

/**
 * Set requested MTU to the device considering maximum/minimum reported MTU.
 * Errors are reported as verdicts.
 *
 * @param[in] rpcs              RPC server handle
 * @param[in] port_id           Port identifier
 * @param[in] mtu               MTU to set
 * @param[in] ethdev_config     Information about device configuration
 */
extern void test_set_mtu(rcf_rpc_server *rpcs, uint16_t port_id, uint16_t mtu,
                         struct test_ethdev_config *ethdev_config);

/**
 * Do default template prepare procedure that essentially sets MAC,
 * packets number and payload and substitutes "#env" units.
 *
 * @param p     Holds template to prepare with auxiliary data.
 */
extern void test_default_template_prepare(struct test_default_tmpl_prepare *p);

/**
 * Wait default time for statistics update with motivation logging.
 */
extern void test_wait_stats_update();

/**
 * Check given descriptors number against descriptors limits.
 *
 * @param[in] desc_nb   Descriptors number
 * @param[in] desc_lim  Descriptors limits
 *
 * @return Boolean if descriptors number violates reported limits
 */
extern te_bool test_desc_nb_violates_limits(unsigned int desc_nb,
                                 const struct tarpc_rte_eth_desc_lim *desc_lim);

/**
 * Collect diagnostcs in the case of no packet delivery to the expected
 * queue in accordance with RSS settings.
 *
 * @param[in] rpcs              RPC server handle
 * @param[in] port_id           Port identifier
 * @param[in] nb_rx_queues      Number of Rx queues
 * @param[in] reta_size         Redirection table size
 * @param[in] reta_conf         Redirection table
 * @param[in] ptrn              Expected packet
 * @param[in] expected_hash     Expected RSS hash value
 * @param[in] expected_queue    Expected Rx queues
 */
extern void test_check_rss_queues(rcf_rpc_server *rpcs, unsigned int port_id,
                uint16_t nb_rx_queues, uint16_t reta_size,
                const struct tarpc_rte_eth_rss_reta_entry64 *reta_conf,
                asn_value *ptrn,
                uint32_t expected_hash, uint16_t expected_queue);

/**
 * Check RSS hash value provided in mbuf if available.
 *
 * @param[in] rpcs              RPC server handle
 * @param[in] mbuf              Packet buffer handle
 * @param[in] expected_hash     Expected RSS hash value
 * @param[in] symmetric_hash    Symmetric RSS hash value
 */
extern void test_check_mbuf_rss_hash_value(rcf_rpc_server *rpcs,
                                           rpc_rte_mbuf_p mbuf,
                                           uint32_t expected_hash,
                                           uint32_t symmetric_hash);

#endif /* !__TS_DPDK_PMD_TS_H__ */
