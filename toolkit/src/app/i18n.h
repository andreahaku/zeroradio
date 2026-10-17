/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace toolkit::i18n {

// UI languages with translations. The bundled fonts have no CJK glyphs, so a
// Japanese or Chinese system falls back to English.
enum class Lang { En, It, Fr, De, Es };

// One string in every supported language. A null entry falls back to `en`.
struct Text {
    const char* en{nullptr};
    const char* it{nullptr};
    const char* fr{nullptr};
    const char* de{nullptr};
    const char* es{nullptr};

    bool empty() const { return en == nullptr || en[0] == '\0'; }
};

// Language from a POSIX locale name ("it_IT.UTF-8", "de", "C"): English when
// the name is empty or not supported.
Lang parse_locale(const char* name);

// The user's language: ZERORADIO_LANG, then LC_ALL, LC_MESSAGES and LANG, the
// way the CardputerZero Store reads it. Read once, then cached.
Lang current();

// `text` in `lang` (or in current()), falling back to English.
const char* pick(const Text& text, Lang lang);
const char* pick(const Text& text);

} // namespace toolkit::i18n
