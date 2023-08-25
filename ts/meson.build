# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved.

project('dpdk-pmd-ts', 'c',
    version : '1.0.0',
    default_options: 'werror=true'
)

te_path = get_option('te_path')
te_libdir = get_option('te_libdir')
add_project_arguments(get_option('te_cflags').split(), language: 'c')
add_project_link_arguments(get_option('te_ldflags').split(), language: 'c')

cc = meson.get_compiler('c')

te_tests_info_sh = find_program(join_paths(te_path, 'te_tests_info.sh'))

test_deps = [ dependency('threads') ]

# The line below would produce empty dependencies on systems
# having no tirpc - it is not a problem.
dep_rpc = dependency('libtirpc', required: false)
test_deps += [ dep_rpc ]

te_libs = [
    'rpc_dpdk',
    'rpcc_dpdk',
    'tapi_dpdk',
    'tapi_env',
    'tapi_job',
    'tapi_rpc',
    'rcfrpc',
    'tapi',
    'confapi',
    'conf_oid',
    'rcfapi',
    'rpcxdr',
    'rpc_types',
    'logger_ten',
    'ipc',
    'tools',
    'tapi_tad',
    'ndn',
    'asn',
    'logger_core',
]

foreach lib : te_libs
    test_deps += cc.find_library(lib, dirs: te_libdir)
endforeach

lib_dir = include_directories('lib')

subdir('lib')

test_deps += declare_dependency(include_directories: lib_dir,
                                link_with: [ tslib ])

tests = [ 'prologue' ]

package_dir = 'dpdk-pmd-ts'
package_tests_c = []

foreach test : tests
    test_exe = test
    test_c = test + '.c'
    package_tests_c += [ test_c ]
    executable(test_exe, test_c, install: true, install_dir: package_dir,
               dependencies: test_deps)
endforeach

install_data([ 'package.xml' ],
             install_dir: package_dir)

packages = [
    'usecases',
    'xmit',
    'filters',
    'representors',
    'perf',
]

mydir = package_dir
foreach package : packages
    package_dir = join_paths(mydir, package)
    package_tests_c = []
    subdir(package)
endforeach
