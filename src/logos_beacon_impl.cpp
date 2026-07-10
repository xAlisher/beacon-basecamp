#include "logos_beacon_impl.h"

#include "logos_sdk.h"   // generated: modules().zone_sequencer (core-context typed)

#include <QSettings>
#include <QString>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QEventLoop>
#include <QUrl>
#include <QMap>
#include <algorithm>

#include <nlohmann/json.hpp>

#include <openssl/evp.h>   // local Ed25519 channel-id derivation (beacon#50)

// QSettings key prefix (unchanged from the legacy BeaconPlugin).
static constexpr const char* kNodeUrlKey         = "beacon/nodeUrl";
static constexpr const char* kWatchedSourcesKey  = "beacon/watchedSources";

// ── pimpl: the Qt internals, preserved from the legacy BeaconPlugin ─────────────
struct LogosBeaconImpl::Impl
{
    QString     m_persistencePath;
    QString     m_signingKeyHex;
    QJsonArray  m_log;
    QStringList m_watchedSources;
    QNetworkAccessManager m_nam;   // Qt6::Network is linked by logos_module()

    Impl()
    {
        // Universal modules don't get the legacy instancePersistencePath property;
        // fall back to the canonical module_data path (same default as before).
        m_persistencePath = QDir::homePath() +
            QStringLiteral("/.local/share/Logos/LogosBasecamp/module_data/logos_beacon");
        QDir().mkpath(m_persistencePath);
        QSettings s;
        m_watchedSources = s.value(QLatin1String(kWatchedSourcesKey)).toStringList();
        loadLog();
    }

    static QString errorJson(const QString& msg)
    { QJsonObject o; o[QStringLiteral("error")] = msg; return QJsonDocument(o).toJson(QJsonDocument::Compact); }
    static QString okJson()
    { QJsonObject o; o[QStringLiteral("ok")] = true; return QJsonDocument(o).toJson(QJsonDocument::Compact); }

    QString nodeUrl() const
    {
        QSettings s;
        QString url = s.value(QLatin1String(kNodeUrlKey), QStringLiteral("http://127.0.0.1:8080")).toString();
        if (url.endsWith('/')) url.chop(1);
        return url;
    }

    void loadLog()
    {
        if (m_persistencePath.isEmpty()) return;
        QFile f(m_persistencePath + QStringLiteral("/inscription-log.json"));
        if (!f.open(QIODevice::ReadOnly)) return;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isArray()) m_log = doc.array();
    }
    void saveLog()
    {
        if (m_persistencePath.isEmpty()) return;
        QFile f(m_persistencePath + QStringLiteral("/inscription-log.json"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QJsonDocument(m_log).toJson(QJsonDocument::Compact));
    }

    // Synchronous GET helper (QEventLoop, as before).
    QNetworkReply* getSync(const QString& url)
    {
        QNetworkReply* r = m_nam.get(QNetworkRequest{QUrl(url)});
        QEventLoop loop; QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit); loop.exec();
        return r;
    }

    QString getBeaconConfig() const
    {
        QSettings s; QJsonObject o;
        o[QStringLiteral("signingKeyHex")]   = m_signingKeyHex;
        o[QStringLiteral("nodeUrl")]         = s.value(QLatin1String(kNodeUrlKey), QStringLiteral("http://127.0.0.1:8080")).toString();
        o[QStringLiteral("persistencePath")] = m_persistencePath;
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }
    QString setNodeUrl(const QString& url)
    {
        if (url.trimmed().isEmpty()) return errorJson(QStringLiteral("url must not be empty"));
        QSettings s; s.setValue(QLatin1String(kNodeUrlKey), url.trimmed()); return okJson();
    }
    QString setWatchedSources(const QString& newlineSeparated)
    {
        m_watchedSources.clear();
        for (const QString& line : newlineSeparated.split(QLatin1Char('\n'))) {
            const QString t = line.trimmed(); if (!t.isEmpty()) m_watchedSources.append(t);
        }
        QSettings s; s.setValue(QLatin1String(kWatchedSourcesKey), m_watchedSources); return okJson();
    }
    QString getWatchedSources() const
    {
        QJsonArray arr; for (const QString& src : m_watchedSources) arr.append(src);
        QJsonObject o; o[QStringLiteral("sources")] = arr; return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }

    QString getStatus() const
    {
        int inscribedCids = 0;
        for (int i = 0; i < m_log.size(); ++i) {
            const QString st = m_log[i].toObject()[QStringLiteral("status")].toString();
            if (st == QLatin1String("confirmed")) ++inscribedCids;
        }
        QJsonObject o;
        o[QStringLiteral("configured")]    = !m_signingKeyHex.isEmpty();
        o[QStringLiteral("seenCids")]      = m_log.size();
        o[QStringLiteral("inscribedCids")] = inscribedCids;
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }
    QString getInscriptionLog() const
    {
        if (m_log.size() <= 100) return QJsonDocument(m_log).toJson(QJsonDocument::Compact);
        QJsonArray tail; for (int i = m_log.size() - 100; i < m_log.size(); ++i) tail.append(m_log[i]);
        return QJsonDocument(tail).toJson(QJsonDocument::Compact);
    }
    QString clearInscriptionLog() { m_log = QJsonArray(); saveLog(); return okJson(); }

    QString getNodeInfo()
    {
        QNetworkReply* reply = getSync(nodeUrl() + QStringLiteral("/cryptarchia/info"));
        if (reply->error() != QNetworkReply::NoError) { const QString e = reply->errorString(); reply->deleteLater(); return errorJson(e); }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll()); reply->deleteLater();
        if (!doc.isObject()) return errorJson(QStringLiteral("unexpected node response"));
        QJsonObject src = doc.object(), o;
        const QJsonObject ci = src.contains(QStringLiteral("cryptarchia_info"))
                                   ? src.value(QStringLiteral("cryptarchia_info")).toObject() : src;
        o[QStringLiteral("slot")]     = ci[QStringLiteral("slot")];
        o[QStringLiteral("lib_slot")] = ci[QStringLiteral("lib_slot")];
        o[QStringLiteral("mode")]     = src[QStringLiteral("mode")];
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }

    QString pinCid(const QString& cid, const QString& label, const QString& source, int slotFrom, int libAtSubmit)
    {
        if (cid.trimmed().isEmpty()) return errorJson(QStringLiteral("cid must not be empty"));
        for (int i = 0; i < m_log.size(); ++i) {
            QJsonObject e = m_log[i].toObject();
            if (e[QStringLiteral("cid")].toString() == cid) {
                if (!e[QStringLiteral("inscriptionId")].toString().isEmpty()) {
                    QJsonObject o; o[QStringLiteral("ok")] = true; o[QStringLiteral("duplicate")] = true;
                    return QJsonDocument(o).toJson(QJsonDocument::Compact);
                }
                m_log.removeAt(i); saveLog(); break;
            }
        }
        QJsonObject entry;
        entry[QStringLiteral("cid")] = cid; entry[QStringLiteral("label")] = label; entry[QStringLiteral("source")] = source;
        entry[QStringLiteral("ts")] = QDateTime::currentSecsSinceEpoch(); entry[QStringLiteral("status")] = QStringLiteral("queued");
        entry[QStringLiteral("inscriptionId")] = QString(); entry[QStringLiteral("slotFrom")] = slotFrom; entry[QStringLiteral("libAtSubmit")] = libAtSubmit;
        int idx = m_log.size(); m_log.append(entry); saveLog();
        QJsonObject o; o[QStringLiteral("ok")] = true; o[QStringLiteral("entryIndex")] = idx;
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }

    QString deriveModuleSigningKey(const QString& moduleName)
    {
        if (m_signingKeyHex.isEmpty()) return errorJson(QStringLiteral("key not set"));
        QByteArray derived = QCryptographicHash::hash(
            QByteArray::fromHex(m_signingKeyHex.toUtf8()) + moduleName.toUtf8(), QCryptographicHash::Sha256);
        QJsonObject o; o[QStringLiteral("signingKey")] = QString::fromLatin1(derived.toHex());
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }

    QString getModules()
    {
        QMap<QString, QJsonObject> groups;
        for (int i = 0; i < m_log.size(); ++i) {
            QJsonObject e = m_log[i].toObject(); QString src = e[QStringLiteral("source")].toString();
            if (!groups.contains(src)) { QJsonObject g; g[QStringLiteral("name")] = src; g[QStringLiteral("cidCount")] = 0; g[QStringLiteral("lastTs")] = qint64(0); groups[src] = g; }
            QJsonObject g = groups[src];
            g[QStringLiteral("cidCount")] = g[QStringLiteral("cidCount")].toInt() + 1;
            qint64 ts = e[QStringLiteral("ts")].toVariant().toLongLong();
            if (ts > g[QStringLiteral("lastTs")].toVariant().toLongLong()) g[QStringLiteral("lastTs")] = ts;
            groups[src] = g;
        }
        QList<QJsonObject> list = groups.values();
        std::sort(list.begin(), list.end(), [](const QJsonObject& a, const QJsonObject& b) {
            return a[QStringLiteral("lastTs")].toVariant().toLongLong() > b[QStringLiteral("lastTs")].toVariant().toLongLong(); });
        QJsonArray arr; for (const auto& g : list) arr.append(g);
        return QJsonDocument(arr).toJson(QJsonDocument::Compact);
    }

    QString ensureCheckpointsDir()
    {
        if (m_persistencePath.isEmpty()) return errorJson(QStringLiteral("persistence path not set"));
        if (QDir(m_persistencePath + QStringLiteral("/checkpoints")).mkpath(QStringLiteral("."))) return okJson();
        return errorJson(QStringLiteral("failed to create checkpoints dir"));
    }

    QString confirmInscription(int entryIndex, const QString& inscriptionId, const QString& status)
    {
        if (entryIndex < 0 || entryIndex >= m_log.size()) return errorJson(QStringLiteral("entryIndex out of range"));
        QJsonObject entry = m_log[entryIndex].toObject();
        // Preserve a real per-item inscription id: a later status-only update (e.g.
        // "finalizing") passes "" and must NOT wipe the tx hash captured at submit
        // (beacon#50 / #43 regression — ids used to collapse to the shared channel tip).
        if (!inscriptionId.isEmpty())
            entry[QStringLiteral("inscriptionId")] = inscriptionId;
        entry[QStringLiteral("status")] = status;
        m_log[entryIndex] = entry; saveLog();
        return okJson();
    }

    QString setSigningKey(const QString& hexKey)
    {
        if (hexKey.length() != 64 || QByteArray::fromHex(hexKey.toUtf8()).size() != 32)
            return errorJson(QStringLiteral("hexKey must be 64 hex chars (32 bytes)"));
        m_signingKeyHex = hexKey; return okJson();
    }
    QString clearSigningKey() { m_signingKeyHex.clear(); return okJson(); }

    QString getManifestLog() const
    {
        if (m_persistencePath.isEmpty()) return QJsonDocument(QJsonArray()).toJson(QJsonDocument::Compact);
        QFile f(m_persistencePath + QStringLiteral("/manifest-log.json"));
        if (!f.open(QIODevice::ReadOnly)) return QJsonDocument(QJsonArray()).toJson(QJsonDocument::Compact);
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        return doc.isArray() ? QJsonDocument(doc.array()).toJson(QJsonDocument::Compact) : QJsonDocument(QJsonArray()).toJson(QJsonDocument::Compact);
    }
    QString recordManifest(const QString& moduleName)
    {
        if (m_persistencePath.isEmpty()) return errorJson(QStringLiteral("persistence path not set"));
        QString path = m_persistencePath + QStringLiteral("/manifest-log.json");
        QJsonArray arr;
        { QFile rf(path); if (rf.open(QIODevice::ReadOnly)) { QJsonDocument doc = QJsonDocument::fromJson(rf.readAll()); if (doc.isArray()) arr = doc.array(); } }
        for (int i = 0; i < arr.size(); ++i) if (arr[i].toString() == moduleName) return okJson();
        arr.append(moduleName);
        QFile wf(path);
        if (!wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) return errorJson(QStringLiteral("failed to write manifest-log.json"));
        wf.write(QJsonDocument(arr).toJson(QJsonDocument::Compact)); return okJson();
    }

    // v0.2 confirmation path (beacon#43). GET /channel/{id} returns 500 ("no state")
    // until an op lands, then 200 with tip_slot/tip_message — the "safe" state (in an
    // L1 block). Callers finalize when tip_slot <= lib_slot. Replaces the
    // /cryptarchia/blocks scan, which returns [] on v0.2 even for slots holding our op.
    QString getChannelState(const QString& channelId)
    {
        QJsonObject result;
        result[QStringLiteral("found")]      = false;
        result[QStringLiteral("tipSlot")]    = -1;
        result[QStringLiteral("tipMessage")] = QString();
        if (channelId.isEmpty()) return QJsonDocument(result).toJson(QJsonDocument::Compact);
        QNetworkReply* reply = getSync(QString("%1/channel/%2").arg(nodeUrl(), channelId));
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (http != 200) return QJsonDocument(result).toJson(QJsonDocument::Compact);  // 500 = nothing landed yet
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        if (!o.contains(QStringLiteral("tip_slot"))) return QJsonDocument(result).toJson(QJsonDocument::Compact);
        result[QStringLiteral("found")]      = true;
        result[QStringLiteral("tipSlot")]    = o.value(QStringLiteral("tip_slot")).toInt();
        result[QStringLiteral("tipMessage")] = o.value(QStringLiteral("tip_message")).toString();
        return QJsonDocument(result).toJson(QJsonDocument::Compact);
    }
};

// ── Adapter: QString-JSON → StdLogosResult ──────────────────────────────────────
static StdLogosResult adapt(const QString& jsonStr)
{
    auto j = nlohmann::json::parse(jsonStr.toStdString(), nullptr, false);
    if (j.is_discarded()) return {false, {}, "invalid module response"};
    if (j.is_object() && j.contains("error"))
        return {false, {}, j["error"].get<std::string>()};
    return {true, j};
}
static QString qs(const std::string& s) { return QString::fromStdString(s); }

// ── ctor/dtor ───────────────────────────────────────────────────────────────────
LogosBeaconImpl::LogosBeaconImpl() : d(std::make_unique<Impl>()) {}
LogosBeaconImpl::~LogosBeaconImpl() = default;

// ── Config ──────────────────────────────────────────────────────────────────────
StdLogosResult LogosBeaconImpl::getBeaconConfig()                         { return adapt(d->getBeaconConfig()); }
StdLogosResult LogosBeaconImpl::setNodeUrl(const std::string& url)        { return adapt(d->setNodeUrl(qs(url))); }
StdLogosResult LogosBeaconImpl::setWatchedSources(const std::string& s)   { return adapt(d->setWatchedSources(qs(s))); }
StdLogosResult LogosBeaconImpl::getWatchedSources()                      { return adapt(d->getWatchedSources()); }

// ── State ─────────────────────────────────────────────────────────────────────
StdLogosResult LogosBeaconImpl::getStatus()             { return adapt(d->getStatus()); }
StdLogosResult LogosBeaconImpl::getInscriptionLog()     { return adapt(d->getInscriptionLog()); }
StdLogosResult LogosBeaconImpl::clearInscriptionLog()   { return adapt(d->clearInscriptionLog()); }

// ── Node info ───────────────────────────────────────────────────────────────────
StdLogosResult LogosBeaconImpl::getNodeInfo()           { return adapt(d->getNodeInfo()); }

// ── Inscription bookkeeping ──────────────────────────────────────────────────────
StdLogosResult LogosBeaconImpl::pinCid(const std::string& cid, const std::string& label,
                                       const std::string& source, int64_t slotFrom, int64_t libAtSubmit)
{ return adapt(d->pinCid(qs(cid), qs(label), qs(source), int(slotFrom), int(libAtSubmit))); }

StdLogosResult LogosBeaconImpl::deriveModuleSigningKey(const std::string& m) { return adapt(d->deriveModuleSigningKey(qs(m))); }
StdLogosResult LogosBeaconImpl::getModules()            { return adapt(d->getModules()); }
StdLogosResult LogosBeaconImpl::ensureCheckpointsDir()  { return adapt(d->ensureCheckpointsDir()); }

StdLogosResult LogosBeaconImpl::confirmInscription(int64_t entryIndex, const std::string& inscriptionId, const std::string& status)
{
    return adapt(d->confirmInscription(int(entryIndex), qs(inscriptionId), qs(status)));
}

// ── Key management ───────────────────────────────────────────────────────────────
StdLogosResult LogosBeaconImpl::setSigningKey(const std::string& hexKey) { return adapt(d->setSigningKey(qs(hexKey))); }
StdLogosResult LogosBeaconImpl::clearSigningKey()       { return adapt(d->clearSigningKey()); }

// ── Manifest log ─────────────────────────────────────────────────────────────────
StdLogosResult LogosBeaconImpl::getManifestLog()        { return adapt(d->getManifestLog()); }
StdLogosResult LogosBeaconImpl::recordManifest(const std::string& m) { return adapt(d->recordManifest(qs(m))); }

// ── Finalization / explorer lookups ──────────────────────────────────────────────
StdLogosResult LogosBeaconImpl::getChannelState(const std::string& channelId)
{ return adapt(d->getChannelState(qs(channelId))); }

// ── zone_sequencer bridge (typed modules(), no getClient) ───────────────────────
// LOCAL channel-id derivation (beacon#50). The channel id is the Ed25519 PUBLIC key of
// the 32-byte signing-key seed (RFC 8032 — byte-identical to zone_sequencer's
// ed25519-dalek `SigningKey::from_bytes(seed).verifying_key()`, verified vs the RFC 8032
// test vector). It is a PUBLIC value and beacon already holds the seed, so there is no
// reason to cross a module boundary for it. The old sync modules().zone_sequencer
// .derive_channel_id spun a QtRO `waitForSource` nested event loop; a reentrant
// `onClientRead` during that loop dereferenced a stale QMetaObject → SIGSEGV. Computing
// it in-process removes the IPC entirely, so that crash path no longer exists.
static std::string deriveChannelIdLocal(const std::string& signingKeyHex)
{
    // Strict: exactly 64 hex chars (32-byte seed) — matches zone_sequencer's hex::decode,
    // which rejects odd/short input. (QByteArray::fromHex would otherwise pad an odd nibble.)
    if (signingKeyHex.size() != 64) return std::string();
    QByteArray seed = QByteArray::fromHex(QByteArray::fromStdString(signingKeyHex));
    if (seed.size() != 32) return std::string();   // rejects non-hex chars within the 64
    EVP_PKEY* pk = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char*>(seed.constData()), 32);
    if (!pk) return std::string();
    unsigned char pub[32];
    size_t publen = sizeof(pub);
    const int ok = EVP_PKEY_get_raw_public_key(pk, pub, &publen);
    EVP_PKEY_free(pk);
    if (ok != 1 || publen != 32) return std::string();
    return QByteArray(reinterpret_cast<const char*>(pub), 32).toHex().toStdString();
}

StdLogosResult LogosBeaconImpl::seqDeriveChannel(const std::string& signingKeyHex)
{
    const std::string ch = deriveChannelIdLocal(signingKeyHex);
    if (ch.empty()) return {false, {}, "invalid signing key (expected 64-hex / 32-byte seed)"};
    return {true, ch, {}};
}
StdLogosResult LogosBeaconImpl::getSourceChannel(const std::string& source)
{
    // per-module signing key (SHA256(masterKey + source)), then the channel id = its Ed25519 pubkey (local)
    StdLogosResult sk = adapt(d->deriveModuleSigningKey(qs(source)));
    if (!sk.success) return sk;
    const std::string keyHex = sk.value.value("signingKey", std::string());
    if (keyHex.empty()) return {false, {}, "no signing key"};
    const std::string ch = deriveChannelIdLocal(keyHex);
    if (ch.empty()) return {false, {}, "channel derivation failed"};
    return {true, ch, {}};
}
StdLogosResult LogosBeaconImpl::seqPublish(int64_t token, const std::string& nodeUrl, const std::string& channelId,
                                           const std::string& signingKeyHex, const std::string& checkpointPath,
                                           const std::string& payload)
{
    // modules() reaches the one persistent zone_sequencer host, so node_url set
    // here is visible to publish_to. Stateless publish_to carries channel+key.
    // A fresh-channel publish on a long chain can block here for minutes; the
    // ui-host backend calls this via seqPublishAsync fire-and-forget, so blocking
    // this core handler is safe (it does not park the ui-host loop). The result is
    // delivered by the publishCompleted event — NOT the sync reply, which the async
    // caller does not (and must not) wait on (beacon#50).
    modules().zone_sequencer.set_node_url(nodeUrl);
    // NOTE (beacon#50): do NOT emit a logos_events event (e.g. publishCompleted) from
    // inside this IPC-handler stack — doing so SIGSEGVs logos_beacon at publish-return
    // (proven by A/B: same slow publish returns cleanly with the emit removed). The
    // result is delivered to QML from beacon_ui's async reply callback instead, which
    // is safe now that the request context lives long enough (Timeout=15min) for the
    // reply to land. `token` is unused core-side (correlation happens in beacon_ui).
    (void)token;
    return modules().zone_sequencer.publish_to(channelId, signingKeyHex, checkpointPath, payload);
}
