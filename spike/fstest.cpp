// std::filesystem on Tiger: libc++ built with LIBCXX_ENABLE_FILESYSTEM=ON.
#include <cstdio>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;
static void T(const char *what, bool ok) { std::printf("%-46s %s\n", what, ok ? "PASS" : (++failures, "FAIL")); }

static void writeFile(const fs::path &p, const std::string &contents)
{
    std::ofstream f(p);
    f << contents;
}

int main()
{
    std::error_code ec;

    fs::path tmp = fs::temp_directory_path(ec);
    T("temp_directory_path", !ec && !tmp.empty());

    fs::path root = tmp / "wktiger-fstest";
    fs::remove_all(root, ec);
    T("create_directories", fs::create_directories(root, ec) && !ec);

    fs::path cur = fs::current_path(ec);
    T("current_path (get)", !ec && !cur.empty());
    fs::current_path(root, ec);
    T("current_path (set)", !ec);

    writeFile(root / "a.txt", "hello tiger filesystem\n");
    writeFile(root / "b.txt", "second file, a little longer than the first one\n");
    T("create_directories nested", fs::create_directories(root / "sub" / "deep", ec) && !ec);
    writeFile(root / "sub" / "c.txt", "nested\n");

    T("exists a.txt", fs::exists(root / "a.txt", ec) && !ec);
    T("exists missing.txt is false", !fs::exists(root / "missing.txt", ec) && !ec);

    auto sizeA = fs::file_size(root / "a.txt", ec);
    T("file_size a.txt", !ec && sizeA == std::string("hello tiger filesystem\n").size());
    auto sizeB = fs::file_size(root / "b.txt", ec);
    T("file_size b.txt > a.txt", !ec && sizeB > sizeA);

    int topEntries = 0, dirEntries = 0;
    for (auto const &e : fs::directory_iterator(root, ec)) {
        ++topEntries;
        if (e.is_directory()) ++dirEntries;
    }
    T("directory_iterator count", !ec && topEntries == 3);
    T("directory_iterator sees subdir", dirEntries == 1);

    int recursiveEntries = 0;
    for (auto const &e : fs::recursive_directory_iterator(root, ec)) {
        (void)e;
        ++recursiveEntries;
    }
    T("recursive_directory_iterator", !ec && recursiveEntries == 5); // a.txt, b.txt, sub, sub/deep, sub/c.txt

    auto now = fs::file_time_type::clock::now();
    fs::last_write_time(root / "a.txt", now, ec);
    auto lwt = fs::last_write_time(root / "a.txt", ec);
    T("last_write_time set/get", !ec);
    (void)lwt;

    fs::path copyDest = root / "a-copy.txt";
    bool copied = fs::copy_file(root / "a.txt", copyDest, ec);
    T("copy_file", copied && !ec && fs::exists(copyDest));
    auto copySize = fs::file_size(copyDest, ec);
    T("copy_file size matches", !ec && copySize == sizeA);

    fs::rename(root / "b.txt", root / "b-renamed.txt", ec);
    T("rename", !ec && fs::exists(root / "b-renamed.txt") && !fs::exists(root / "b.txt"));

    bool removedOne = fs::remove(root / "a-copy.txt", ec);
    T("remove single file", removedOne && !ec);

    fs::current_path(cur, ec); // step out before removing root
    T("current_path restore", !ec);

    auto removedCount = fs::remove_all(root, ec);
    T("remove_all", !ec && removedCount > 0 && !fs::exists(root));

    std::printf("%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
