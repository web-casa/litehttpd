#pragma once
#include <rapidcheck.h>
#include <string>
#include <vector>

namespace gen {

inline rc::Gen<std::string> mime_type()
{
    auto types = std::vector<std::string>{
        "text/html", "text/css", "text/plain", "text/javascript",
        "application/json", "application/xml", "application/pdf",
        "application/javascript", "image/png", "image/jpeg",
        "image/gif", "image/svg+xml", "font/woff2"};
    return rc::gen::elementOf(types);
}

} // namespace gen
