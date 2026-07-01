#include "beacon_ui_backend.h"

#include "logos_sdk.h"   // generated: modules().zone_sequencer (Qt-typed)

// Map a Qt LogosResult to the QString contract QML expects: the value on
// success, or an "error: ..."-prefixed string on failure (QML checks the prefix).
static QString unwrap(const LogosResult& r)
{
    return r.success ? r.getString() : (QStringLiteral("error: ") + r.getError());
}

QString BeaconUiBackend::configure(QString signingKeyHex, QString nodeUrl)
{
    if (!isContextReady())
        return QStringLiteral("error: context not ready");
    auto& zs = modules().zone_sequencer;
    // Stateful sequence — modules() reaches the one persistent zone_sequencer
    // host instance, so state survives across these calls (unlike per-call GUI
    // callModule). Config setters are fire-and-forget; the channel is what QML needs.
    zs.set_signing_key(signingKeyHex);
    zs.set_node_url(nodeUrl);
    zs.set_checkpoint_path(QString());   // empty = bootstrap fresh
    LogosResult ch = zs.get_channel_id();
    if (!ch.success)
        return QStringLiteral("error: ") + ch.getError();
    const QString channel = ch.getString();
    zs.set_channel_id(channel);
    setZoneSeqReady(true);
    return channel;
}

QString BeaconUiBackend::deriveChannelId(QString signingKeyHex)
{
    if (!isContextReady())
        return QStringLiteral("error: context not ready");
    return unwrap(modules().zone_sequencer.derive_channel_id(signingKeyHex));
}

QString BeaconUiBackend::publish(QString payload)
{
    if (!isContextReady())
        return QStringLiteral("error: context not ready");
    return unwrap(modules().zone_sequencer.publish(payload));
}

QString BeaconUiBackend::publishTo(QString channelId, QString signingKeyHex,
                                   QString checkpointPath, QString payload)
{
    if (!isContextReady())
        return QStringLiteral("error: context not ready");
    return unwrap(modules().zone_sequencer.publish_to(channelId, signingKeyHex,
                                                      checkpointPath, payload));
}

QString BeaconUiBackend::ping()
{
    return QStringLiteral("{\"ok\":true,\"module\":\"beacon_ui\",\"ctxReady\":%1}")
        .arg(isContextReady() ? QStringLiteral("true") : QStringLiteral("false"));
}
