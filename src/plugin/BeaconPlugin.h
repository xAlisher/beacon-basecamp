#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QJsonArray>

#include "interface.h"

class BeaconPlugin : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.logos.BeaconModuleInterface" FILE "plugin_metadata.json")
    Q_INTERFACES(PluginInterface)

public:
    explicit BeaconPlugin(QObject* parent = nullptr);

    QString name()    const override { return QStringLiteral("logos_beacon"); }
    QString version() const override { return QStringLiteral("1.0.0"); }

    Q_INVOKABLE void initLogos(LogosAPI* api);

    // ── Config ───────────────────────────────────────────────────────────────
    // Returns {signingKeyHex, nodeUrl, watchStash:bool, persistencePath, channelLabel}
    Q_INVOKABLE QString getBeaconConfig() const;

    // Persists beacon/nodeUrl. Returns {"ok":true} or {"error":"..."}
    Q_INVOKABLE QString setNodeUrl(const QString& url);

    // Persists beacon/watchStash. Returns {"ok":true}
    Q_INVOKABLE QString setWatchStash(bool enabled);

    // Persists beacon/channelLabel. Returns {"ok":true}
    Q_INVOKABLE QString setChannelLabel(const QString& label);

    // ── State ────────────────────────────────────────────────────────────────
    // Returns {configured:bool, seenCids:N, inscribedCids:N}
    Q_INVOKABLE QString getStatus() const;

    // Returns JSON array [{cid, inscriptionId, label, ts, status}], last 100
    Q_INVOKABLE QString getInscriptionLog() const;

    // ── Inscription ──────────────────────────────────────────────────────────
    // Checks for duplicate, appends pending entry, saves.
    // Returns {"ok":true,"entryIndex":N} | {"ok":true,"duplicate":true} | {"error":"..."}
    Q_INVOKABLE QString pinCid(const QString& cid, const QString& label);

    // Called from QML after zone seq responds. Updates entry status.
    // Returns {"ok":true} or {"error":"..."}
    Q_INVOKABLE QString confirmInscription(int entryIndex,
                                           const QString& inscriptionId,
                                           const QString& status);

    // Called from QML after keycardAuthComplete — sets m_signingKeyHex (32-byte hex).
    // Returns {"ok":true} or {"error":"..."}
    Q_INVOKABLE QString setSigningKey(const QString& hexKey);

    // Called from QML on card removal or auth restart — clears m_signingKeyHex.
    // Returns {"ok":true}
    Q_INVOKABLE QString clearSigningKey();

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);
    void inscriptionConfirmed(int entryIndex, const QString& inscriptionId,
                              const QString& status);

private:
    void     loadLog();
    void     saveLog();

    static QString errorJson(const QString& msg);
    static QString okJson();

    QString     m_persistencePath;
    QString     m_signingKeyHex;
    QJsonArray  m_log;             // in-memory inscription log
};
