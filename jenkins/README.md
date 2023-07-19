[SPDX-License-Identifier: Apache-2.0]::
[Copyright (C) 2023 OKTET Labs Ltd. All rights reserved.]::

# Jenkins pipelines

The code here can be used to organize automated testing via Jenkins.

It uses TE Jenkins library (see `README.md` in `te-jenkins` repository) and
also assumes that `ts-rigs` repository is available and contains some site
specific information.

## Required ts-rigs content

`ts-rigs` should contain `jenkins/defs/dpdk-ethdev-ts/defs.groovy` file which
sets some test suite specific variables in `set_defs()` method. Example:

```
def set_defs(ctx) {
    ctx.TS_GIT_URL = "https://some/path/dpdk-ethdev-ts.git"
    ctx.TS_DEF_BRANCH = 'main'

    // Variables required for publish-logs
    ctx.PUBLISH_LOGS_NODE = 'ts-logs'
    ctx.TS_LOGS_SUBPATH = 'logs/dpdk-ethdev-ts/'

    // Variables required for bublik-import
    ctx.PROJECT = 'project_name'
    ctx.TS_BUBLIK_URL = 'https://foobar.com/bublik/'
    ctx.TS_LOGS_URL_PREFIX = 'https://foobar.com/someprefix/'

    // Log listener used by Bublik
    ctx.TS_LOG_LISTENER_NAME = 'somelistener'
}

return this
```

`ts-rigs` can also contain a pipeline for running tests on your hosts
according to a schedule, based on `teScheduledRunPipeline` template.
See `README.md` in `te-jenkins` and comments in the template file for
more information.

## How to configure pipelines in Jenkins

See `README.md` in `te-jenkins`, there you can find generic information
about configuring Jenkins and creating pipelines.

Specification for the following pipelines is available here:
- `update` - pipeline for rebuilding sources after detecting new commits
  in used repositories (including this one and TE). Based on `teTsPipeline`
  template in `te-jenkins`. If you want to test different repositories or
  branches, you can create multiple update jobs with different parameter
  values, and pass name of appropriate update job as `update_job` parameter
  to `run` job to test specific repository or branch.
  Node with label `dpdk-ethdev-ts` should be configured in Jenkins where
  this pipeline will be run. This test suite will be built there.
- `doc` - pipeline for building documentation for this test suite, based on
  `teTsDoc` template.
  Node with label `dpdk-ethdev-ts-doc` should be configured in Jenkins where
  this pipeline will be run.
- `run` - pipeline for running tests and publishing testing logs, based
  on `teTsPipeline` template. Single `run` job can be created for all
  configurations, or multiple `run` jobs for different configurations.
  Node with label `dpdk-ethdev-ts` should be configured in Jenkins where
  this pipeline will be run. This test suite will be built and run there.

### Configuring emails about finished pipelines

You can specify destination addresses for emails by setting
`TE_EMAIL_TO_DPDK_ETHDEV_TS` environment variable in Jenkins. This
variable can contain one or more email addresses (use ';' as a separator).

Default sender address can be specified by setting
`TE_EMAIL_FROM_DEF` in environment. This variable can contain `__USER__`
string which will be replaced with current user name.
