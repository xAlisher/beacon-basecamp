#pragma once

#include "rep_beacon_ui_source.h"     // generated from src/beacon_ui.rep (repc)
#include "logos_ui_plugin_context.h"  // modules() + onContextReady()

/**
 * @brief beacon_ui backend — bridges QML to the universal zone_sequencer.
 *
 * QML (logos.module("beacon_ui") + logos.watch) → this backend → the Qt-free
 * universal zone_sequencer via modules().zone_sequencer.*. Replaces beacon's
 * legacy logos.callModule("liblogos_zone_sequencer_module", ...) path, which
 * returns "null" for universal modules (beacon#30). Pattern proven by
 * logos-zone-sequencer-module/doctests/ui-backend-fixture.
 */
class BeaconUiBackend : public BeaconUiSimpleSource,
                        public LogosUiPluginContext
{
public:
    QString configure(QString signingKeyHex, QString nodeUrl) override;
    QString deriveChannelId(QString signingKeyHex) override;
    QString publish(QString payload) override;
    QString publishTo(QString channelId, QString signingKeyHex,
                      QString checkpointPath, QString payload) override;
    QString ping() override;

protected:
    void onContextReady() override { setStatus(QStringLiteral("zone_sequencer wired")); }
};
