// make -C desktop test: how old files are, the requests that ask about them, a pack against its folder, and the swap of
// old files for new.

#include "domain/outdated.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

using namespace tz::outdated;

const std::string kDir = "/tmp/themezer-outdated-test";

void make(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    assert(file);
    std::fputs("x", file);
    std::fclose(file);
}

bool exists(const std::string& path)
{
    return access(path.c_str(), F_OK) == 0;
}

bool putFails(const std::string&, const std::string&)
{
    return false;
}

// What updates::replace does when it works: the staged file is the one at path now
bool putWorks(const std::string& staged, const std::string& path)
{
    unlink(path.c_str());
    return rename(staged.c_str(), path.c_str()) == 0;
}

void millis()
{
    const auto at = [](const char* id) { return millisOfId(id); };
    assert(at("Themezer:1E93D:1730000000000") == 1730000000000);
    assert(at("Themezer:1E93D:0") == 0);
    assert(at("Themezer:1E93D:extra:1730000000001") == 1730000000001);
    assert(!at("Themezer:1E93D"));  // the ID is no time
    assert(!at("Themezer"));
    assert(!at("1730000000000"));
    assert(!at(""));
    assert(!at("Themezer:1E93D:"));
    assert(!at("Themezer:1E93D:17300x"));
    assert(!at("Themezer:1E93D:-5"));
    assert(!at("Themezer:1E93D:+5"));
    assert(!at("Themezer:1E93D:5 "));
    assert(!at("Other:1E93D:1730000000000"));
    assert(!at("themezer:1E93D:1730000000000"));
    // 2^63 - 1 fits, one more doesn't
    assert(at("Themezer:1E93D:9223372036854775807") == INT64_MAX);
    assert(!at("Themezer:1E93D:9223372036854775808"));
    assert(!at("Themezer:1E93D:99999999999999999999999"));

    // the ID in it, for a file whose name has none: between the colons, hex, in capitals
    assert(idOfId("Themezer:13782:1770590788521") == "13782");
    assert(idOfId("Themezer:e204:1591829994001") == "E204");
    assert(idOfId("Themezer:1E93D") == "");          // no time: not Themezer's Id
    assert(idOfId("Themezer::1730000000000") == "");
    assert(idOfId("Themezer:1E93D:extra:1730000000001") == "");
    assert(idOfId("Themezer:not-hex:1730000000000") == "");
    assert(idOfId("Other:1E93D:1730000000000") == "");
    assert(idOfId("") == "");
}

void versions()
{
    // the Id wins, even over a newer file
    assert(versionOf(1730000000000, 1800000000) == 1730000000000);
    assert(versionOf(1730000000000, 0) == 1730000000000);
    assert(versionOf(std::nullopt, 1730000000) == 1730000000000);
    assert(!versionOf(std::nullopt, 0));
    assert(!versionOf(std::nullopt, -5));

    assert(packVersion({1730000500, 1730000000, 1730000900}) == 1730000000000);
    assert(packVersion({0, 1730000500, -1}) == 1730000500000);
    assert(!packVersion({0, -3}));
    assert(!packVersion({}));
    assert(packVersion({7}) == 7000);
}

void asked()
{
    assert(canonical("1e93d") == "1E93D");
    assert(canonical("1E93D") == "1E93D");

    const auto made = queries({{'T', "1e93d", 1700000000000}, {'S', "1EB6B", 1700000000001}});
    assert(made.size() == 2);
    assert(made[0].quickId == "T1E93D" && made[0].version == "1700000000000");
    assert(made[1].quickId == "S1EB6B" && made[1].version == "1700000000001");

    // not IDs: a remote splash's code, text, nothing; a version that isn't one; a letter that isn't Themezer's
    assert(queries({{'S', "R12AB", 5}, {'T', "xyz", 5}, {'T', "", 5}, {'T', "1A", 0}, {'T', "1A", -4}, {'R', "1A", 5}})
               .empty());

    const auto copies = queries({{'T', "1A", 100}, {'T', "1a", 300}, {'T', "1A", 200}});
    assert(copies.size() == 1 && copies[0].quickId == "T1A" && copies[0].version == "300");

    const auto both = queries({{'T', "1E93D", 1}, {'S', "1E93D", 2}, {'P', "1E93D", 3}});
    assert(both.size() == 3 && both[0].quickId == "T1E93D" && both[1].quickId == "S1E93D" &&
           both[2].quickId == "P1E93D");

    const auto order = queries({{'P', "FF", 1}, {'T', "10", 1}, {'P', "FF", 2}, {'S', "A", 1}});
    assert(order.size() == 3 && order[0].quickId == "PFF" && order[1].quickId == "T10" && order[2].quickId == "SA");
    assert(order[0].version == "2");

    assert(queries({}).empty());
}

// Brackets pair up in the text as it is written here: no strings with brackets in them
bool balanced(const std::string& json)
{
    int square = 0, curly = 0;
    for (char c : json)
    {
        square += c == '[' ? 1 : c == ']' ? -1 : 0;
        curly += c == '{' ? 1 : c == '}' ? -1 : 0;
        if (square < 0 || curly < 0)
            return false;
    }
    return square == 0 && curly == 0;
}

size_t itemsIn(const std::string& text)
{
    size_t count = 0;
    for (size_t at = text.find("quickId:"); at != std::string::npos; at = text.find("quickId:", at + 1))
        ++count;
    return count;
}

void batches()
{
    const auto many = [](size_t count) {
        std::vector<Query> out;
        for (size_t i = 0; i < count; ++i)
            out.push_back({"T" + std::to_string(i + 1), "1700000000000"});
        return out;
    };
    assert(fields({}).empty());

    const struct
    {
        size_t items;
        std::vector<size_t> sizes;
    } cases[] = {{1, {1}},
                 {kBatch, {kBatch}},
                 {kBatch + 1, {kBatch, 1}},
                 {kBatch * 2 + 30, {kBatch, kBatch, 30}}};
    for (const auto& one : cases)
    {
        const auto sent = fields(many(one.items));
        if (sent.size() != one.sizes.size())
            std::printf("%zu items: %zu requests\n", one.items, sent.size());
        assert(sent.size() == one.sizes.size());
        for (size_t i = 0; i < sent.size(); ++i)
        {
            if (itemsIn(sent[i]) != one.sizes[i])
                std::printf("%zu items, request %zu: %zu items\n", one.items, i, itemsIn(sent[i]));
            assert(itemsIn(sent[i]) == one.sizes[i] && balanced(sent[i]));
        }
    }

    // the text, exactly: a field each, named by its place; the second request starts where the first stopped, at a0
    const std::string two = fields({{"T1E93D", "1730000000000"}, {"S1EB6B", "1730000000001"}})[0];
    const std::string want = "a0:checkUpdates(items:[{quickId:\"T1E93D\",version:\"1730000000000\"}]){...F} "
                             "a1:checkUpdates(items:[{quickId:\"S1EB6B\",version:\"1730000000001\"}]){...F}";
    if (two != want)
        std::printf("got %s\n", two.c_str());
    assert(two == want);
    const auto next = fields(many(kBatch + 1));
    assert(next[1] == "a0:checkUpdates(items:[{quickId:\"T" + std::to_string(kBatch + 1) +
                          "\",version:\"1700000000000\"}]){...F}");
}

void packs()
{
    using Ids = std::vector<std::string>;
    auto diff = diffPack({"1A", "1B", "1C"}, {"1A", "1D"});
    assert((diff.added == Ids{"1B", "1C"}));
    assert((diff.removed == Ids{"1D"}));

    diff = diffPack({"1a", "1B"}, {"1A", "1b", "1B", "1a"});
    assert(diff.added.empty() && diff.removed.empty());

    diff = diffPack({"1A"}, {});
    assert((diff.added == Ids{"1A"}) && diff.removed.empty());
    diff = diffPack({}, {"1a"});
    assert(diff.added.empty() && (diff.removed == Ids{"1A"}));
    diff = diffPack({}, {});
    assert(diff.added.empty() && diff.removed.empty());
}

void moves()
{
    assert(followed("/Themes/A", "/Themes/A", "/Themes/B") == "/Themes/B");
    assert(followed("/Themes/A/x.nxtheme", "/Themes/A", "/Themes/B") == "/Themes/B/x.nxtheme");
    assert(followed("/Themes/A/s/x.nxtheme", "/Themes/A", "/Themes/B") == "/Themes/B/s/x.nxtheme");
    assert(followed("/Themes/AB/x.nxtheme", "/Themes/A", "/Themes/B") == "/Themes/AB/x.nxtheme");
    assert(followed("/Themes/AB", "/Themes/A", "/Themes/B") == "/Themes/AB");
    assert(followed("/Themes/C/x.nxtheme", "/Themes/A", "/Themes/B") == "/Themes/C/x.nxtheme");
    assert(followed("/Themes", "/Themes/A", "/Themes/B") == "/Themes");
    assert(followed("", "/Themes/A", "/Themes/B").empty());
    assert(followed("/Themes/A", "", "/Themes/B") == "/Themes/A");

    // a theme a pack took out, then the pack renamed: each path is where the moves before it put it
    const std::vector<std::pair<std::string, std::string>> made = {
        {"/Themes/P/t.nxtheme", "/Themes/t.nxtheme"}, {"/Themes/P", "/Themes/Q"}};
    assert(rebased("/Themes/P/t.nxtheme", made) == "/Themes/t.nxtheme");
    assert(rebased("/Themes/P/u.nxtheme", made) == "/Themes/Q/u.nxtheme");
    assert(rebased("/Themes/P", made) == "/Themes/Q");
    assert(rebased("/Themes/R/v.nxtheme", made) == "/Themes/R/v.nxtheme");
    assert(rebased("/Themes/P/t.nxtheme", {}) == "/Themes/P/t.nxtheme");

    assert(samePath("/Themes/Foo.nxtheme", "/Themes/foo.NXTHEME"));
    assert(samePath("/a", "/a"));
    assert(!samePath("/a", "/ab"));
    assert(!samePath("/a/b", "/a/c"));
}

void swaps()
{
    mkdir(kDir.c_str(), 0777);
    const std::string staged = kDir + "/staged.nxtheme", path = kDir + "/new.nxtheme";
    const std::string oldOne = kDir + "/old one.nxtheme", oldTwo = kDir + "/old two.nxtheme";
    const auto clean = [&] {
        for (const std::string& file : {staged, path, oldOne, oldTwo, kDir + "/Case.nxtheme", kDir + "/case.nxtheme"})
            unlink(file.c_str());
    };

    // put fails: the old copies stand, and nothing is said to have moved
    clean();
    make(staged);
    make(oldOne);
    make(oldTwo);
    std::vector<std::string> replaced;
    assert(!replace(staged, path, {oldOne, oldTwo}, replaced, putFails));
    assert(exists(oldOne) && exists(oldTwo) && replaced.empty());

    // put works: the old copies are gone, the new file is there, and the copies are said to be it
    clean();
    make(staged);
    make(oldOne);
    make(oldTwo);
    assert(replace(staged, path, {oldOne, oldTwo}, replaced, putWorks));
    assert(exists(path) && !exists(oldOne) && !exists(oldTwo) && !exists(staged));
    assert((replaced == std::vector<std::string>{oldOne, oldTwo}));

    // the new file is among the others: it stays, and moves nowhere
    clean();
    replaced.clear();
    make(staged);
    assert(replace(staged, path, {path}, replaced, putWorks));
    assert(exists(path) && replaced.empty());

    clean();
    replaced.clear();
    make(staged);
    make(oldTwo);
    assert(replace(staged, path, {oldOne, oldTwo}, replaced, putWorks));
    assert(exists(path) && !exists(oldTwo));
    assert((replaced == std::vector<std::string>{oldTwo}));

    // by another case it's the new file on FAT (and on a Mac); where cases differ it's a file of its own, and stays
    clean();
    replaced.clear();
    make(staged);
    make(kDir + "/Case.nxtheme");
    assert(replace(staged, kDir + "/case.nxtheme", {kDir + "/Case.nxtheme"}, replaced, putWorks));
    assert(exists(kDir + "/case.nxtheme"));
    assert((replaced == std::vector<std::string>{kDir + "/Case.nxtheme"}));

    // the same beyond ASCII: FAT (and a Mac) fold "É" to "é", which no byte comparison sees, and the new file stays
    const std::string accented = kDir + "/\xC3\x89" "cran.nxtheme", lower = kDir + "/\xC3\xA9" "cran.nxtheme";
    unlink(accented.c_str());
    unlink(lower.c_str());
    replaced.clear();
    make(staged);
    make(accented);
    assert(replace(staged, lower, {accented}, replaced, putWorks));
    assert(exists(lower));
    assert((replaced == std::vector<std::string>{accented}));
    unlink(accented.c_str());
    unlink(lower.c_str());

    clean();
    rmdir(kDir.c_str());
}

} // namespace

int main()
{
    millis();
    versions();
    asked();
    batches();
    packs();
    moves();
    swaps();
    std::printf("outdated: all good\n");
    return 0;
}
