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
#include <algorithm>

// ── QSettings key prefix ──────────────────────────────────────────────────────
static constexpr const char* kNodeUrlKey      = "beacon/nodeUrl";
static constexpr const char* kWatchStashKey   = "beacon/watchStash";
static constexpr const char* kChannelLabelKey = "beacon/channelLabel";

// ── Helpers ───────────────────────────────────────────────────────────────────
QString BeaconPlugin::errorJson(const QString& msg)
{
    QJsonObject o;
    o[QStringLiteral("error")] = msg;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString BeaconPlugin::okJson()
{
    QJsonObject o;
    o[QStringLiteral("ok")] = true;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── Constructor ───────────────────────────────────────────────────────────────
BeaconPlugin::BeaconPlugin(QObject* parent)
    : QObject(parent)
{}

// ── initLogos ─────────────────────────────────────────────────────────────────
void BeaconPlugin::initLogos(LogosAPI* api)
{
    logosAPI = api;

    // Retrieve instancePersistencePath injected by the platform.
    // Falls back to a sensible default if property is not available (e.g. in tests).
    QVariant prop = property("instancePersistencePath");
    if (prop.isValid() && !prop.toString().isEmpty()) {
        m_persistencePath = prop.toString();
    } else {
        m_persistencePath = QDir::homePath() +
            QStringLiteral("/.local/share/Logos/LogosBasecamp/module_data/logos_beacon");
    }

    QDir().mkpath(m_persistencePath);

    loadLog();
}

// ── setSigningKey ─────────────────────────────────────────────────────────────
// Called from QML after keycardAuthComplete delivers the 32-byte domain key.
// Replaces the old file-based ensureKey() — key now comes from hardware each session.
QString BeaconPlugin::setSigningKey(const QString& hexKey)
{
    if (hexKey.length() != 64 || QByteArray::fromHex(hexKey.toUtf8()).size() != 32)
        return errorJson(QStringLiteral("hexKey must be 64 hex chars (32 bytes)"));

    m_signingKeyHex = hexKey;
    return okJson();
}

// ── clearSigningKey ───────────────────────────────────────────────────────────
// Called from QML on card removal or auth restart.
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
        ts << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
           << " " << msg << "\n";
    }
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

// ── setNodeUrl ────────────────────────────────────────────────────────────────
QString BeaconPlugin::setNodeUrl(const QString& url)
{
    if (url.trimmed().isEmpty())
        return errorJson(QStringLiteral("url must not be empty"));
    QSettings s;
    s.setValue(QLatin1String(kNodeUrlKey), url.trimmed());
    return okJson();
}

// ── setWatchStash ─────────────────────────────────────────────────────────────
QString BeaconPlugin::setWatchStash(bool enabled)
{
    QSettings s;
    s.setValue(QLatin1String(kWatchStashKey), enabled);
    return okJson();
}

// ── setChannelLabel ───────────────────────────────────────────────────────────
QString BeaconPlugin::setChannelLabel(const QString& label)
{
    QSettings s;
    s.setValue(QLatin1String(kChannelLabelKey), label.trimmed());
    return okJson();
}

// ── getStatus ─────────────────────────────────────────────────────────────────
QString BeaconPlugin::getStatus() const
{
    bool configured = !m_signingKeyHex.isEmpty();

    int inscribedCids = 0;
    for (int i = 0; i < m_log.size(); ++i) {
        QJsonObject e = m_log[i].toObject();
        if (e[QStringLiteral("status")].toString() == QLatin1String("ok"))
            ++inscribedCids;
    }

    QJsonObject o;
    o[QStringLiteral("configured")]    = configured;
    o[QStringLiteral("seenCids")]      = m_log.size();
    o[QStringLiteral("inscribedCids")] = inscribedCids;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── loadLog / saveLog ─────────────────────────────────────────────────────────
void BeaconPlugin::loadLog()
{
    if (m_persistencePath.isEmpty())
        return;

    QString logPath = m_persistencePath + QStringLiteral("/inscription-log.json");
    QFile f(logPath);
    if (!f.open(QIODevice::ReadOnly))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isArray())
        m_log = doc.array();
}

void BeaconPlugin::saveLog()
{
    if (m_persistencePath.isEmpty())
        return;

    QString logPath = m_persistencePath + QStringLiteral("/inscription-log.json");
    QFile f(logPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(m_log).toJson(QJsonDocument::Compact));
    }
}

// ── getInscriptionLog ─────────────────────────────────────────────────────────
QString BeaconPlugin::getInscriptionLog() const
{
    // Return last 100 entries
    if (m_log.size() <= 100)
        return QJsonDocument(m_log).toJson(QJsonDocument::Compact);

    QJsonArray tail;
    int start = m_log.size() - 100;
    for (int i = start; i < m_log.size(); ++i)
        tail.append(m_log[i]);
    return QJsonDocument(tail).toJson(QJsonDocument::Compact);
}

// ── pinCid ────────────────────────────────────────────────────────────────────
QString BeaconPlugin::pinCid(const QString& cid, const QString& label,
                              const QString& source)
{
    if (cid.trimmed().isEmpty())
        return errorJson(QStringLiteral("cid must not be empty"));

    // Duplicate guard: check if CID already in log
    for (int i = 0; i < m_log.size(); ++i) {
        QJsonObject e = m_log[i].toObject();
        if (e[QStringLiteral("cid")].toString() == cid) {
            QJsonObject o;
            o[QStringLiteral("ok")]        = true;
            o[QStringLiteral("duplicate")] = true;
            return QJsonDocument(o).toJson(QJsonDocument::Compact);
        }
    }

    QJsonObject entry;
    entry[QStringLiteral("cid")]           = cid;
    entry[QStringLiteral("label")]         = label;
    entry[QStringLiteral("source")]        = source;
    entry[QStringLiteral("ts")]            = QDateTime::currentSecsSinceEpoch();
    entry[QStringLiteral("status")]        = QStringLiteral("pending");
    entry[QStringLiteral("inscriptionId")] = QString();

    int entryIndex = m_log.size();
    m_log.append(entry);
    saveLog();

    QJsonObject o;
    o[QStringLiteral("ok")]         = true;
    o[QStringLiteral("entryIndex")] = entryIndex;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── deriveModuleSigningKey ────────────────────────────────────────────────────
QString BeaconPlugin::deriveModuleSigningKey(const QString& moduleName)
{
    if (m_signingKeyHex.isEmpty())
        return errorJson(QStringLiteral("key not set"));

    QByteArray masterKeyBytes  = QByteArray::fromHex(m_signingKeyHex.toUtf8());
    QByteArray moduleNameUtf8  = moduleName.toUtf8();
    QByteArray derived = QCryptographicHash::hash(masterKeyBytes + moduleNameUtf8,
                                                  QCryptographicHash::Sha256);
    QJsonObject o;
    o[QStringLiteral("signingKey")] = QString::fromLatin1(derived.toHex());
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── getModules ────────────────────────────────────────────────────────────────
QString BeaconPlugin::getModules()
{
    QMap<QString, QJsonObject> groups;

    for (int i = 0; i < m_log.size(); ++i) {
        QJsonObject e      = m_log[i].toObject();
        QString     source = e[QStringLiteral("source")].toString();

        if (!groups.contains(source)) {
            QJsonObject g;
            g[QStringLiteral("name")]     = source;
            g[QStringLiteral("cidCount")] = 0;
            g[QStringLiteral("lastTs")]   = qint64(0);
            groups[source] = g;
        }

        QJsonObject g = groups[source];
        g[QStringLiteral("cidCount")] = g[QStringLiteral("cidCount")].toInt() + 1;

        qint64 ts = e[QStringLiteral("ts")].toVariant().toLongLong();
        if (ts > g[QStringLiteral("lastTs")].toVariant().toLongLong())
            g[QStringLiteral("lastTs")] = ts;

        groups[source] = g;
    }

    QList<QJsonObject> list = groups.values();
    std::sort(list.begin(), list.end(), [](const QJsonObject& a, const QJsonObject& b) {
        return a[QStringLiteral("lastTs")].toVariant().toLongLong() >
               b[QStringLiteral("lastTs")].toVariant().toLongLong();
    });

    QJsonArray arr;
    for (const auto& g : list)
        arr.append(g);

    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

// ── ensureCheckpointsDir ──────────────────────────────────────────────────────
QString BeaconPlugin::ensureCheckpointsDir()
{
    if (m_persistencePath.isEmpty())
        return errorJson(QStringLiteral("persistence path not set"));

    QDir dir(m_persistencePath + QStringLiteral("/checkpoints"));
    if (dir.mkpath(QStringLiteral(".")))
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

// ── getManifestLog ────────────────────────────────────────────────────────────
// Returns a JSON array of module names that have been manifested to the primary
// Beacon channel, e.g. ["notes", "stash"]. Loaded from manifest-log.json.
QString BeaconPlugin::getManifestLog() const
{
    if (m_persistencePath.isEmpty())
        return QJsonDocument(QJsonArray()).toJson(QJsonDocument::Compact);

    QString path = m_persistencePath + QStringLiteral("/manifest-log.json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QJsonDocument(QJsonArray()).toJson(QJsonDocument::Compact);

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isArray())
        return QJsonDocument(doc.array()).toJson(QJsonDocument::Compact);

    return QJsonDocument(QJsonArray()).toJson(QJsonDocument::Compact);
}

// ── recordManifest ────────────────────────────────────────────────────────────
// Appends moduleName to manifest-log.json if not already present.
QString BeaconPlugin::recordManifest(const QString& moduleName)
{
    if (m_persistencePath.isEmpty())
        return errorJson(QStringLiteral("persistence path not set"));

    QString path = m_persistencePath + QStringLiteral("/manifest-log.json");

    // Load existing
    QJsonArray arr;
    QFile rf(path);
    if (rf.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(rf.readAll());
        if (doc.isArray())
            arr = doc.array();
        rf.close();
    }

    // Idempotent: only append if not already present
    for (int i = 0; i < arr.size(); ++i)
        if (arr[i].toString() == moduleName)
            return okJson();

    arr.append(moduleName);

    QFile wf(path);
    if (!wf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return errorJson(QStringLiteral("failed to write manifest-log.json"));

    wf.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return okJson();
}
