#pragma once
#include <rapidcheck.h>
#include <string>
#include <vector>

namespace gen {

inline rc::Gen<std::string> http_method()
{
    auto methods = std::vector<std::string>{
        "GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"};
    return rc::gen::elementOf(methods);
}

} // namespace gen
