#pragma once
#include <rapidcheck.h>
#include <string>
#include <crypt.h>

namespace gen {

inline rc::Gen<std::string> username()
{
    return rc::gen::suchThat(
        rc::gen::container<std::string>(rc::gen::inRange((char)'a', (char)('z' + 1))),
        [](const std::string &s) { return s.size() >= 3 && s.size() <= 8; });
}

inline rc::Gen<std::string> password()
{
    return rc::gen::suchThat(
        rc::gen::container<std::string>(rc::gen::inRange((char)'a', (char)('z' + 1))),
        [](const std::string &s) { return s.size() >= 1 && s.size() <= 8; });
}

} // namespace gen
