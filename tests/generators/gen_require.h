#pragma once
#include <rapidcheck.h>
#include <string>
#include <vector>

namespace gen {

inline rc::Gen<std::string> require_directive()
{
    auto directives = std::vector<std::string>{
        "Require all granted", "Require all denied",
        "Require ip 10.0.0.0/8", "Require ip 192.168.0.0/16",
        "Require not ip 172.16.0.0/12", "Require valid-user"};
    return rc::gen::elementOf(directives);
}

} // namespace gen
