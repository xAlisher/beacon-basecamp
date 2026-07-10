#include <logos_test.h>
#include "logos_beacon_impl.h"

// Channel id = Ed25519 public key of the 32-byte signing-key seed (RFC 8032).
// This MUST stay byte-identical to zone_sequencer's ed25519-dalek derivation,
// because beacon now computes it locally (no IPC) — the fix for the getSourceChannel
// reentrancy SIGSEGV (beacon#50). Locked against two independent vectors.

// RFC 8032 Ed25519 test vector 1 — the canonical correctness anchor.
LOGOS_TEST(ed25519_channel_matches_rfc8032_vector1) {
    LogosBeaconImpl impl;
    auto r = impl.seqDeriveChannel(
        "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_EQ(r.value.get<std::string>(),
        std::string("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));
}

// The value zone_sequencer's ed25519-dalek produces for seed = "aa"*32
// (asserted in doctests/logos-beacon-universal.test.yaml) — proves parity.
LOGOS_TEST(ed25519_channel_matches_zone_sequencer_vector) {
    LogosBeaconImpl impl;
    auto r = impl.seqDeriveChannel(std::string(64, 'a'));
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_EQ(r.value.get<std::string>(),
        std::string("e734ea6c2b6257de72355e472aa05a4c487e6b463c029ed306df2f01b5636b58"));
}

// Deterministic + distinct.
LOGOS_TEST(ed25519_channel_is_deterministic_and_distinct) {
    LogosBeaconImpl impl;
    auto a1 = impl.seqDeriveChannel(std::string(64, '1'));
    auto a2 = impl.seqDeriveChannel(std::string(64, '1'));
    auto b  = impl.seqDeriveChannel(std::string(64, '2'));
    LOGOS_ASSERT_TRUE(a1.success);
    LOGOS_ASSERT_EQ(a1.value.get<std::string>(), a2.value.get<std::string>());
    LOGOS_ASSERT_NE(a1.value.get<std::string>(), b.value.get<std::string>());
}

// Bad input → failure, not a crash / garbage channel.
LOGOS_TEST(ed25519_channel_rejects_bad_key) {
    LogosBeaconImpl impl;
    LOGOS_ASSERT_FALSE(impl.seqDeriveChannel("not-hex-not-64-chars").success);
    LOGOS_ASSERT_FALSE(impl.seqDeriveChannel("00").success);            // too short
    LOGOS_ASSERT_FALSE(impl.seqDeriveChannel(std::string(63, 'a')).success); // odd length
}

// Per-source subchannel = Ed25519(SHA256(masterKey + source)) — end to end, no IPC.
LOGOS_TEST(source_channel_is_sha256_then_ed25519) {
    LogosBeaconImpl impl;
    LOGOS_ASSERT_TRUE(impl.setSigningKey(std::string(64, '1')).success);
    auto sk = impl.deriveModuleSigningKey("keeper");
    LOGOS_ASSERT_TRUE(sk.success);
    const std::string keeperKey = sk.value.at("signingKey").get<std::string>();
    auto viaKey    = impl.seqDeriveChannel(keeperKey);
    auto viaSource = impl.getSourceChannel("keeper");
    LOGOS_ASSERT_TRUE(viaSource.success);
    LOGOS_ASSERT_EQ(viaSource.value.get<std::string>(), viaKey.value.get<std::string>());
}
