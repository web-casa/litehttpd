#pragma once
#include <rapidcheck.h>
#include <string>
#include <vector>

namespace gen {

inline rc::Gen<std::string> options_flag()
{
    auto flags = std::vector<std::string>{
        "+Indexes", "-Indexes", "+FollowSymLinks", "-FollowSymLinks",
        "+MultiViews", "-MultiViews", "+ExecCGI", "-ExecCGI"};
    return rc::gen::elementOf(flags);
}

inline rc::Gen<std::string> options_line()
{
    return rc::gen::map(
        rc::gen::suchThat(
            rc::gen::container<std::vector<std::string>>(options_flag()),
            [](const std::vector<std::string> &v) {
                return v.size() >= 1 && v.size() <= 4;
            }),
        [](const std::vector<std::string> &flags) {
            std::string s = "Options";
            for (auto &f : flags) s += " " + f;
            return s;
        });
}

} // namespace gen
