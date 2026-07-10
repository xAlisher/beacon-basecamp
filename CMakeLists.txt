cmake_minimum_required(VERSION 3.14)
project(LogosBeacon LANGUAGES CXX)

# logos_beacon builds via logos-module-builder (nix build .#lgx / .#lgx-portable),
# which runs the codegen for the universal impl_header + the modules() dependency
# wrappers. LOGOS_MODULE_BUILDER_ROOT is always set inside those nix builds.
if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)

    # v0.2 universal impl (LogosModuleContext) with codegen impl_header (metadata).
    # QtNetwork is pulled in transitively by RemoteObjects; QtCore is linked by
    # logos_module() — the pimpl in logos_beacon_impl.cpp uses both.
    logos_module(
        NAME logos_beacon
        SOURCES
            src/logos_beacon_impl.h
            src/logos_beacon_impl.cpp
        INCLUDE_DIRS
            src
        # OpenSSL EVP for local Ed25519 channel-id derivation (beacon#50): the channel
        # id is the Ed25519 public key of the signing key (RFC 8032, matches
        # zone_sequencer's ed25519-dalek). Deriving it in-process replaces the crashing
        # sync modules().zone_sequencer.derive_channel_id IPC. OpenSSL is already in the
        # protocol-layer closure.
        FIND_PACKAGES OpenSSL
        LINK_TARGETS OpenSSL::Crypto
    )
else()
    message(FATAL_ERROR
        "logos_beacon builds via logos-module-builder (nix build .#lgx). "
        "The legacy standalone-cmake path was removed together with BeaconPlugin.")
endif()
