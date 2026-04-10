/**
 * prop_generators_smoke.cpp - Smoke test for all custom RapidCheck generators
 *
 * Verifies that all generators in tests/generators/ compile, link, and
 * produce valid values. This is not a property test per se, but ensures
 * the generator infrastructure works before downstream property tests use it.
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <cstring>

extern "C" {
#include "htaccess_directive.h"
#include "htaccess_cidr.h"
#include "htaccess_expires.h"
}

#include "gen_regex.h"
#include "gen_header.h"
#include "gen_cidr.h"
#include "gen_expires.h"
#include "gen_directive.h"
#include "gen_htaccess.h"
#include "gen_directory.h"

/* ------------------------------------------------------------------ */
/*  gen_regex.h smoke tests                                            */
/* ------------------------------------------------------------------ */

RC_GTEST_PROP(GenRegex, ProducesNonEmptyPatterns, ())
{
    auto pattern = *gen::simpleRegex();
    RC_ASSERT(!pattern.empty());
}

RC_GTEST_PROP(GenRegex, FileMatchRegexNonEmpty, ())
{
    auto pattern = *gen::fileMatchRegex();
    RC_ASSERT(!pattern.empty());
}

/* ------------------------------------------------------------------ */
/*  gen_header.h smoke tests                                           */
/* ------------------------------------------------------------------ */

RC_GTEST_PROP(GenHeader, HeaderNameContainsDash, ())
{
    auto name = *gen::headerName();
    RC_ASSERT(name.find('-') != std::string::npos);
}

RC_GTEST_PROP(GenHeader, HeaderValueNonEmpty, ())
{
    auto value = *gen::headerValue();
    RC_ASSERT(!value.empty());
}

RC_GTEST_PROP(GenHeader, SimpleValueNoWhitespace, ())
{
    auto value = *gen::simpleValue();
    RC_ASSERT(!value.empty());
    for (char c : value)
        RC_ASSERT(!isspace((unsigned char)c));
}

/* ------------------------------------------------------------------ */
/*  gen_cidr.h smoke tests                                             */
/* ------------------------------------------------------------------ */

RC_GTEST_PROP(GenCidr, CidrRangeHasValidMask, ())
{
    auto cidr = *gen::cidrRange();
    /* network must be properly masked */
    RC_ASSERT((cidr.network & cidr.mask) == cidr.network);
}

RC_GTEST_PROP(GenCidr, CidrStringIsParseable, ())
{
    auto str = *gen::cidrString();
    cidr_v4_t parsed;
    RC_ASSERT(cidr_parse(str.c_str(), &parsed) == 0);
}

RC_GTEST_PROP(GenCidr, IpInCidrActuallyMatches, ())
{
    auto cidr = *gen::cidrRange();
    auto ip = *gen::ipInCidr(cidr);
    RC_ASSERT(cidr_match(&cidr, ip) == 1);
}

/* ------------------------------------------------------------------ */
/*  gen_expires.h smoke tests                                          */
/* ------------------------------------------------------------------ */

RC_GTEST_PROP(GenExpires, DurationStringStartsWithAccess, ())
{
    auto result = *gen::expiresDuration();
    RC_ASSERT(result.first.find("access plus") == 0);
    RC_ASSERT(result.second > 0);
}

RC_GTEST_PROP(GenExpires, DurationStringIsParseable, ())
{
    auto result = *gen::expiresDuration();
    long secs = parse_expires_duration(result.first.c_str());
    RC_ASSERT(secs == result.second);
}

/* ------------------------------------------------------------------ */
/*  gen_directive.h smoke tests                                        */
/* ------------------------------------------------------------------ */

RC_GTEST_PROP(GenDirective, SimpleDirectiveIsNotNull, ())
{
    auto *d = *gen::simpleDirective();
    RC_ASSERT(d != nullptr);
    htaccess_directives_free(d);
}

RC_GTEST_PROP(GenDirective, DirectiveListHasCorrectCount, ())
{
    auto count = *rc::gen::inRange(1, 6);
    auto *head = *gen::directiveList(count);
    RC_ASSERT(head != nullptr);

    int actual = 0;
    for (auto *d = head; d; d = d->next)
        actual++;
    RC_ASSERT(actual >= 1);
    RC_ASSERT(actual <= count);

    htaccess_directives_free(head);
}

/* ------------------------------------------------------------------ */
/*  gen_htaccess.h smoke tests                                         */
/* ------------------------------------------------------------------ */

RC_GTEST_PROP(GenHtaccess, ContentEndsWithNewline, ())
{
    auto content = *gen::htaccessContent(5);
    RC_ASSERT(!content.empty());
    RC_ASSERT(content.back() == '\n');
}

RC_GTEST_PROP(GenHtaccess, TaggedContentTypesMatchLineCount, ())
{
    auto tc = *gen::taggedHtaccessContent(5);
    RC_ASSERT(!tc.first.empty());
    RC_ASSERT(!tc.second.empty());
    /* Count newlines in content should equal number of types */
    int newlines = 0;
    for (char c : tc.first)
        if (c == '\n') newlines++;
    RC_ASSERT((int)tc.second.size() == newlines);
}

/* ------------------------------------------------------------------ */
/*  gen_directory.h smoke tests                                        */
/* ------------------------------------------------------------------ */

RC_GTEST_PROP(GenDirectory, HierarchyHasAtLeastOneLevel, ())
{
    auto h = *gen::dirHierarchy(3);
    RC_ASSERT(!h.levels.empty());
    RC_ASSERT(!h.doc_root.empty());
}

RC_GTEST_PROP(GenDirectory, TargetPathStartsWithDocRoot, ())
{
    auto h = *gen::dirHierarchy(3);
    auto target = h.targetPath();
    RC_ASSERT(target.find(h.doc_root) == 0);
}

RC_GTEST_PROP(GenDirectory, AllPathsCountEqualsLevelsPlusOne, ())
{
    auto h = *gen::dirHierarchy(3);
    auto paths = h.allPaths();
    RC_ASSERT(paths.size() == h.levels.size() + 1);
}

RC_GTEST_PROP(GenDirectory, FilePathEndsWithHtaccess, ())
{
    auto path = *gen::filePath();
    RC_ASSERT(path.size() > 10);
    RC_ASSERT(path.substr(path.size() - 10) == "/.htaccess");
}

/* ------------------------------------------------------------------ */
/*  v2 generator smoke tests                                           */
/* ------------------------------------------------------------------ */

#include "gen_options.h"
#include "gen_http_method.h"
#include "gen_mime.h"
#include "gen_extension.h"
#include "gen_htpasswd.h"
#include "gen_require.h"

/* gen_options.h */
RC_GTEST_PROP(GenOptions, FlagStartsWithPlusOrMinus, ())
{
    auto flag = *gen::options_flag();
    RC_ASSERT(flag[0] == '+' || flag[0] == '-');
}

RC_GTEST_PROP(GenOptions, LineStartsWithOptions, ())
{
    auto line = *gen::options_line();
    RC_ASSERT(line.find("Options") == 0);
}

/* gen_http_method.h */
RC_GTEST_PROP(GenHttpMethod, MethodIsUpperCase, ())
{
    auto method = *gen::http_method();
    RC_ASSERT(!method.empty());
    for (char c : method)
        RC_ASSERT(isupper((unsigned char)c));
}

/* gen_mime.h */
RC_GTEST_PROP(GenMime, MimeContainsSlash, ())
{
    auto mime = *gen::mime_type();
    RC_ASSERT(mime.find('/') != std::string::npos);
}

/* gen_extension.h */
RC_GTEST_PROP(GenExtension, ExtensionStartsWithDot, ())
{
    auto ext = *gen::file_extension();
    RC_ASSERT(ext[0] == '.');
}

/* gen_htpasswd.h */
RC_GTEST_PROP(GenHtpasswd, UsernameIsAlpha, ())
{
    auto user = *gen::username();
    RC_ASSERT(user.size() >= 3);
    for (char c : user)
        RC_ASSERT(islower((unsigned char)c));
}

RC_GTEST_PROP(GenHtpasswd, PasswordIsNonEmpty, ())
{
    auto pass = *gen::password();
    RC_ASSERT(!pass.empty());
}

/* gen_require.h */
RC_GTEST_PROP(GenRequire, DirectiveStartsWithRequire, ())
{
    auto dir = *gen::require_directive();
    RC_ASSERT(dir.find("Require") == 0);
}

/* gen_directive.h v2 types */
RC_GTEST_PROP(GenDirectiveV2, V2DirectiveIsNotNull, ())
{
    /* Generate a v2 directive type and verify it produces a valid directive */
    auto type = *rc::gen::oneOf(gen::directiveTypeV2a(), gen::directiveTypeV2b());
    auto *d = *gen::directiveOfType(type);
    RC_ASSERT(d != nullptr);
    RC_ASSERT(d->type == type);
    htaccess_directives_free(d);
}

/* gen_htaccess.h v2 content */
RC_GTEST_PROP(GenHtaccessV2, V2ContentEndsWithNewline, ())
{
    auto content = *gen::htaccessContentV2(5);
    RC_ASSERT(!content.empty());
    RC_ASSERT(content.back() == '\n');
}

RC_GTEST_PROP(GenHtaccessV2, V2TaggedContentTypesMatchLineCount, ())
{
    auto tc = *gen::taggedHtaccessContentV2(5);
    RC_ASSERT(!tc.first.empty());
    RC_ASSERT(!tc.second.empty());
    int newlines = 0;
    for (char c : tc.first)
        if (c == '\n') newlines++;
    RC_ASSERT((int)tc.second.size() == newlines);
}
