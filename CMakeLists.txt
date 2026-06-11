cmake_minimum_required(VERSION 3.14)
project(litehttpd C CXX)

# C11 for module source, C++17 for tests
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared library output
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Security hardening flags
#
# This module is loaded into a network-facing web server and parses
# semi-trusted .htaccess files plus fully untrusted request data, so it is
# built with standard exploit-mitigation flags. These are applied to all
# targets defined below (module, tool, tests). The RPM spec also passes the
# distro %optflags; these ensure hardening even in a plain cmake build.
# ---------------------------------------------------------------------------
include(CheckCCompilerFlag)
set(HARDENING_C_FLAGS)
foreach(_flag
        -fstack-protector-strong
        -fstack-clash-protection
        -Wformat
        -Werror=format-security)
    string(MAKE_C_IDENTIFIER "HAVE${_flag}" _flag_var)
    check_c_compiler_flag("${_flag}" ${_flag_var})
    if(${_flag_var})
        list(APPEND HARDENING_C_FLAGS "${_flag}")
    endif()
endforeach()
add_compile_options(${HARDENING_C_FLAGS})
# _FORTIFY_SOURCE requires optimization; only meaningful in non-Debug builds.
add_compile_definitions($<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=2>)
# Full RELRO + immediate binding and non-executable stack for all links.
add_link_options(-Wl,-z,relro,-z,now -Wl,-z,noexecstack)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Explicitly list all C source files (preferred over GLOB for reliable builds)
set(MODULE_SOURCES
    src/htaccess_cache.c
    src/htaccess_cidr.c
    src/htaccess_directive.c
    src/htaccess_dirwalker.c
    src/htaccess_exec_acl.c
    src/htaccess_exec_auth.c
    src/htaccess_exec_brute_force.c
    src/htaccess_exec_dirindex.c
    src/htaccess_exec_encoding.c
    src/htaccess_exec_env.c
    src/htaccess_exec_error_doc.c
    src/htaccess_exec_expires.c
    src/htaccess_exec_files_match.c
    src/htaccess_exec_forcetype.c
    src/htaccess_exec_handler.c
    src/htaccess_exec_header.c
    src/htaccess_exec_limit.c
    src/htaccess_exec_options.c
    src/htaccess_exec_php.c
    src/htaccess_exec_redirect.c
    src/htaccess_exec_require.c
    src/htaccess_exec_rewrite.c
    src/htaccess_expr.c
    src/htaccess_expires.c
    src/htaccess_parser.c
    src/htaccess_printer.c
    src/htaccess_shm.c
    src/lsiapi_shim.c
    src/mod_htaccess.c
    src/ols_native_api.c
)

# Build the shared library (.so)
if(MODULE_SOURCES)
    add_library(litehttpd_htaccess SHARED ${MODULE_SOURCES})
    target_include_directories(litehttpd_htaccess PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(litehttpd_htaccess PRIVATE MNAME=litehttpd_htaccess)
    target_link_libraries(litehttpd_htaccess crypt dl)
    set_target_properties(litehttpd_htaccess PROPERTIES
        OUTPUT_NAME "litehttpd_htaccess"
        PREFIX ""
        SUFFIX ".so"
    )
endif()

# Testing dependencies — only when BUILD_TESTING is ON (default).
# Use -DBUILD_TESTING=OFF to build only the .so without network access.
option(BUILD_TESTING "Build tests (requires network for googletest/rapidcheck)" ON)

if(BUILD_TESTING)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )

    FetchContent_Declare(
        rapidcheck
        GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
        GIT_TAG master  # rapidcheck has no tagged releases
    )

    # Prevent GoogleTest from overriding compiler/linker settings on Windows
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    # Enable RapidCheck Google Test integration (extras/gtest)
    set(RC_ENABLE_GTEST ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest rapidcheck)

    # Enable testing
    enable_testing()

    # Add test subdirectory
    add_subdirectory(tests)
endif()

# Apache config converter tool (litehttpd-confconv)
add_executable(litehttpd-confconv
    tools/ols-confconv.c
    tools/apacheconf_parser.c
    tools/ols_config_writer.c
)
target_include_directories(litehttpd-confconv PRIVATE ${CMAKE_SOURCE_DIR}/tools)
install(TARGETS litehttpd-confconv DESTINATION bin)
