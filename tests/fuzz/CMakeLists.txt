# Fuzz targets: built only with Clang + BUILD_FUZZING=ON (libFuzzer).
# Usage (on VM371):
#   cmake -B build-fuzz -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
#         -DBUILD_FUZZING=ON -DBUILD_TESTING=OFF
#   cmake --build build-fuzz -j2 && ctest --test-dir build-fuzz
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "BUILD_FUZZING requires Clang (libFuzzer)")
endif()
enable_testing()

set(FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
set(FUZZ_LINK -fsanitize=fuzzer,address,undefined)

add_library(htaccess_fuzz_core STATIC EXCLUDE_FROM_ALL
    ../../src/htaccess_parser.c
    ../../src/htaccess_directive.c
    ../../src/htaccess_expires.c
    ../../src/htaccess_cidr.c
)
target_include_directories(htaccess_fuzz_core PUBLIC ../../include)

function(add_fuzz_target name)
    add_executable(${name} EXCLUDE_FROM_ALL ${name}.cpp)
    target_compile_options(${name} PRIVATE ${FUZZ_FLAGS})
    target_link_options(${name} PRIVATE ${FUZZ_LINK})
    target_link_libraries(${name} PRIVATE htaccess_fuzz_core)
    add_test(NAME ${name}-smoke
             COMMAND ${name} -runs=200 ${CMAKE_CURRENT_SOURCE_DIR}/corpus)
endfunction()

add_fuzz_target(fuzz_parser)
add_fuzz_target(fuzz_redirect)
add_fuzz_target(fuzz_cidr)
