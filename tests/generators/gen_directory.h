/**
 * gen_directory.h - Directory hierarchy generator for RapidCheck
 *
 * Generates random directory hierarchies with associated .htaccess
 * file content for testing the DirWalker component. Each level in
 * the hierarchy may or may not have a .htaccess file.
 *
 * Validates: Requirements 13.1
 */
#ifndef GEN_DIRECTORY_H
#define GEN_DIRECTORY_H

#include <rapidcheck.h>
#include <string>
#include <vector>
#include <utility>

#include "gen_header.h"
#include "gen_htaccess.h"

namespace gen {

/**
 * A single directory level: directory name and optional .htaccess content.
 * If htaccess_content is empty, no .htaccess file exists at this level.
 */
struct DirLevel {
    std::string name;
    std::string htaccess_content; /* empty = no .htaccess */
};

/**
 * A complete directory hierarchy from doc root to target.
 */
struct DirHierarchy {
    std::string doc_root;
    std::vector<DirLevel> levels;

    /** Build the full target directory path. */
    std::string targetPath() const
    {
        std::string path = doc_root;
        for (const auto &level : levels)
            path += "/" + level.name;
        return path;
    }

    /** Get all directory paths from root to target (inclusive). */
    std::vector<std::string> allPaths() const
    {
        std::vector<std::string> paths;
        std::string path = doc_root;
        paths.push_back(path);
        for (const auto &level : levels) {
            path += "/" + level.name;
            paths.push_back(path);
        }
        return paths;
    }
};

static const std::string kDirNameChars =
    "abcdefghijklmnopqrstuvwxyz0123456789_-";

/**
 * Generate a simple directory name (2-8 chars).
 */
inline rc::Gen<std::string> dirName()
{
    return rc::gen::mapcat(
        rc::gen::inRange(2, 9),
        [](int len) {
            return rc::gen::map(
                rc::gen::container<std::vector<char>>(
                    (std::size_t)len,
                    rc::gen::elementOf(kDirNameChars)),
                [](const std::vector<char> &v) {
                    return std::string(v.begin(), v.end());
                });
        });
}

/**
 * Generate a single directory level with optional .htaccess content.
 * hasHtaccess controls whether this level has a .htaccess file.
 */
inline rc::Gen<DirLevel> dirLevel(bool hasHtaccess)
{
    if (hasHtaccess) {
        return rc::gen::map(
            rc::gen::pair(dirName(), htaccessContent(5)),
            [](const std::pair<std::string, std::string> &p) {
                return DirLevel{p.first, p.second};
            });
    }
    return rc::gen::map(dirName(),
        [](const std::string &name) {
            return DirLevel{name, ""};
        });
}

/**
 * Generate a random directory hierarchy with 1-maxDepth levels.
 * Each level has a ~70% chance of containing a .htaccess file.
 */
inline rc::Gen<DirHierarchy> dirHierarchy(int maxDepth = 4)
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, maxDepth + 1),
        [](int depth) {
            return rc::gen::map(
                rc::gen::container<std::vector<DirLevel>>(
                    (std::size_t)depth,
                    rc::gen::mapcat(
                        rc::gen::inRange(0, 10),
                        [](int r) {
                            /* ~70% chance of having .htaccess */
                            return dirLevel(r < 7);
                        })),
                [](const std::vector<DirLevel> &levels) {
                    DirHierarchy h;
                    h.doc_root = "/var/www/html";
                    h.levels = levels;
                    return h;
                });
        });
}

/**
 * Generate a random file path like "/var/www/dir1/dir2/.htaccess".
 */
inline rc::Gen<std::string> filePath()
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, 5),
        [](int depth) {
            return rc::gen::map(
                rc::gen::container<std::vector<std::string>>(
                    (std::size_t)depth, dirName()),
                [](const std::vector<std::string> &dirs) {
                    std::string path = "/var/www";
                    for (const auto &d : dirs)
                        path += "/" + d;
                    path += "/.htaccess";
                    return path;
                });
        });
}

} /* namespace gen */

#endif /* GEN_DIRECTORY_H */
