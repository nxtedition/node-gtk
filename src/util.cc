/*
 * util.cc
 * Copyright (C) 2016 romgrk <romgrk@Romgrk-ARCH>
 *
 * Distributed under terms of the MIT license.
 */

#include <node.h>
#include <nan.h>
#include <girepository.h>
#include <glib-object.h>

#include <regex>
#include "util.h"

using v8::Local;
using v8::Value;
using v8::Object;
using v8::String;

namespace Util {

std::string hyphenCaseToSnakeCase(const std::string& input) {
    std::string output = input;
    std::replace( output.begin(), output.end(), '-', '_');
    return output;
}

std::string snakeCaseToCamelCase(const std::string& input) {
    std::string output = input;
    int n = output.length();
    int res_ind = 0;
    for (int i = 0; i < n; i++) {
        if (output[i] == '_') {
            output[i + 1] = toupper(output[i + 1]);
            continue;
        } else {
            output[res_ind++] = output[i];
        }
    }
    output.erase(res_ind, n);
    if (output[0]) {
        output[0] = tolower(output[0]);
    }
    return output;
}

std::string camelCaseToSnakeCase(std::string &&camelCase) {
    std::string str(1, tolower(camelCase[0]));

    // First place underscores between contiguous lower and upper case letters.
    // For example, `_LowerCamelCase` becomes `_Lower_Camel_Case`.
    for (auto it = camelCase.begin() + 1; it != camelCase.end(); ++it) {
      if (isupper(*it) && *(it-1) != '_' && islower(*(it-1))) {
        str += "_";
      }
      str += *it;
    }
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}

const char* ArrayTypeToString (GIArrayType array_type) {
    switch (array_type) {
    case GI_ARRAY_TYPE_C:
        return "C-array";
    case GI_ARRAY_TYPE_ARRAY:
        return "GArray";
    case GI_ARRAY_TYPE_PTR_ARRAY:
        return "GPtrArray";
    case GI_ARRAY_TYPE_BYTE_ARRAY:
        return "GByteArray";
    }
    g_assert_not_reached();
}

char* GetSignalName(const char* signal_detail) {
    char* signal_name;
    char* detail_start;
    if ((detail_start = g_strrstr(signal_detail, "::")) != NULL) {
        signal_name = g_strndup(signal_detail, reinterpret_cast<gsize>(detail_start) - reinterpret_cast<gsize>(signal_detail));
    } else {
        signal_name = g_strdup(signal_detail);
    }
    return signal_name;
}


/**
 * This function is used to call "process._tickCallback()" inside NodeJS.
 * We want to do this after we run the LibUV eventloop because there might
 * be pending Micro-tasks from Promises or calls to 'process.nextTick()'.
 */
void CallNextTickCallback() {
    Local<Object> processObject = Nan::GetCurrentContext()->Global()->Get(
            Nan::New<String>("process").ToLocalChecked())->ToObject();
    Local<Value> tickCallbackValue = processObject->Get(Nan::New("_tickCallback").ToLocalChecked());
    if (tickCallbackValue->IsFunction()) {
        Nan::CallAsFunction(tickCallbackValue->ToObject(), processObject, 0, nullptr);
    }
}

}
