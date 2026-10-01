# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# NMEA 2000 over TWAI: receiver, transmitter, and a candump-format TCP
# server so the bus is reachable from a laptop (`candump`, canboatjs, a
# SignalK n2k-ip-gateway source). Board-agnostic — the application picks
# the TX/RX pins and bitrate; nothing here assumes a particular panel.
#
# Built on IDF 6's esp_twai node API. The peripheral is touched only by
# src/twai_node.cpp, which owns the node handle the receiver and the
# transmitter share; everything else — including the candump codec, which
# is why this component now has host tests — is driver-free.
idf_component_register(
    SRCS src/twai_node.cpp src/twai_receiver.cpp src/twai_transmitter.cpp
         src/api_n2k.cpp
         src/candump_tcp_server.cpp src/candump_format.cpp
    INCLUDE_DIRS include
    # esp_driver_gpio is PUBLIC, not private: twai_receiver.h is a public
    # header and its TwaiReceiverConfig exposes gpio_num_t, so every consumer
    # needs driver/gpio.h on its include path. As a PRIV_REQUIRES it did not
    # propagate, and a project that required only espos_n2k failed to compile
    # on the component's own header -- which the cockpit never hit only because
    # its HAL happens to require esp_driver_gpio too.
    REQUIRES esp_driver_twai esp_timer esp_driver_gpio
    PRIV_REQUIRES lwip espressif__mdns)

# GET /api/v1/n2k is worth having but not worth requiring: a headless N2K
# gateway with no web server is a legitimate firmware, and api_n2k.cpp guards
# itself with __has_include so it compiles to a stub there. The optional
# dependency is what puts espos_httpd's headers on the include path when it
# IS in the build -- without it __has_include answers no even though the
# component is present.
# Namespace-tolerant component checks (espOS #138). Its own copy, because a
# component installed from the registry cannot reach a sibling's directory,
# and this component does not require espos_core. The file guards against
# being included twice.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/espos_components.cmake")

espos_optional_requires(PRIVATE espos_httpd espressif__cjson)
