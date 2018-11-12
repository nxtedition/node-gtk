/*
 * util.h
 * Copyright (C) 2016 romgrk <romgrk@Romgrk-ARCH>
 *
 * Distributed under terms of the MIT license.
 */

#pragma once

#include <node.h>
#include <girepository.h>

namespace Util {
    std::string hyphenCaseToSnakeCase(const std::string& input);
    std::string snakeCaseToCamelCase(const std::string& input);
    std::string camelCaseToSnakeCase(std::string &&camelCase);

    const char*    ArrayTypeToString (GIArrayType array_type);
    char*          GetSignalName(const char* signal_detail);

    void           CallNextTickCallback();

} /* Util */
