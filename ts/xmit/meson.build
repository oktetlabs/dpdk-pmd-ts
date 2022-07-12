# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved.

tests = [
    'alternate_vlan',
    'one_packet',
    'one_packet_with_dpdk_rx',
    'one_packet_with_dpdk_rx_epilogue',
    'one_packet_with_dpdk_rx_prologue',
    'reap_on_stop',
    'vlan_on_packet_drop',
    'vlan_on_port_restart',
    'vlan_txqs_interference',
]

foreach test : tests
    test_exe = test
    test_c = test + '.c'
    package_tests_c += [ test_c ]
    executable(test_exe, test_c, install: true, install_dir: package_dir,
               dependencies: test_deps)
endforeach

tests_info_xml = custom_target(package_dir.underscorify() + 'tests-info-xml',
                               install: true, install_dir: package_dir,
                               input: package_tests_c,
                               output: 'tests-info.xml', capture: true,
                               command: [ te_tests_info_sh,
                                          meson.current_source_dir() ])

install_data([ 'package.dox', 'package.xml' ],
             install_dir: package_dir)
