// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// libFuzzer target for the root helper's request checks. Random bytes are
// turned into a request map – known and unknown keys, every value type,
// nesting – and passed to EcService::set() for every supported model.
// It aborts if any ACPI call other than the allowed GameZone methods with
// values the model allows would be made, and on a SetLED the helper promises
// to avoid (each one pauses the machine): a second one for the same light in
// one request, or one that sets what GetLED reported.

#include "helper/ecservice.h"

#include <QCoreApplication>
#include <QSet>
#include <cstdio>
#include <cstdlib>

using namespace Qt::StringLiterals;

using namespace Glowzone;

[[noreturn]] static void fail(const QByteArray &command)
{
    fprintf(stderr, "UNEXPECTED ACPI COMMAND: %s\n", command.constData());
    abort();
}

class CheckingAcpi : public AcpiBackend
{
public:
    explicit CheckingAcpi(const ModelProfile &model)
        : m_model(model)
    {
    }

    bool ensureReady(QString *) override
    {
        return true;
    }

    std::optional<QByteArray> call(const QByteArray &command, QString *) override
    {
        const QByteArray prefix = QByteArray(m_model.acpiGameZone) + " 0 0x";
        if (!command.startsWith(prefix)) {
            fail(command);
        }
        const QByteArray rest = command.mid(prefix.size());
        if (rest == "0a 0") {
            return QByteArray("{0x") + QByteArray::number(m_model.normalFanLayout, 16) + ", 0x00, 0x00, 0x00}";
        }
        if (rest == "0e 0") {
            return QByteArray("0x") + QByteArray::number(m_model.biosLedPresence, 16);
        }
        if (rest.startsWith("0f ")) {
            bool ok = false;
            const uint index = rest.mid(3).toUInt(&ok);
            if (!ok || !m_model.ecLightByIndex(quint8(index))) {
                fail(command);
            }
            return "{" + hex(index) + ", " + hex(CurrentMode) + ", " + hex(CurrentBrightness) + ", 0x00}";
        }
        // SetLED: "10 b" + index, mode, brightness, 00 as hex bytes
        if (rest.startsWith("10 b") && rest.size() == 12 && rest.endsWith("00")) {
            bool ok1 = false, ok2 = false, ok3 = false;
            const uint index = rest.mid(4, 2).toUInt(&ok1, 16);
            const uint mode = rest.mid(6, 2).toUInt(&ok2, 16);
            const uint brightness = rest.mid(8, 2).toUInt(&ok3, 16);
            const bool valid = ok1 && ok2 && ok3 && m_model.ecLightByIndex(quint8(index)) && m_model.hasEcLightMode(quint8(mode))
                && brightness >= 1 && brightness <= m_model.ecBrightnessLevels;
            const bool needed = (mode != CurrentMode || brightness != CurrentBrightness) && !m_set.contains(index);
            if (!valid || !needed) {
                fail(command);
            }
            m_set.insert(index);
            return QByteArray("0x0");
        }
        fail(command);
    }

private:
    // What GetLED reports for every light.
    static constexpr uint CurrentMode = 1;
    static constexpr uint CurrentBrightness = 1;

    static QByteArray hex(uint n)
    {
        return "0x" + QByteArray::number(n, 16).rightJustified(2, '0');
    }

    const ModelProfile &m_model;
    QSet<uint> m_set; // lights given a SetLED
};

// Reads the fuzzer's bytes as a sequence of choices.
struct Reader {
    const uint8_t *p;
    size_t n;

    uint8_t byte()
    {
        if (n == 0) {
            return 0;
        }
        --n;
        return *p++;
    }
    // Low byte first. One byte per statement: the order of the reads inside one
    // expression isn't specified.
    quint32 word()
    {
        quint32 w = byte();
        w |= quint32(byte()) << 8;
        w |= quint32(byte()) << 16;
        w |= quint32(byte()) << 24;
        return w;
    }
    QString key()
    {
        static const char *keys[] = {"lights", "index", "mode", "brightness", "detail", "", "Lights", "index "};
        return QString::fromLatin1(keys[byte() % 8]);
    }
    QVariant value(int depth)
    {
        switch (byte() % 12) {
        case 0:
            return uint(byte());
        case 1:
            return int(word());
        case 2:
            return word();
        case 3:
        {
            const qint64 high = word();
            return high << 32 | word();
        }
        case 4:
            return double(int(word())) / 7.0;
        case 5:
            return bool(byte() & 1);
        case 6:
            return QString::number(int(word()));
        case 7:
            return u"0x%1"_s.arg(byte(), 0, 16);
        case 8:
            return {};
        case 9:
            return QByteArray(1, char(byte()));
        case 10:
            if (depth < 3) {
                QVariantList list;
                for (int i = byte() % 5; i > 0; --i) {
                    list << value(depth + 1);
                }
                return list;
            }
            return {};
        default:
            if (depth < 3) {
                QVariantMap map;
                for (int i = byte() % 5; i > 0; --i) {
                    map.insert(key(), value(depth + 1));
                }
                return map;
            }
            return {};
        }
    }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static int argc = 1;
    static char name[] = "fuzz_setled";
    static char *argv[] = {name, nullptr};
    static QCoreApplication app(argc, argv);

    for (const ModelProfile &model : supportedModels()) {
        Reader r{data, size};
        QVariantMap args;
        for (int i = r.byte() % 4; i > 0; --i) {
            args.insert(r.key(), r.value(0));
        }
        CheckingAcpi acpi(model);
        EcService service(DmiInfo{model.vendor, model.productVersion, model.boardName}, acpi);
        service.set(args);
    }
    return 0;
}
