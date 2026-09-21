/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "docs.h"

#include <fstream>
#include <sstream>

#ifndef ZERORADIO_DOCS_ROOT
#define ZERORADIO_DOCS_ROOT "/usr/share/zeroradio"
#endif

namespace toolkit {

std::string read_doc(const std::string& relative_path) {
    std::ifstream in(std::string(ZERORADIO_DOCS_ROOT) + "/" + relative_path);
    if (!in) return {};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

} // namespace toolkit
