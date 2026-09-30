# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Namespace-tolerant component checks. Included by each component that needs
# them rather than relying on any single component being in the build: a
# firmware may use espos_ota without espos_core, and before this file existed
# that failed with `Unknown CMake command "espos_optional_requires"` (found in
# review of espOS #138 and reproduced).
#
# Guarded so several components including it is harmless.
if(COMMAND espos_has_component)
    return()
endif()

# ---------------------------------------------------------------------------
# espos_has_component(<var> <bare-name>)
#
# Is a component in this build, whatever it is called?
#
# BUILD_COMPONENTS holds CMake TARGET names, and the component manager
# namespaces those on install: a registry copy of espos_wifi builds as
# `signalk-espos__espos_wifi`, while an in-tree checkout builds as plain
# `espos_wifi`. So `if(espos_wifi IN_LIST comps)` is true in the tree and
# silently FALSE for every registry install -- and silence is the problem:
# espos_core's optional stages simply compiled out, so espos_start() brought up
# no WiFi, no SignalK client and no OTA on a firmware that had linked all three,
# with no error and no warning (espOS #138, found downstream in
# BoatHacks/signalk-espos-8relay#11).
#
# espOS's own CI could not catch it, because the from_registry example uses
# override_path and therefore builds under the bare names too.
#
# So this matches the entries of BUILD_COMPONENTS themselves: exactly, for an
# in-tree build, or on a `__<bare name>` suffix for a namespaced one. Suffix
# rather than stripping a literal `signalk-espos__`, so a fork or mirror
# published under another namespace keeps working.
#
# Not by reading COMPONENT_NAME, which would be the other way to do it:
# idf_component_get_property() raises a FATAL_ERROR on a name it cannot resolve,
# and a component is resolvable that way only once it has been registered, which
# is not guaranteed when these run.
# An optional third argument receives the name as this build actually spells it,
# so a caller that then needs idf_component_get_property() passes THAT rather
# than the bare name: that function raises a FATAL_ERROR on a name it cannot
# resolve, and it resolves a bare name only once the component has been
# registered -- which is not guaranteed at the point these run.
function(espos_has_component var name)
    idf_build_get_property(_espos_comps BUILD_COMPONENTS)
    foreach(_espos_t ${_espos_comps})
        # Cheap path first: an in-tree build matches on the nose.
        if(_espos_t STREQUAL name)
            set(${var} TRUE PARENT_SCOPE)
            if(ARGC GREATER 2)
                set(${ARGV2} ${_espos_t} PARENT_SCOPE)
            endif()
            return()
        endif()
        # A namespaced target ends in `__<bare name>`. Compare the suffix rather
        # than stripping a fixed `signalk-espos__`, so a fork or a mirror
        # published under another namespace keeps working.
        if(_espos_t MATCHES "__${name}$")
            set(${var} TRUE PARENT_SCOPE)
            if(ARGC GREATER 2)
                set(${ARGV2} ${_espos_t} PARENT_SCOPE)
            endif()
            return()
        endif()
    endforeach()
    set(${var} FALSE PARENT_SCOPE)
    if(ARGC GREATER 2)
        set(${ARGV2} "" PARENT_SCOPE)
    endif()
endfunction()

# espos_optional_requires(<req_type> <bare-name>...)
#
# idf_component_optional_requires() that works for a namespaced install.
#
# IDF's own version has the same defect this file's helper exists for: it tests
# `req IN_LIST build_components` against TARGET names
# (tools/cmake/component.cmake), so an optional dependency named by its bare name
# is silently skipped when the component is installed from the registry. That is
# how espos_n2k's `GET /api/v1/n2k` became a 404 on registry installs -- the
# optional requirement on espos_httpd did not link, so its headers were not on
# the include path, so __has_include("espos_httpd.h") answered no and the handler
# compiled to a stub (espOS #138).
#
# Names that already carry a namespace (espressif__cjson) pass through: they are
# matched literally by the same helper.
function(espos_optional_requires req_type)
    foreach(_req ${ARGN})
        espos_has_component(_present ${_req} _real)
        if(_present)
            # ${_real}, not ${_req}: idf_component_get_property() raises a
            # FATAL_ERROR on a name it cannot resolve, and its bare-name fallback
            # only works once that component has been registered -- not
            # guaranteed here. Verified: passing the bare name failed with
            # "Failed to resolve component 'espos_sk'".
            idf_component_get_property(_req_lib ${_real} COMPONENT_LIB)
            target_link_libraries(${COMPONENT_LIB} ${req_type} ${_req_lib})
        endif()
    endforeach()
endfunction()
