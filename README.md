# DPDK-EthDev-TS - test suite for DPDK Networking Drivers

DPDK EthDev TS carries out testing and performance evaluation of Poll Mode Drivers used by DPDK.

The tests cover (list not exhaustive):

* basic operations like sending and receiving traffic, setting MTU and getting statistics from the NIC;
* representor functionality;
* flow filtering.

OKTET Labs TE is used as an Engine that allows to prepare desired environment for every test.
This guarantees reproducible test results.

## Documentation

The suite documentation is generated with doxygen and doxyrest:

```
export DOXYREST_PREFIX=/path/to/doxyrest-<version>-linux-amd64
./scripts/gen_doxygen --html
```

Run `$TE_BASE/scripts/doxyrest_deploy.sh --put` first if doxyrest is not
installed; `--how` prints what it would do without doing it.

Test scenarios are read out of the C sources with libclang, so the
Python bindings are needed to document them properly:

```
apt install python3-clang     # or: pip install libclang
```

The build still succeeds without them, but every scenario comes out
flat: steps lose the conditions and loops guarding them, and substeps do
not nest.

## License

See the LICENSE file in this repository
