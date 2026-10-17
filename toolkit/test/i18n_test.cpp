/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

// toolkit::i18n: locale names to languages, and the English fallback.

#include "i18n.h"

#include <cstdio>
#include <cstring>

using namespace toolkit::i18n;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

int main() {
    check(parse_locale("it_IT.UTF-8") == Lang::It, "it_IT.UTF-8 -> it");
    check(parse_locale("fr_CA") == Lang::Fr, "fr_CA -> fr");
    check(parse_locale("de") == Lang::De, "de -> de");
    check(parse_locale("es-ES") == Lang::Es, "es-ES -> es");
    check(parse_locale("en_US.UTF-8") == Lang::En, "en_US -> en");
    check(parse_locale("ja_JP.UTF-8") == Lang::Ja, "ja_JP -> ja");
    check(parse_locale("zh_CN.UTF-8") == Lang::Zh, "zh_CN -> zh");
    check(parse_locale("zh_TW.UTF-8") == Lang::Zh, "zh_TW -> zh (Simplified)");
    check(parse_locale("ko_KR.UTF-8") == Lang::En, "ko (no translation) -> en");
    check(needs_cjk_font(Lang::Ja) && needs_cjk_font(Lang::Zh), "ja and zh need a CJK font");
    check(!needs_cjk_font(Lang::It), "it uses Inter");
    check(parse_locale("C") == Lang::En, "C -> en");
    check(parse_locale("") == Lang::En, "empty -> en");
    check(parse_locale(nullptr) == Lang::En, "null -> en");
    check(parse_locale("item") == Lang::En, "a word starting with a code is not that code");

    const Text full{"Zoom in", "Ingrandisci", "Zoom avant", "Vergrößern", "Acercar",
                    "ズームイン", "放大"};
    check(std::strcmp(pick(full, Lang::It), "Ingrandisci") == 0, "pick it");
    check(std::strcmp(pick(full, Lang::De), "Vergrößern") == 0, "pick de");
    check(std::strcmp(pick(full, Lang::En), "Zoom in") == 0, "pick en");
    check(std::strcmp(pick(full, Lang::Ja), "ズームイン") == 0, "pick ja");
    check(std::strcmp(pick(full, Lang::Zh), "放大") == 0, "pick zh");

    const Text partial{"Zoom in", nullptr, "", nullptr, nullptr, nullptr, ""};
    check(std::strcmp(pick(partial, Lang::It), "Zoom in") == 0, "null falls back to en");
    check(std::strcmp(pick(partial, Lang::Fr), "Zoom in") == 0, "empty falls back to en");
    check(std::strcmp(pick(partial, Lang::Ja), "Zoom in") == 0, "null ja falls back to en");
    check(std::strcmp(pick(partial, Lang::Zh), "Zoom in") == 0, "empty zh falls back to en");

    const Text none{};
    check(none.empty(), "default Text is empty");
    check(std::strcmp(pick(none, Lang::Es), "") == 0, "empty Text picks an empty string");

    if (failures == 0) std::printf("i18n_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
