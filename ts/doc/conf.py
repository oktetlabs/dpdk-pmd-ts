# -*- coding: utf-8 -*-
# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved.

project = u'DPDK EthDev Test Suite'
copyright = u'(c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved.'
author = u'Andrew Rybchenko'
version = u'1.0'
release = u'release'
latex_documents = [
    (
        'index',
        'dpdk_ethdev_test_suite.tex',
        u'DPDK EthDev Test Suite',
        u'Andrew Rybchenko',
        'manual',
    ),
]

# Specify the path to Doxyrest extensions for Sphinx:

import sys
import os
doxyrest_path = os.getenv('DOXYREST_PREFIX')
sys.path.insert(1, os.path.abspath(doxyrest_path + '/share/doxyrest/sphinx'))

source_suffix = '.rst'
master_doc = 'index'
language = "c"

exclude_patterns = [
    'generated/rst/index.rst',
]

import sphinx_rtd_theme
extensions = [
    "sphinx_rtd_theme"
]
html_theme = 'sphinx_rtd_theme'

html_theme_options = {
    'collapse_navigation': False,
}

extensions += ['doxyrest', 'cpplexer']

