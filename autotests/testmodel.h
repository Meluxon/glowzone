// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "model/modelprofile.h"

// The model the tests are written for (also in release builds, unlike Q_ASSERT).
inline const Glowzone::ModelProfile &legionModel()
{
    using namespace Qt::StringLiterals;
    const Glowzone::ModelProfile *m = Glowzone::matchModel({u"LENOVO"_s, u"Legion T5 26IOB6"_s, u"3733"_s});
    if (!m) {
        qFatal("No model profile for the Legion T5 26IOB6");
    }
    return *m;
}
