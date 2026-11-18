// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ecservice.h"

#include <QSet>

namespace Glowzone
{

namespace HP = HelperProtocol;

// GameZone WMI method IDs used here – nothing else is ever called.
static constexpr quint8 MethodLightingLayout = 0x0A; // IsSupportLightingFeature
static constexpr quint8 MethodIsSupportBiosLed = 0x0E;
static constexpr quint8 MethodGetLed = 0x0F;
static constexpr quint8 MethodSetLed = 0x10;

static QByteArray hex2(quint8 v)
{
    return QByteArray::number(v, 16).rightJustified(2, '0');
}

static bool isAcpiCallError(const QByteArray &reply)
{
    return reply.startsWith("Error") || reply.startsWith("not called");
}

static std::optional<quint32> parseInteger(const QByteArray &reply)
{
    bool ok = false;
    const quint32 v = reply.trimmed().toUInt(&ok, 0);
    return ok ? std::optional<quint32>(v) : std::nullopt;
}

// acpi_call prints buffers as "{0x01, 0x02, 0x03}".
static std::optional<QList<quint8>> parseBuffer(const QByteArray &reply)
{
    const QByteArray s = reply.trimmed();
    if (!s.startsWith('{') || !s.endsWith('}')) {
        return std::nullopt;
    }
    QList<quint8> bytes;
    for (const QByteArray &part : s.mid(1, s.size() - 2).split(',')) {
        bool ok = false;
        const uint v = part.trimmed().toUInt(&ok, 0);
        if (!ok || v > 0xff) {
            return std::nullopt;
        }
        bytes.append(quint8(v));
    }
    return bytes;
}

EcService::EcService(const DmiInfo &dmi, AcpiBackend &acpi)
    : m_model(matchModel(dmi))
    , m_acpi(acpi)
{
    if (!m_model) {
        m_unsupportedDmi = dmi.vendor + QLatin1String(" / ") + dmi.productVersion + QLatin1String(" / ") + dmi.boardName;
    }
}

QByteArray EcService::gameZoneCall(quint8 method, const QByteArray &arg) const
{
    return QByteArray(m_model->acpiGameZone) + " 0 0x" + hex2(method) + ' ' + arg;
}

int EcService::checkModel(QString *detail) const
{
    if (!m_model) {
        *detail = QStringLiteral("unsupported model: ") + m_unsupportedDmi;
        return HP::UnsupportedModel;
    }
    return 0;
}

// Loads acpi_call if needed and asks the BIOS whether the lights are fitted.
int EcService::prepareBios(QString *detail)
{
    if (!m_acpi.ensureReady(detail)) {
        return HP::AcpiCallUnavailable;
    }
    const auto reply = m_acpi.call(gameZoneCall(MethodIsSupportBiosLed, "0"), detail);
    if (!reply) {
        return HP::AcpiCallFailed;
    }
    if (isAcpiCallError(*reply)) {
        *detail = QString::fromLatin1(*reply);
        return HP::AcpiCallFailed;
    }
    const auto presence = parseInteger(*reply);
    if (!presence) {
        *detail = QStringLiteral("IsSupportBIOSLED returned '%1'").arg(QString::fromLatin1(*reply));
        return HP::UnexpectedReply;
    }
    if (int(*presence) != m_model->biosLedPresence) {
        *detail = QStringLiteral("IsSupportBIOSLED returned '%1', expected 0x%2")
                      .arg(QString::fromLatin1(*reply))
                      .arg(m_model->biosLedPresence, 0, 16);
        return HP::BiosLedMissing;
    }
    return 0;
}

int EcService::preflight(QString *detail)
{
    if (const int error = checkModel(detail)) {
        return error;
    }
    return prepareBios(detail);
}

// The state of every light, as the EC sees it now.
bool EcService::readAll(Result *r)
{
    for (const EcLightDef &light : m_model->ecLights) {
        const auto state = getLed(light.index, &r->error, &r->detail);
        if (!state) {
            r->lights.clear();
            return false;
        }
        r->lights.append(*state);
    }
    return true;
}

std::optional<EcLightState> EcService::getLed(quint8 index, int *error, QString *detail)
{
    const auto reply = m_acpi.call(gameZoneCall(MethodGetLed, QByteArray::number(index)), detail);
    if (!reply || isAcpiCallError(*reply)) {
        if (reply) {
            *detail = QString::fromLatin1(*reply);
        }
        *error = HP::AcpiCallFailed;
        return std::nullopt;
    }
    const auto bytes = parseBuffer(*reply);
    if (!bytes || bytes->size() < 3 || bytes->at(0) != index) {
        *detail = QStringLiteral("GetLED(%1) returned '%2'").arg(index).arg(QString::fromLatin1(*reply));
        *error = HP::UnexpectedReply;
        return std::nullopt;
    }
    return EcLightState{index, bytes->at(1), bytes->at(2)};
}

EcService::Result EcService::status()
{
    Result r;
    r.error = preflight(&r.detail);
    if (!r.error) {
        readAll(&r);
    }
    return r;
}

// About the fans, not the lights: no light presence check.
EcService::LayoutResult EcService::layout()
{
    LayoutResult r;
    r.error = checkModel(&r.detail);
    if (r.error) {
        return r;
    }
    if (!m_acpi.ensureReady(&r.detail)) {
        r.error = HP::AcpiCallUnavailable;
        return r;
    }
    const auto reply = m_acpi.call(gameZoneCall(MethodLightingLayout, "0"), &r.detail);
    if (!reply || isAcpiCallError(*reply)) {
        if (reply) {
            r.detail = QString::fromLatin1(*reply);
        }
        r.error = HP::AcpiCallFailed;
        return r;
    }
    // A buffer {code, 0, 0, 0} – or a plain 0 when no layout matches.
    if (const auto bytes = parseBuffer(*reply); bytes && !bytes->isEmpty()) {
        r.layout = bytes->constFirst();
    } else if (const auto value = parseInteger(*reply)) {
        r.layout = int(*value);
    } else {
        r.detail = QStringLiteral("IsSupportLightingFeature returned '%1'").arg(QString::fromLatin1(*reply));
        r.error = HP::UnexpectedReply;
    }
    return r;
}

std::optional<QList<EcRequest>> EcService::parseRequest(const QVariantMap &args, QString *detail) const
{
    const QVariant listVariant = args.value(QLatin1String(HP::KeyLights));
    if (listVariant.typeId() != QMetaType::QVariantList) {
        *detail = QStringLiteral("missing light list");
        return std::nullopt;
    }
    const QVariantList list = listVariant.toList();
    if (list.isEmpty() || list.size() > m_model->ecLights.size()) {
        *detail = QStringLiteral("bad number of lights: %1").arg(list.size());
        return std::nullopt;
    }

    // Integers only – no text, fractions or booleans that Qt would convert.
    auto readByte = [](const QVariantMap &m, const char *key, uint max, quint8 *out) {
        const QVariant v = m.value(QLatin1String(key));
        switch (v.typeId()) {
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
            break;
        default:
            return false;
        }
        if (v.toLongLong() < 0 || v.toULongLong() > max) {
            return false;
        }
        *out = quint8(v.toUInt());
        return true;
    };

    QList<EcRequest> requests;
    QSet<quint8> seen;
    for (const QVariant &item : list) {
        if (item.typeId() != QMetaType::QVariantMap) {
            *detail = QStringLiteral("light entry is not a map");
            return std::nullopt;
        }
        const QVariantMap m = item.toMap();
        EcRequest req;
        if (!readByte(m, HP::KeyIndex, 0xff, &req.index) || !m_model->ecLightByIndex(req.index) || seen.contains(req.index)) {
            *detail = QStringLiteral("bad or repeated light index");
            return std::nullopt;
        }
        // Only what this model offers; 0 = leave unchanged.
        if (!readByte(m, HP::KeyMode, 0xff, &req.mode) || (req.mode != 0 && !m_model->hasEcLightMode(req.mode))
            || !readByte(m, HP::KeyBrightness, m_model->ecBrightnessLevels, &req.brightness)) {
            *detail = QStringLiteral("mode or brightness out of range");
            return std::nullopt;
        }
        if (req.mode == 0 && req.brightness == 0) {
            *detail = QStringLiteral("nothing to change for light %1").arg(req.index);
            return std::nullopt;
        }
        seen.insert(req.index);
        requests.append(req);
    }
    return requests;
}

EcService::Result EcService::set(const QVariantMap &args)
{
    // Check the request before anything runs as root (module load, BIOS calls).
    Result r;
    r.error = checkModel(&r.detail);
    if (r.error) {
        return r;
    }
    const auto requests = parseRequest(args, &r.detail);
    if (!requests) {
        r.error = HP::InvalidArguments;
        return r;
    }
    r.error = prepareBios(&r.detail);
    if (r.error) {
        return r;
    }

    for (const EcRequest &req : *requests) {
        // SetLED takes a 4-byte buffer: index, mode, brightness, 0.
        const QByteArray buffer = "b" + hex2(req.index) + hex2(req.mode) + hex2(req.brightness) + "00";
        const auto reply = m_acpi.call(gameZoneCall(MethodSetLed, buffer), &r.detail);
        if (!reply) {
            r.error = HP::AcpiCallFailed;
            return r;
        }
        const auto value = parseInteger(*reply);
        if (isAcpiCallError(*reply) || !value || *value != 0) {
            r.detail = QStringLiteral("SetLED(%1) returned '%2'").arg(QString::fromLatin1(buffer), QString::fromLatin1(*reply));
            r.error = HP::AcpiCallFailed;
            return r;
        }
    }

    readAll(&r);
    return r;
}

} // namespace Glowzone
