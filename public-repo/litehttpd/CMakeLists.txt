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
# Expose GNU libc extensions used by the module (for example strcasestr).
add_compile_definitions(_GNU_SOURCE)
# _FORTIFY_SOURCE requires optimization; only meaningful in non-Debug builds.
add_compile_definitions($<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=2>)
# Full RELRO + immediate binding and non-executable stack for all links.
add_link_options(-Wl,-z,relro,-z,now -Wl,-z,noexecstack)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# FilesMatch uses Apache-compatible PCRE syntax. Prefer a release-build static
# PCRE2 root when provided by the RPM spec; otherwise use the host package.
set(PCRE2_ROOT "" CACHE PATH "Prefix containing PCRE2 include/ and lib/")
set(LITEHTTPD_PCRE2_INCLUDE_DIRS)
set(LITEHTTPD_PCRE2_LIBRARIES)
set(LITEHTTPD_PCRE2_LIBRARY_DIRS)
set(LITEHTTPD_PCRE2_STATIC OFF)

if(PCRE2_ROOT)
    find_path(LITEHTTPD_PCRE2_INCLUDE_DIR
        NAMES pcre2.h
        PATHS "${PCRE2_ROOT}/include"
        NO_DEFAULT_PATH)
    find_library(LITEHTTPD_PCRE2_LIBRARY
        NAMES pcre2-8 libpcre2-8.a
        PATHS "${PCRE2_ROOT}/lib" "${PCRE2_ROOT}/lib64"
        NO_DEFAULT_PATH)
    if(NOT LITEHTTPD_PCRE2_INCLUDE_DIR OR NOT LITEHTTPD_PCRE2_LIBRARY)
        message(FATAL_ERROR "PCRE2_ROOT=${PCRE2_ROOT} does not contain pcre2.h and libpcre2-8")
    endif()
    set(LITEHTTPD_PCRE2_INCLUDE_DIRS "${LITEHTTPD_PCRE2_INCLUDE_DIR}")
    set(LITEHTTPD_PCRE2_LIBRARIES "${LITEHTTPD_PCRE2_LIBRARY}")
    set(LITEHTTPD_PCRE2_STATIC ON)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PCRE2 QUIET libpcre2-8)
    endif()
    if(PCRE2_FOUND)
        set(LITEHTTPD_PCRE2_INCLUDE_DIRS ${PCRE2_INCLUDE_DIRS})
        set(LITEHTTPD_PCRE2_LIBRARIES ${PCRE2_LIBRARIES})
        set(LITEHTTPD_PCRE2_LIBRARY_DIRS ${PCRE2_LIBRARY_DIRS})
    else()
        find_path(LITEHTTPD_PCRE2_INCLUDE_DIR NAMES pcre2.h)
        find_library(LITEHTTPD_PCRE2_LIBRARY NAMES pcre2-8)
        if(NOT LITEHTTPD_PCRE2_INCLUDE_DIR OR NOT LITEHTTPD_PCRE2_LIBRARY)
            message(FATAL_ERROR "PCRE2 development files are required (libpcre2-8)")
        endif()
        set(LITEHTTPD_PCRE2_INCLUDE_DIRS "${LITEHTTPD_PCRE2_INCLUDE_DIR}")
        set(LITEHTTPD_PCRE2_LIBRARIES "${LITEHTTPD_PCRE2_LIBRARY}")
    endif()
endif()

function(litehttpd_link_pcre2 target_name)
    target_include_directories(${target_name} PRIVATE ${LITEHTTPD_PCRE2_INCLUDE_DIRS})
    if(LITEHTTPD_PCRE2_LIBRARY_DIRS)
        target_link_directories(${target_name} PRIVATE ${LITEHTTPD_PCRE2_LIBRARY_DIRS})
    endif()
    target_link_libraries(${target_name} ${LITEHTTPD_PCRE2_LIBRARIES})
    if(LITEHTTPD_PCRE2_STATIC)
        target_compile_definitions(${target_name} PRIVATE PCRE2_STATIC)
    endif()
endfunction()

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
    litehttpd_link_pcre2(litehttpd_htaccess)
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
