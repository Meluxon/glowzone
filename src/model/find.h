// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <iterator>

namespace Glowzone
{

// The first element of list that matches, or nullptr: the lookups in the
// model and mode tables.
template<typename List, typename Predicate>
const auto *findIf(const List &list, Predicate matches)
{
    const auto it = std::ranges::find_if(list, matches);
    return it != std::ranges::end(list) ? &*it : nullptr;
}

} // namespace Glowzone
