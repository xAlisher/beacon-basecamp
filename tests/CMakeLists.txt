cmake_minimum_required(VERSION 3.14)
project(LogosBeaconTests LANGUAGES CXX)

include(LogosTest)

# The module derives channel ids locally via OpenSSL EVP (Ed25519), so the test
# binary must link libcrypto too (mirrors the module's CMakeLists LINK_TARGETS).
find_package(OpenSSL REQUIRED)

logos_test(
    NAME logos_beacon_tests
    MODULE_SOURCES
        ../src/logos_beacon_impl.cpp
    TEST_SOURCES
        main.cpp
        test_derivation.cpp
        test_inscription.cpp
    EXTRA_INCLUDES
        ../src
    # logos_beacon_impl.cpp #includes the generated logos_sdk.h and uses
    # modules().zone_sequencer (in seqPublish). The builder's own test codegen emits
    # the Qt flavor, which mismatches this universal (std) module — so we ship a
    # pre-generated std logos_sdk.{h,cpp} + a minimal std zone_sequencer stub here.
    GENERATED_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/generated_std
    EXTRA_LINK_LIBS
        OpenSSL::Crypto
)
