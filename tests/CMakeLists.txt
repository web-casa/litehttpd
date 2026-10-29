# Test configuration for ols-htaccess module
# Unit tests use Google Test, property tests use RapidCheck + Google Test

include(GoogleTest)

# Include directories for test helpers and generators
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/tests/generators
)

# Collect module source files for linking into tests
# Exclude lsiapi_shim.c — tests use mock_lsiapi.cpp instead
file(GLOB MODULE_SOURCES ${CMAKE_SOURCE_DIR}/src/*.c)
list(FILTER MODULE_SOURCES EXCLUDE REGEX "lsiapi_shim\\.c$")

# Mock LSIAPI layer (shared by all test targets)
set(MOCK_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/mock_lsiapi.cpp)

# --- Unit Tests ---
file(GLOB UNIT_TEST_SOURCES unit/*.cpp)
# Exclude confconv tests from main unit_tests (they need different sources)
list(FILTER UNIT_TEST_SOURCES EXCLUDE REGEX "test_apacheconf\\.cpp$")
if(UNIT_TEST_SOURCES)
    add_executable(unit_tests ${UNIT_TEST_SOURCES} ${MODULE_SOURCES} ${MOCK_SOURCES})
    target_compile_definitions(unit_tests PRIVATE MNAME=litehttpd_htaccess HTACCESS_UNIT_TEST)
    litehttpd_link_pcre2(unit_tests)
    target_link_libraries(unit_tests
        GTest::gtest
        GTest::gtest_main
        crypt
    )
    gtest_discover_tests(unit_tests)
endif()

# --- Apache Config Converter Tests ---
set(CONFCONV_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_apacheconf.cpp)
set(CONFCONV_SOURCES
    ${CMAKE_SOURCE_DIR}/tools/apacheconf_parser.c
    ${CMAKE_SOURCE_DIR}/tools/ols_config_writer.c
)
add_executable(confconv_tests ${CONFCONV_TEST_SOURCES} ${CONFCONV_SOURCES})
target_include_directories(confconv_tests PRIVATE ${CMAKE_SOURCE_DIR}/tools)
target_link_libraries(confconv_tests
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(confconv_tests)

# --- Property Tests ---
file(GLOB PROPERTY_TEST_SOURCES property/*.cpp)
if(PROPERTY_TEST_SOURCES)
    add_executable(property_tests ${PROPERTY_TEST_SOURCES} ${MODULE_SOURCES} ${MOCK_SOURCES})
    target_compile_definitions(property_tests PRIVATE MNAME=litehttpd_htaccess HTACCESS_UNIT_TEST)
    litehttpd_link_pcre2(property_tests)
    target_link_libraries(property_tests
        GTest::gtest
        GTest::gtest_main
        rapidcheck
        rapidcheck_gtest
        crypt
    )
    gtest_discover_tests(property_tests)
endif()

# --- Compatibility Tests ---
file(GLOB COMPAT_TEST_SOURCES compat/*.cpp)
if(COMPAT_TEST_SOURCES)
    add_executable(compat_tests ${COMPAT_TEST_SOURCES} ${MODULE_SOURCES} ${MOCK_SOURCES})
    target_compile_definitions(compat_tests PRIVATE
        MNAME=litehttpd_htaccess
        COMPAT_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/tests/compat/htaccess_samples"
    )
    litehttpd_link_pcre2(compat_tests)
    target_link_libraries(compat_tests
        GTest::gtest
        GTest::gtest_main
        crypt
    )
    gtest_discover_tests(compat_tests)
endif()

add_executable(test_cidr_table test_cidr_table.cpp)
target_sources(test_cidr_table PRIVATE ../src/htaccess_cidr.c ../src/htaccess_cidr_table.c)
target_include_directories(test_cidr_table PRIVATE ../include)
target_link_libraries(test_cidr_table gtest_main)
gtest_discover_tests(test_cidr_table)

add_executable(test_esi test_esi.cpp)
target_sources(test_esi PRIVATE ../src/htaccess_esi.c)
target_include_directories(test_esi PRIVATE ../include)
target_link_libraries(test_esi gtest_main)
gtest_discover_tests(test_esi)

add_executable(test_shm_fork test_shm_fork.cpp)
target_sources(test_shm_fork PRIVATE ../src/htaccess_shm.c ../src/ols_native_api.c)
target_include_directories(test_shm_fork PRIVATE ../include)
target_link_libraries(test_shm_fork gtest_main pthread rt crypt)
gtest_discover_tests(test_shm_fork)

add_executable(test_cache_bridge test_cache_bridge.cpp)
target_sources(test_cache_bridge PRIVATE ../src/htaccess_exec_cache.c ${MOCK_SOURCES})
target_include_directories(test_cache_bridge PRIVATE ../include)
target_link_libraries(test_cache_bridge gtest_main)
gtest_discover_tests(test_cache_bridge)

add_executable(test_firewall_client test_firewall_client.cpp)
target_sources(test_firewall_client PRIVATE ../src/htaccess_firewall_client.c)
target_include_directories(test_firewall_client PRIVATE ../include)
target_link_libraries(test_firewall_client gtest_main pthread)
gtest_discover_tests(test_firewall_client)
