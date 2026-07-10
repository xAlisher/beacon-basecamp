#include <logos_test.h>
#include "logos_beacon_impl.h"

// pinCid + confirmInscription id-handling. The regression this locks: an item's own
// per-publish tx hash (stored at submit) must survive a later status-only update, and
// must never be overwritten (beacon#43 — ids used to collapse to the shared channel tip).

LOGOS_TEST(pinCid_appends_queued_entry_with_empty_id) {
    LogosBeaconImpl impl;
    impl.clearInscriptionLog();
    auto p = impl.pinCid("bafcidA", "label A", "keeper", 100, 90);
    LOGOS_ASSERT_TRUE(p.success);
    LOGOS_ASSERT_EQ(p.value.at("entryIndex").get<int>(), 0);
    auto log = impl.getInscriptionLog();
    LOGOS_ASSERT_EQ(static_cast<int>(log.value.size()), 1);
    LOGOS_ASSERT_EQ(log.value.at(0).at("status").get<std::string>(), std::string("queued"));
    LOGOS_ASSERT_EQ(log.value.at(0).at("inscriptionId").get<std::string>(), std::string(""));
}

LOGOS_TEST(confirmInscription_stores_id_and_status) {
    LogosBeaconImpl impl;
    impl.clearInscriptionLog();
    impl.pinCid("bafcidB", "l", "keeper", 100, 90);
    LOGOS_ASSERT_TRUE(impl.confirmInscription(0, "deadbeef1234", "confirmed").success);
    auto log = impl.getInscriptionLog();
    LOGOS_ASSERT_EQ(log.value.at(0).at("inscriptionId").get<std::string>(), std::string("deadbeef1234"));
    LOGOS_ASSERT_EQ(log.value.at(0).at("status").get<std::string>(),        std::string("confirmed"));
}

// THE Phase-1 fix: a later empty-id status update (e.g. "finalizing") must keep the
// real tx hash captured at submit — not wipe it.
LOGOS_TEST(confirmInscription_preserves_id_on_status_only_update) {
    LogosBeaconImpl impl;
    impl.clearInscriptionLog();
    impl.pinCid("bafcidC", "l", "keeper", 100, 90);
    impl.confirmInscription(0, "txhash_abc", "submitted");
    impl.confirmInscription(0, "", "finalizing");   // status-only; id must survive
    auto log = impl.getInscriptionLog();
    LOGOS_ASSERT_EQ(log.value.at(0).at("inscriptionId").get<std::string>(), std::string("txhash_abc"));
    LOGOS_ASSERT_EQ(log.value.at(0).at("status").get<std::string>(),        std::string("finalizing"));
}

LOGOS_TEST(confirmInscription_out_of_range_errors) {
    LogosBeaconImpl impl;
    LOGOS_ASSERT_FALSE(impl.confirmInscription(9999, "x", "confirmed").success);
    LOGOS_ASSERT_FALSE(impl.confirmInscription(-1,   "x", "confirmed").success);
}

// A CID already inscribed (non-empty id) is a duplicate; one still queued (empty id)
// is re-queued (removed + re-appended), not counted as a duplicate.
LOGOS_TEST(pinCid_dedups_only_when_already_inscribed) {
    LogosBeaconImpl impl;
    impl.clearInscriptionLog();
    impl.pinCid("bafcidD", "l", "keeper", 100, 90);
    impl.confirmInscription(0, "tx_D", "confirmed");
    auto dup = impl.pinCid("bafcidD", "l", "keeper", 101, 91);
    LOGOS_ASSERT_TRUE(dup.success);
    LOGOS_ASSERT_TRUE(dup.value.value("duplicate", false));
    LOGOS_ASSERT_EQ(static_cast<int>(impl.getInscriptionLog().value.size()), 1);
}
