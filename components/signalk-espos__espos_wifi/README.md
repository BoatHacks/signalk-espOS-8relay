# espos_wifi

Station + provisioning portal, a pure-C state machine, static IP, co-processor watchdog; reports into espos_net.

```sh
idf.py add-dependency "signalk-espos/espos_wifi^0.7"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_wifi` works with the same version of every other.

**Documentation:** [docs/wifi.md](https://github.com/signalk-espOS/espOS/blob/main/docs/wifi.md)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_wifi` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
