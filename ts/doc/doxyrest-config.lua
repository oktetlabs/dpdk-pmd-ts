-- SPDX-License-Identifier: Apache-2.0
-- (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.
-- Specify input and output paths:
INPUT_FILE = "ts/doc/generated/xml/index.xml"
OUTPUT_FILE = "ts/doc/generated/rst/index.rst"

INTRO_FILE = "index.rst"

-- Specific title
INDEX_TITLE = "DPDK PMD Test Suite"

-- If your documentation uses \verbatim directives for code snippets
-- you can convert those to reStructuredText C++ code-blocks:
VERBATIM_TO_CODE_BLOCK = "c"

-- Asterisks, pipes and trailing underscores have special meaning in
-- reStructuredText. If they appear in Doxy-comments anywhere except
-- for code-blocks, they must be escaped:
ESCAPE_ASTERISKS = true
ESCAPE_PIPES = true
ESCAPE_TRAILING_UNDERSCORES = true

SORT_GROUPS_BY = "id"
