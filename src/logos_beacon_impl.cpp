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
#include <QTextStream>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QEventLoop>
#include <QUrl>
#include <QMap>
#include <algorithm>

#include <nlohmann/json.hpp>

// QSettings key prefix (unchanged from the legacy BeaconPlugin).
static constexpr const char* kNodeUrlKey         = "beacon/nodeUrl";
static constexpr const char* kWatchStashKey      = "beacon/watchStash";
static constexpr const char* kChannelLabelKey    = "beacon/channelLabel";
static constexpr const char* kWatchedSourcesKey  = "beacon/watchedSources";
static constexpr const char* kExplorerUrlKey     = "beacon/explorerUrl";
static constexpr const char* kDefaultExplorerUrl = "https://logosblocks.noders.services";

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
    QString explorerBaseUrl() const
    {
        QSettings s;
        QString url = s.value(QLatin1String(kExplorerUrlKey), QLatin1String(kDefaultExplorerUrl)).toString();
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
        o[QStringLiteral("watchStash")]      = s.value(QLatin1String(kWatchStashKey), true).toBool();
        o[QStringLiteral("persistencePath")] = m_persistencePath;
        o[QStringLiteral("channelLabel")]    = s.value(QLatin1String(kChannelLabelKey), QStringLiteral("My Beacon")).toString();
        o[QStringLiteral("explorerUrl")]     = explorerBaseUrl();
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }
    QString setNodeUrl(const QString& url)
    {
        if (url.trimmed().isEmpty()) return errorJson(QStringLiteral("url must not be empty"));
        QSettings s; s.setValue(QLatin1String(kNodeUrlKey), url.trimmed()); return okJson();
    }
    QString setWatchStash(bool enabled) { QSettings s; s.setValue(QLatin1String(kWatchStashKey), enabled); return okJson(); }
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
    QString setChannelLabel(const QString& label) { QSettings s; s.setValue(QLatin1String(kChannelLabelKey), label.trimmed()); return okJson(); }

    QString getStatus() const
    {
        int inscribedCids = 0;
        for (int i = 0; i < m_log.size(); ++i) {
            const QString st = m_log[i].toObject()[QStringLiteral("status")].toString();
            if (st == QLatin1String("ok") || st == QLatin1String("confirmed")) ++inscribedCids;
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

    // confirmInscription returns the entry so the adapter can emit the event.
    QString confirmInscription(int entryIndex, const QString& inscriptionId, const QString& status)
    {
        if (entryIndex < 0 || entryIndex >= m_log.size()) return errorJson(QStringLiteral("entryIndex out of range"));
        QJsonObject entry = m_log[entryIndex].toObject();
        entry[QStringLiteral("inscriptionId")] = inscriptionId; entry[QStringLiteral("status")] = status;
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

    QString findAnchorTx(const QString& nUrl, const QString& channelId, int slotFrom, int slotTo)
    {
        QJsonObject result; result[QStringLiteral("txHash")] = QString();
        QString base = nUrl; if (base.endsWith('/')) base.chop(1);
        QNetworkReply* reply = getSync(QString("%1/cryptarchia/blocks?slot_from=%2&slot_to=%3").arg(base).arg(slotFrom).arg(slotTo));
        if (reply->error() != QNetworkReply::NoError) { reply->deleteLater(); return QJsonDocument(result).toJson(QJsonDocument::Compact); }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll()); reply->deleteLater();
        if (!doc.isArray()) return QJsonDocument(result).toJson(QJsonDocument::Compact);
        for (const QJsonValue& bv : doc.array()) {
            for (const QJsonValue& tv : bv[QStringLiteral("transactions")].toArray()) {
                const QJsonObject mtx = tv[QStringLiteral("mantle_tx")].toObject();
                for (const QJsonValue& ov : mtx[QStringLiteral("ops")].toArray()) {
                    if (ov[QStringLiteral("payload")][QStringLiteral("channel_id")].toString() == channelId) {
                        result[QStringLiteral("txHash")] = mtx[QStringLiteral("hash")].toString();
                        return QJsonDocument(result).toJson(QJsonDocument::Compact);
                    }
                }
            }
        }
        return QJsonDocument(result).toJson(QJsonDocument::Compact);
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

    QString findExplorerTxHash(const QString& channelId, int slotFrom, int slotTo)
    {
        QJsonObject result; result[QStringLiteral("txHash")] = QString(); result[QStringLiteral("blockHash")] = QString(); result[QStringLiteral("found")] = false;
        if (channelId.isEmpty() || slotFrom <= 0) return QJsonDocument(result).toJson(QJsonDocument::Compact);
        QNetworkReply* blocksReply = getSync(QString("%1/cryptarchia/blocks?slot_from=%2&slot_to=%3").arg(nodeUrl()).arg(slotFrom).arg(slotTo));
        if (blocksReply->error() != QNetworkReply::NoError) { blocksReply->deleteLater(); return QJsonDocument(result).toJson(QJsonDocument::Compact); }
        QJsonDocument blocksDoc = QJsonDocument::fromJson(blocksReply->readAll()); blocksReply->deleteLater();
        if (!blocksDoc.isArray()) return QJsonDocument(result).toJson(QJsonDocument::Compact);
        QString blockHeaderId; int txIndex = -1;
        for (const QJsonValue& bv : blocksDoc.array()) {
            const QJsonObject block = bv.toObject();
            const QString bid = block[QStringLiteral("header")][QStringLiteral("id")].toString();
            const QJsonArray txs = block[QStringLiteral("transactions")].toArray();
            for (int ti = 0; ti < txs.size(); ++ti) {
                const QJsonArray ops = txs[ti][QStringLiteral("mantle_tx")][QStringLiteral("ops")].toArray();
                for (const QJsonValue& ov : ops) {
                    if (ov[QStringLiteral("payload")][QStringLiteral("channel_id")].toString() == channelId) { blockHeaderId = bid; txIndex = ti; goto found_block; }
                }
            }
        }
        return QJsonDocument(result).toJson(QJsonDocument::Compact);
    found_block:
        QNetworkReply* explorerReply = getSync(QString("%1/api/blocks/%2").arg(explorerBaseUrl(), blockHeaderId));
        QString txHash;
        if (explorerReply->error() == QNetworkReply::NoError) {
            QJsonDocument ed = QJsonDocument::fromJson(explorerReply->readAll());
            for (const QJsonValue& tv : ed[QStringLiteral("transactions")].toArray()) {
                if (tv[QStringLiteral("index_in_block")].toInt(-1) == txIndex) { txHash = tv[QStringLiteral("tx_hash")].toString(); break; }
            }
        }
        explorerReply->deleteLater();
        if (!txHash.isEmpty()) { result[QStringLiteral("txHash")] = txHash; result[QStringLiteral("blockHash")] = blockHeaderId; result[QStringLiteral("found")] = true; }
        else { result[QStringLiteral("txHash")] = blockHeaderId; result[QStringLiteral("blockHash")] = blockHeaderId; result[QStringLiteral("found")] = true; }
        return QJsonDocument(result).toJson(QJsonDocument::Compact);
    }

    QString getBlockForTx(const QString& txHash, int slotFrom)
    {
        QJsonObject result; result[QStringLiteral("blockHash")] = QString(); result[QStringLiteral("found")] = false;
        if (txHash.isEmpty()) return QJsonDocument(result).toJson(QJsonDocument::Compact);
        QNetworkReply* infoReply = getSync(nodeUrl() + QStringLiteral("/cryptarchia/info"));
        if (infoReply->error() != QNetworkReply::NoError) { infoReply->deleteLater(); return QJsonDocument(result).toJson(QJsonDocument::Compact); }
        QJsonDocument infoDoc = QJsonDocument::fromJson(infoReply->readAll()); infoReply->deleteLater();
        const QJsonObject infoObj = infoDoc.object();
        const QJsonObject ciScan = infoObj.contains(QStringLiteral("cryptarchia_info")) ? infoObj.value(QStringLiteral("cryptarchia_info")).toObject() : infoObj;
        int libSlot = ciScan[QStringLiteral("lib_slot")].toInt(0);
        if (libSlot <= 0) return QJsonDocument(result).toJson(QJsonDocument::Compact);
        int scanFrom = (slotFrom > 0) ? slotFrom : qMax(0, libSlot - 5000);
        QNetworkReply* blocksReply = getSync(QString("%1/cryptarchia/blocks?slot_from=%2&slot_to=%3").arg(nodeUrl()).arg(scanFrom).arg(libSlot));
        if (blocksReply->error() != QNetworkReply::NoError) { blocksReply->deleteLater(); return QJsonDocument(result).toJson(QJsonDocument::Compact); }
        QJsonDocument blocksDoc = QJsonDocument::fromJson(blocksReply->readAll()); blocksReply->deleteLater();
        if (!blocksDoc.isArray()) return QJsonDocument(result).toJson(QJsonDocument::Compact);
        for (const QJsonValue& bv : blocksDoc.array()) {
            const QJsonObject block = bv.toObject();
            const QString headerId = block[QStringLiteral("header")][QStringLiteral("id")].toString();
            for (const QJsonValue& tv : block[QStringLiteral("transactions")].toArray()) {
                if (tv[QStringLiteral("mantle_tx")][QStringLiteral("hash")].toString() == txHash) {
                    result[QStringLiteral("blockHash")] = headerId; result[QStringLiteral("found")] = true;
                    return QJsonDocument(result).toJson(QJsonDocument::Compact);
                }
            }
        }
        return QJsonDocument(result).toJson(QJsonDocument::Compact);
    }

    QString diagLog(const QString& msg)
    {
        QFile f(QStringLiteral("/tmp/beacon_plugin.diag"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Append)) { QTextStream ts(&f); ts << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << " " << msg << "\n"; }
        return okJson();
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
StdLogosResult LogosBeaconImpl::setWatchStash(const std::string& enabled) { return adapt(d->setWatchStash(enabled == "true" || enabled == "1")); }
StdLogosResult LogosBeaconImpl::setWatchedSources(const std::string& s)   { return adapt(d->setWatchedSources(qs(s))); }
StdLogosResult LogosBeaconImpl::getWatchedSources()                      { return adapt(d->getWatchedSources()); }
StdLogosResult LogosBeaconImpl::setChannelLabel(const std::string& label) { return adapt(d->setChannelLabel(qs(label))); }

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
    StdLogosResult r = adapt(d->confirmInscription(int(entryIndex), qs(inscriptionId), qs(status)));
    if (r.success) inscriptionConfirmed(entryIndex, inscriptionId, status);
    return r;
}

// ── Key management ───────────────────────────────────────────────────────────────
StdLogosResult LogosBeaconImpl::setSigningKey(const std::string& hexKey) { return adapt(d->setSigningKey(qs(hexKey))); }
StdLogosResult LogosBeaconImpl::clearSigningKey()       { return adapt(d->clearSigningKey()); }

// ── Manifest log ─────────────────────────────────────────────────────────────────
StdLogosResult LogosBeaconImpl::getManifestLog()        { return adapt(d->getManifestLog()); }
StdLogosResult LogosBeaconImpl::recordManifest(const std::string& m) { return adapt(d->recordManifest(qs(m))); }

// ── Finalization / explorer lookups ──────────────────────────────────────────────
StdLogosResult LogosBeaconImpl::findAnchorTx(const std::string& nodeUrl, const std::string& channelId, int64_t slotFrom, int64_t slotTo)
{ return adapt(d->findAnchorTx(qs(nodeUrl), qs(channelId), int(slotFrom), int(slotTo))); }
StdLogosResult LogosBeaconImpl::getChannelState(const std::string& channelId)
{ return adapt(d->getChannelState(qs(channelId))); }
StdLogosResult LogosBeaconImpl::findExplorerTxHash(const std::string& channelId, int64_t slotFrom, int64_t slotTo)
{ return adapt(d->findExplorerTxHash(qs(channelId), int(slotFrom), int(slotTo))); }
StdLogosResult LogosBeaconImpl::getBlockForTx(const std::string& txHash, int64_t slotFrom)
{ return adapt(d->getBlockForTx(qs(txHash), int(slotFrom))); }

// ── Debug ─────────────────────────────────────────────────────────────────────────
StdLogosResult LogosBeaconImpl::diagLog(const std::string& msg) { return adapt(d->diagLog(qs(msg))); }

// ── zone_sequencer bridge (typed modules(), no getClient) ───────────────────────
StdLogosResult LogosBeaconImpl::seqDeriveChannel(const std::string& signingKeyHex)
{
    return modules().zone_sequencer.derive_channel_id(signingKeyHex);
}
StdLogosResult LogosBeaconImpl::seqPublish(const std::string& nodeUrl, const std::string& channelId,
                                           const std::string& signingKeyHex, const std::string& checkpointPath,
                                           const std::string& payload)
{
    // modules() reaches the one persistent zone_sequencer host, so node_url set
    // here is visible to publish_to. Stateless publish_to carries channel+key.
    modules().zone_sequencer.set_node_url(nodeUrl);
    return modules().zone_sequencer.publish_to(channelId, signingKeyHex, checkpointPath, payload);
}
