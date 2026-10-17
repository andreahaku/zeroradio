/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "i18n.h"

#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace toolkit::i18n {

Lang parse_locale(const char* name) {
    if (!name || std::strlen(name) < 2) return Lang::En;
    const auto is = [name](const char* code) {
        return std::strncmp(name, code, 2) == 0 && (name[2] == '\0' || name[2] == '_' ||
                                                    name[2] == '-' || name[2] == '.');
    };
    if (is("it")) return Lang::It;
    if (is("fr")) return Lang::Fr;
    if (is("de")) return Lang::De;
    if (is("es")) return Lang::Es;
    return Lang::En;
}

Lang current() {
    static const Lang lang = [] {
        // The first variable that is set wins, as in the C library: LC_ALL
        // overrides LANG even when it names a language without translations.
        for (const char* var : {"ZERORADIO_LANG", "LC_ALL", "LC_MESSAGES", "LANG"}) {
            const char* value = std::getenv(var);
            if (value && value[0] != '\0') return parse_locale(value);
        }
        return Lang::En;
    }();
    return lang;
}

const char* pick(const Text& text, Lang lang) {
    const char* value = nullptr;
    switch (lang) {
        case Lang::It: value = text.it; break;
        case Lang::Fr: value = text.fr; break;
        case Lang::De: value = text.de; break;
        case Lang::Es: value = text.es; break;
        case Lang::En: break;
    }
    if (value && value[0] != '\0') return value;
    return text.en ? text.en : "";
}

const char* pick(const Text& text) {
    return pick(text, current());
}

} // namespace toolkit::i18n
