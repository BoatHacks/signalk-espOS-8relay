# espos_n2k

NMEA 2000 over TWAI + a candump TCP server.

```sh
idf.py add-dependency "signalk-espos/espos_n2k^0.7"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_n2k` works with the same version of every other.

**Documentation:** [docs/n2k.md](https://github.com/signalk-espOS/espOS/blob/main/docs/n2k.md)

**Examples:**

* [`n2k_candump`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_n2k/examples/n2k_candump)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_n2k` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
