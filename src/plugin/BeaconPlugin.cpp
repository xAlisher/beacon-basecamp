#include "BeaconPlugin.h"

#include <QSettings>
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
#include <algorithm>

// ── QSettings key prefix ──────────────────────────────────────────────────────
static constexpr const char* kNodeUrlKey        = "beacon/nodeUrl";
static constexpr const char* kWatchStashKey     = "beacon/watchStash";
static constexpr const char* kChannelLabelKey   = "beacon/channelLabel";
static constexpr const char* kWatchedSourcesKey = "beacon/watchedSources";
static constexpr const char* kExplorerUrlKey    = "beacon/explorerUrl";
static constexpr const char* kDefaultExplorerUrl = "https://testnet.blockchain.logos.co";

// ── Helpers ───────────────────────────────────────────────────────────────────
QString BeaconPlugin::errorJson(const QString& msg)
{
    QJsonObject o; o[QStringLiteral("error")] = msg;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString BeaconPlugin::okJson()
{
    QJsonObject o; o[QStringLiteral("ok")] = true;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString BeaconPlugin::nodeUrl() const
{
    QSettings s;
    QString url = s.value(QLatin1String(kNodeUrlKey),
                          QStringLiteral("http://127.0.0.1:8080")).toString();
    if (url.endsWith('/')) url.chop(1);
    return url;
}

QString BeaconPlugin::explorerBaseUrl() const
{
    QSettings s;
    QString url = s.value(QLatin1String(kExplorerUrlKey),
                          QLatin1String(kDefaultExplorerUrl)).toString();
    if (url.endsWith('/')) url.chop(1);
    return url;
}

// ── Constructor ───────────────────────────────────────────────────────────────
BeaconPlugin::BeaconPlugin(QObject* parent) : QObject(parent) {}

// ── initLogos ─────────────────────────────────────────────────────────────────
void BeaconPlugin::initLogos(LogosAPI* api)
{
    logosAPI = api;
    QVariant prop = property("instancePersistencePath");
    if (prop.isValid() && !prop.toString().isEmpty()) {
        m_persistencePath = prop.toString();
    } else {
        m_persistencePath = QDir::homePath() +
            QStringLiteral("/.local/share/Logos/LogosBasecamp/module_data/logos_beacon");
    }
    QDir().mkpath(m_persistencePath);
    QSettings s;
    m_watchedSources = s.value(QLatin1String(kWatchedSourcesKey)).toStringList();
    loadLog();
}

// ── setSigningKey / clearSigningKey ───────────────────────────────────────────
QString BeaconPlugin::setSigningKey(const QString& hexKey)
{
    if (hexKey.length() != 64 || QByteArray::fromHex(hexKey.toUtf8()).size() != 32)
        return errorJson(QStringLiteral("hexKey must be 64 hex chars (32 bytes)"));
    m_signingKeyHex = hexKey;
    return okJson();
}

QString BeaconPlugin::clearSigningKey()
{
    m_signingKeyHex.clear();
    return okJson();
}

// ── diagLog ───────────────────────────────────────────────────────────────────
QString BeaconPlugin::diagLog(const QString& msg)
{
    QFile f(QStringLiteral("/tmp/beacon_plugin.diag"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << " " << msg << "\n";
    }
    return okJson();
}

// ── clearInscriptionLog ───────────────────────────────────────────────────────
QString BeaconPlugin::clearInscriptionLog()
{
    m_log = QJsonArray();
    saveLog();
    return okJson();
}

// ── getBeaconConfig ───────────────────────────────────────────────────────────
QString BeaconPlugin::getBeaconConfig() const
{
    QSettings s;
    QJsonObject o;
    o[QStringLiteral("signingKeyHex")]   = m_signingKeyHex;
    o[QStringLiteral("nodeUrl")]         = s.value(QLatin1String(kNodeUrlKey),
                                                    QStringLiteral("http://127.0.0.1:8080")).toString();
    o[QStringLiteral("watchStash")]      = s.value(QLatin1String(kWatchStashKey), true).toBool();
    o[QStringLiteral("persistencePath")] = m_persistencePath;
    o[QStringLiteral("channelLabel")]    = s.value(QLatin1String(kChannelLabelKey),
                                                    QStringLiteral("My Beacon")).toString();
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── setNodeUrl / setWatchStash / setWatchedSources / getWatchedSources / setChannelLabel ──
QString BeaconPlugin::setNodeUrl(const QString& url)
{
    if (url.trimmed().isEmpty())
        return errorJson(QStringLiteral("url must not be empty"));
    QSettings s;
    s.setValue(QLatin1String(kNodeUrlKey), url.trimmed());
    return okJson();
}

QString BeaconPlugin::setWatchStash(bool enabled)
{
    QSettings s; s.setValue(QLatin1String(kWatchStashKey), enabled); return okJson();
}

QString BeaconPlugin::setWatchedSources(const QString& newlineSeparated)
{
    m_watchedSources.clear();
    for (const QString& line : newlineSeparated.split(QLatin1Char('\n'))) {
        const QString t = line.trimmed();
        if (!t.isEmpty()) m_watchedSources.append(t);
    }
    QSettings s; s.setValue(QLatin1String(kWatchedSourcesKey), m_watchedSources);
    return okJson();
}

QString BeaconPlugin::getWatchedSources() const
{
    QJsonArray arr;
    for (const QString& src : m_watchedSources) arr.append(src);
    QJsonObject o; o[QStringLiteral("sources")] = arr;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString BeaconPlugin::setChannelLabel(const QString& label)
{
    QSettings s; s.setValue(QLatin1String(kChannelLabelKey), label.trimmed()); return okJson();
}

// ── getStatus ─────────────────────────────────────────────────────────────────
QString BeaconPlugin::getStatus() const
{
    int inscribedCids = 0;
    for (int i = 0; i < m_log.size(); ++i) {
        const QString st = m_log[i].toObject()[QStringLiteral("status")].toString();
        if (st == QLatin1String("ok") || st == QLatin1String("confirmed"))
            ++inscribedCids;
    }
    QJsonObject o;
    o[QStringLiteral("configured")]    = !m_signingKeyHex.isEmpty();
    o[QStringLiteral("seenCids")]      = m_log.size();
    o[QStringLiteral("inscribedCids")] = inscribedCids;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── loadLog / saveLog / getInscriptionLog ─────────────────────────────────────
void BeaconPlugin::loadLog()
{
    if (m_persistencePath.isEmpty()) return;
    QFile f(m_persistencePath + QStringLiteral("/inscription-log.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isArray()) m_log = doc.array();
}

void BeaconPlugin::saveLog()
{
    if (m_persistencePath.isEmpty()) return;
    QFile f(m_persistencePath + QStringLiteral("/inscription-log.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(m_log).toJson(QJsonDocument::Compact));
}

QString BeaconPlugin::getInscriptionLog() const
{
    if (m_log.size() <= 100)
        return QJsonDocument(m_log).toJson(QJsonDocument::Compact);
    QJsonArray tail;
    for (int i = m_log.size() - 100; i < m_log.size(); ++i) tail.append(m_log[i]);
    return QJsonDocument(tail).toJson(QJsonDocument::Compact);
}

// ── getNodeInfo ───────────────────────────────────────────────────────────────
QString BeaconPlugin::getNodeInfo()
{
    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    QNetworkReply* reply = m_nam->get(QNetworkRequest{QUrl(nodeUrl() + QStringLiteral("/cryptarchia/info"))});
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError) {
        const QString err = reply->errorString(); reply->deleteLater();
        return errorJson(err);
    }
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();
    if (!doc.isObject()) return errorJson(QStringLiteral("unexpected node response"));
    QJsonObject src = doc.object(), o;
    o[QStringLiteral("slot")]     = src[QStringLiteral("slot")];
    o[QStringLiteral("lib_slot")] = src[QStringLiteral("lib_slot")];
    o[QStringLiteral("mode")]     = src[QStringLiteral("mode")];
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── pinCid ────────────────────────────────────────────────────────────────────
QString BeaconPlugin::pinCid(const QString& cid, const QString& label,
                              const QString& source, int slotFrom, int libAtSubmit)
{
    if (cid.trimmed().isEmpty())
        return errorJson(QStringLiteral("cid must not be empty"));

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
    entry[QStringLiteral("cid")]           = cid;
    entry[QStringLiteral("label")]         = label;
    entry[QStringLiteral("source")]        = source;
    entry[QStringLiteral("ts")]            = QDateTime::currentSecsSinceEpoch();
    entry[QStringLiteral("status")]        = QStringLiteral("queued");
    entry[QStringLiteral("inscriptionId")] = QString();
    entry[QStringLiteral("slotFrom")]      = slotFrom;
    entry[QStringLiteral("libAtSubmit")]   = libAtSubmit;

    int idx = m_log.size();
    m_log.append(entry);
    saveLog();

    QJsonObject o; o[QStringLiteral("ok")] = true; o[QStringLiteral("entryIndex")] = idx;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── deriveModuleSigningKey ────────────────────────────────────────────────────
QString BeaconPlugin::deriveModuleSigningKey(const QString& moduleName)
{
    if (m_signingKeyHex.isEmpty()) return errorJson(QStringLiteral("key not set"));
    QByteArray derived = QCryptographicHash::hash(
        QByteArray::fromHex(m_signingKeyHex.toUtf8()) + moduleName.toUtf8(),
        QCryptographicHash::Sha256);
    QJsonObject o; o[QStringLiteral("signingKey")] = QString::fromLatin1(derived.toHex());
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── getModules ────────────────────────────────────────────────────────────────
QString BeaconPlugin::getModules()
{
    QMap<QString, QJsonObject> groups;
    for (int i = 0; i < m_log.size(); ++i) {
        QJsonObject e = m_log[i].toObject();
        QString src = e[QStringLiteral("source")].toString();
        if (!groups.contains(src)) {
            QJsonObject g; g[QStringLiteral("name")] = src;
            g[QStringLiteral("cidCount")] = 0; g[QStringLiteral("lastTs")] = qint64(0);
            groups[src] = g;
        }
        QJsonObject g = groups[src];
        g[QStringLiteral("cidCount")] = g[QStringLiteral("cidCount")].toInt() + 1;
        qint64 ts = e[QStringLiteral("ts")].toVariant().toLongLong();
        if (ts > g[QStringLiteral("lastTs")].toVariant().toLongLong())
            g[QStringLiteral("lastTs")] = ts;
        groups[src] = g;
    }
    QList<QJsonObject> list = groups.values();
    std::sort(list.begin(), list.end(), [](const QJsonObject& a, const QJsonObject& b) {
        return a[QStringLiteral("lastTs")].toVariant().toLongLong() >
               b[QStringLiteral("lastTs")].toVariant().toLongLong();
    });
    QJsonArray arr;
    for (const auto& g : list) arr.append(g);
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

// ── ensureCheckpointsDir ──────────────────────────────────────────────────────
QString BeaconPlugin::ensureCheckpointsDir()
{
    if (m_persistencePath.isEmpty()) return errorJson(QStringLiteral("persistence path not set"));
    if (QDir(m_persistencePath + QStringLiteral("/checkpoints")).mkpath(QStringLiteral(".")))
        return okJson();
    return errorJson(QStringLiteral("failed to create checkpoints dir"));
}

// ── confirmInscription ────────────────────────────────────────────────────────
QString BeaconPlugin::confirmInscription(int entryIndex,
                                          const QString& inscriptionId,
                                          const QString& status)
{
    if (entryIndex < 0 || entryIndex >= m_log.size())
        return errorJson(QStringLiteral("entryIndex out of range"));
    QJsonObject entry = m_log[entryIndex].toObject();
    entry[QStringLiteral("inscriptionId")] = inscriptionId;
    entry[QStringLiteral("status")]        = status;
    m_log[entryIndex] = entry;
    saveLog();
    emit inscriptionConfirmed(entryIndex, inscriptionId, status);
    return okJson();
}

// ── findAnchorTx ──────────────────────────────────────────────────────────────
QString BeaconPlugin::findAnchorTx(const QString& nUrl,
                                    const QString& channelId,
                                    int slotFrom, int slotTo)
{
    QJsonObject result; result[QStringLiteral("txHash")] = QString();
    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    QString base = nUrl; if (base.endsWith('/')) base.chop(1);
    QString url = QString("%1/cryptarchia/blocks?slot_from=%2&slot_to=%3").arg(base).arg(slotFrom).arg(slotTo);
    QNetworkReply* reply = m_nam->get(QNetworkRequest{QUrl(url)});
    QEventLoop loop; QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit); loop.exec();
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

// ── findExplorerTxHash ────────────────────────────────────────────────────────
// 1) Scan node blocks [slotFrom, slotTo] for tx matching channelId → get block.header.id + tx index.
// 2) Query explorer /blocks/{blockId}?fork=N → get real explorer TX hash at that index.
QString BeaconPlugin::findExplorerTxHash(const QString& channelId,
                                          int slotFrom, int slotTo)
{
    QJsonObject result;
    result[QStringLiteral("txHash")]    = QString();
    result[QStringLiteral("blockHash")] = QString();
    result[QStringLiteral("found")]     = false;

    if (channelId.isEmpty() || slotFrom <= 0)
        return QJsonDocument(result).toJson(QJsonDocument::Compact);

    if (!m_nam) m_nam = new QNetworkAccessManager(this);

    // ── Step 1: scan node blocks ──────────────────────────────────────────────
    auto getReply = [&](const QString& url) -> QNetworkReply* {
        QNetworkReply* r = m_nam->get(QNetworkRequest{QUrl(url)});
        QEventLoop loop;
        QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        return r;
    };

    QNetworkReply* blocksReply = getReply(
        QString("%1/cryptarchia/blocks?slot_from=%2&slot_to=%3")
            .arg(nodeUrl()).arg(slotFrom).arg(slotTo));

    if (blocksReply->error() != QNetworkReply::NoError) {
        blocksReply->deleteLater();
        return QJsonDocument(result).toJson(QJsonDocument::Compact);
    }
    QJsonDocument blocksDoc = QJsonDocument::fromJson(blocksReply->readAll());
    blocksReply->deleteLater();

    if (!blocksDoc.isArray())
        return QJsonDocument(result).toJson(QJsonDocument::Compact);

    // NOTE: mantle_tx.hash from node != explorer hash (node may compute Poseidon2 differently).
    // Step 1: find the block containing our channelId inscription via node scan.
    // Step 2: query explorer block API to get the real explorer tx hash.
    QString blockHeaderId;
    QString mantleTxHash;  // fallback: mantle_tx.hash from node if explorer is unavailable

    for (const QJsonValue& bv : blocksDoc.array()) {
        const QJsonObject block = bv.toObject();
        const QString     bid   = block[QStringLiteral("header")][QStringLiteral("id")].toString();
        for (const QJsonValue& tv : block[QStringLiteral("transactions")].toArray()) {
            const QJsonObject mt  = tv[QStringLiteral("mantle_tx")].toObject();
            const QJsonArray  ops = mt[QStringLiteral("ops")].toArray();
            for (const QJsonValue& ov : ops) {
                if (ov[QStringLiteral("payload")][QStringLiteral("channel_id")].toString() == channelId) {
                    blockHeaderId = bid;
                    mantleTxHash  = mt[QStringLiteral("hash")].toString();
                    goto found_block;
                }
            }
        }
    }
    return QJsonDocument(result).toJson(QJsonDocument::Compact);  // not found

found_block:
    // ── Step 2: get real tx hash from explorer block API ─────────────────────
    QNetworkReply* forkReply = getReply(
        explorerBaseUrl() + QStringLiteral("/web/explorer/api/v1/fork-choice"));
    int fork = 0;
    if (forkReply->error() == QNetworkReply::NoError) {
        fork = QJsonDocument::fromJson(forkReply->readAll())[QStringLiteral("fork")].toInt(0);
    }
    forkReply->deleteLater();

    QNetworkReply* explorerReply = getReply(
        QString("%1/web/explorer/api/v1/blocks/%2?fork=%3")
            .arg(explorerBaseUrl(), blockHeaderId).arg(fork));

    QString txHash;
    if (explorerReply->error() == QNetworkReply::NoError) {
        QJsonDocument ed = QJsonDocument::fromJson(explorerReply->readAll());
        for (const QJsonValue& tv : ed[QStringLiteral("transactions")].toArray()) {
            for (const QJsonValue& ov : tv[QStringLiteral("operations")].toArray()) {
                if (ov[QStringLiteral("content")][QStringLiteral("channel_id")].toString() == channelId) {
                    txHash = tv[QStringLiteral("hash")].toString();
                    break;
                }
            }
            if (!txHash.isEmpty()) break;
        }
    }
    explorerReply->deleteLater();

    // Fall back to the node's mantle_tx.hash when the explorer hasn't indexed the block yet
    if (txHash.isEmpty() && !mantleTxHash.isEmpty())
        txHash = mantleTxHash;

    if (!txHash.isEmpty()) {
        result[QStringLiteral("txHash")]    = txHash;
        result[QStringLiteral("blockHash")] = blockHeaderId;
        result[QStringLiteral("found")]     = true;
    }
    return QJsonDocument(result).toJson(QJsonDocument::Compact);
}

// ── getBlockForTx ─────────────────────────────────────────────────────────────
QString BeaconPlugin::getBlockForTx(const QString& txHash, int slotFrom)
{
    QJsonObject result;
    result[QStringLiteral("blockHash")] = QString();
    result[QStringLiteral("found")]     = false;
    if (txHash.isEmpty()) return QJsonDocument(result).toJson(QJsonDocument::Compact);

    if (!m_nam) m_nam = new QNetworkAccessManager(this);

    auto getReply = [&](const QString& url) -> QNetworkReply* {
        QNetworkReply* r = m_nam->get(QNetworkRequest{QUrl(url)});
        QEventLoop loop;
        QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        return r;
    };

    QNetworkReply* infoReply = getReply(nodeUrl() + QStringLiteral("/cryptarchia/info"));
    if (infoReply->error() != QNetworkReply::NoError) { infoReply->deleteLater(); return QJsonDocument(result).toJson(QJsonDocument::Compact); }
    QJsonDocument infoDoc = QJsonDocument::fromJson(infoReply->readAll()); infoReply->deleteLater();
    int libSlot = infoDoc[QStringLiteral("lib_slot")].toInt(0);
    if (libSlot <= 0) return QJsonDocument(result).toJson(QJsonDocument::Compact);

    int scanFrom = (slotFrom > 0) ? slotFrom : qMax(0, libSlot - 5000);

    QNetworkReply* blocksReply = getReply(
        QString("%1/cryptarchia/blocks?slot_from=%2&slot_to=%3").arg(nodeUrl()).arg(scanFrom).arg(libSlot));
    if (blocksReply->error() != QNetworkReply::NoError) { blocksReply->deleteLater(); return QJsonDocument(result).toJson(QJsonDocument::Compact); }
    QJsonDocument blocksDoc = QJsonDocument::fromJson(blocksReply->readAll()); blocksReply->deleteLater();
    if (!blocksDoc.isArray()) return QJsonDocument(result).toJson(QJsonDocument::Compact);

    for (const QJsonValue& bv : blocksDoc.array()) {
        const QJsonObject block = bv.toObject();
        const QString headerId  = block[QStringLiteral("header")][QStringLiteral("id")].toString();
        for (const QJsonValue& tv : block[QStringLiteral("transactions")].toArray()) {
            if (tv[QStringLiteral("mantle_tx")][QStringLiteral("hash")].toString() == txHash) {
                result[QStringLiteral("blockHash")] = headerId;
                result[QStringLiteral("found")]     = true;
                return QJsonDocument(result).toJson(QJsonDocument::Compact);
            }
        }
    }
    return QJsonDocument(result).toJson(QJsonDocument::Compact);
}

// ── getManifestLog / recordManifest ───────────────────────────────────────────
QString BeaconPlugin::getManifestLog() const
{
    if (m_persistencePath.isEmpty()) return QJsonDocument(QJsonArray()).toJson(QJsonDocument::Compact);
    QFile f(m_persistencePath + QStringLiteral("/manifest-log.json"));
    if (!f.open(QIODevice::ReadOnly)) return QJsonDocument(QJsonArray()).toJson(QJsonDocument::Compact);
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.isArray() ? QJsonDocument(doc.array()).toJson(QJsonDocument::Compact)
                         : QJsonDocument(QJsonArray()).toJson(QJsonDocument::Compact);
}

QString BeaconPlugin::recordManifest(const QString& moduleName)
{
    if (m_persistencePath.isEmpty()) return errorJson(QStringLiteral("persistence path not set"));
    QString path = m_persistencePath + QStringLiteral("/manifest-log.json");
    QJsonArray arr;
    { QFile rf(path); if (rf.open(QIODevice::ReadOnly)) { QJsonDocument doc = QJsonDocument::fromJson(rf.readAll()); if (doc.isArray()) arr = doc.array(); } }
    for (int i = 0; i < arr.size(); ++i) if (arr[i].toString() == moduleName) return okJson();
    arr.append(moduleName);
    QFile wf(path);
    if (!wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) return errorJson(QStringLiteral("failed to write manifest-log.json"));
    wf.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return okJson();
}
