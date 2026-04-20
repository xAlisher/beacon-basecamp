#include "BeaconPlugin.h"

#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QRandomGenerator>
#include <QFileDevice>

// ── QSettings key prefix ──────────────────────────────────────────────────────
static constexpr const char* kNodeUrlKey   = "beacon/nodeUrl";
static constexpr const char* kWatchStashKey = "beacon/watchStash";

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

    ensureKey();
    loadLog();
}

// ── ensureKey ─────────────────────────────────────────────────────────────────
// Generate a fresh Ed25519 seed (32 bytes) on first run; read it back on
// subsequent runs. Stored as 64-char lowercase hex, chmod 0600.
void BeaconPlugin::ensureKey()
{
    if (m_persistencePath.isEmpty())
        return;

    QString keyPath = m_persistencePath + QStringLiteral("/beacon.key");

    if (!QFile::exists(keyPath)) {
        // 8 × uint32 = 32 bytes of cryptographically secure randomness
        QByteArray seed(32, Qt::Uninitialized);
        QRandomGenerator::system()->fillRange(
            reinterpret_cast<quint32*>(seed.data()), 8);

        QFile f(keyPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(seed.toHex());
            f.close();
            f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
    }

    QFile f(keyPath);
    if (f.open(QIODevice::ReadOnly)) {
        m_signingKeyHex = QString::fromLatin1(f.readAll().trimmed());
        f.close();
    }
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
QString BeaconPlugin::pinCid(const QString& cid, const QString& label)
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
