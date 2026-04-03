#pragma once
#include <rapidcheck.h>
#include <string>
#include <vector>

namespace gen {

inline rc::Gen<std::string> file_extension()
{
    auto exts = std::vector<std::string>{
        ".html", ".css", ".js", ".json", ".xml", ".php",
        ".png", ".jpg", ".gif", ".svg", ".txt", ".pdf",
        ".gz", ".zip", ".woff2"};
    return rc::gen::elementOf(exts);
}

} // namespace gen
