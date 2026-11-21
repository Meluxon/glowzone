# SPDX-FileCopyrightText: 2026 Meluxon
# SPDX-License-Identifier: GPL-2.0-or-later

# Fuzzer for the root helper's request checks (clang only):
#   cmake -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DGLOWZONE_FUZZ=ON
#   cmake --build build-fuzz --target fuzz_setled
#   mkdir -p corpus && build-fuzz/bin/fuzz_setled -max_total_time=300 corpus fuzz/seeds
# The seeds are valid requests, so SetLED is reached from the start; new
# inputs go to corpus/.

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "GLOWZONE_FUZZ needs clang (libFuzzer)")
endif()

# The helper code is built into the fuzzer itself, so it is instrumented too.
add_executable(fuzz_setled
    fuzz_setled.cpp
    ${PROJECT_SOURCE_DIR}/src/helper/ecservice.cpp
    ${PROJECT_SOURCE_DIR}/src/model/dmi.cpp
    ${PROJECT_SOURCE_DIR}/src/model/modelprofile.cpp
    ${PROJECT_SOURCE_DIR}/src/model/helperprotocol.cpp
)
target_include_directories(fuzz_setled PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(fuzz_setled PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_setled PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_libraries(fuzz_setled PRIVATE Qt6::Core KF6::I18n)
