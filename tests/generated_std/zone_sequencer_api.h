#pragma once
#include <string>
#include <logos_result.h>

class LogosAPI;

// Minimal std-flavor stub of the zone_sequencer typed handle — UNIT TESTS ONLY.
//
// The builder's test codegen (mkLogosModuleTests → logos-cpp-generator --general-only)
// emits the *Qt* flavor (QString / logos::CallError), which mismatches this universal
// (std) module. logos_beacon_impl.cpp only touches zone_sequencer in seqPublish
// (set_node_url + publish_to), and the unit suite never exercises seqPublish — it tests
// local Ed25519 derivation and the inscription log — so trivial stubs are enough to make
// the module translation unit compile against the std ABI it was written for.
class ZoneSequencer {
public:
    explicit ZoneSequencer(LogosAPI* = nullptr) {}
    StdLogosResult set_node_url(const std::string&) { return StdLogosResult{false, {}, "unit-test stub"}; }
    StdLogosResult publish_to(const std::string&, const std::string&, const std::string&, const std::string&) {
        return StdLogosResult{false, {}, "unit-test stub"};
    }
};
