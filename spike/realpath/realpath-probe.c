/* Bounded Tiger i386/x86_64 check of the realpath(NULL) compatibility wrapper. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned failures;

static void check(int condition, const char *name)
{
    printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    failures += !condition;
}

static void allocated(const char *path, const char *expected, const char *name)
{
    char *result = realpath(path, NULL);
    check(result && !strcmp(result, expected), name);
    free(result);
}

static void failure(const char *path, int expected, const char *name)
{
    char supplied[PATH_MAX];
    unsigned allocate;
    for (allocate = 0; allocate != 2; ++allocate) {
        errno = 0;
        char *result = realpath(path, allocate ? NULL : supplied);
        int saved = errno;
        char label[256];
        snprintf(label, sizeof(label), "%s %s", allocate ? "allocated" : "supplied", name);
        check(!result && saved == expected, label);
        if (result || saved != expected)
            printf("  result=%s errno=%d expected=%d\n", result ? result : "(null)", saved, expected);
        if (allocate)
            free(result);
    }
}

int main(void)
{
    char directory[] = "/tmp/tiger-realpath-XXXXXX";
    if (!mkdtemp(directory) || chdir(directory)) {
        perror("private probe directory");
        return 2;
    }
    int fd = open("target", O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0 || close(fd) || mkdir("nested", 0700) || symlink("target", "relative-link")
        || symlink("missing", "dangling-link") || symlink("loop-link", "loop-link")) {
        perror("private probe fixture");
        return 2;
    }
    char canonical[PATH_MAX], target[PATH_MAX], supplied[PATH_MAX];
    char *result = realpath(".", canonical);
    check(result == canonical, "caller-supplied buffer returned");
    if (!result)
        return 2;
    if (snprintf(target, sizeof(target), "%s/target", canonical) >= (int)sizeof(target))
        return 2;

    allocated(".", canonical, "allocated relative directory");
    allocated("nested/../target", target, "allocated relative normalized file");
    allocated(target, target, "allocated absolute file");
    allocated("relative-link", target, "allocated relative symlink");
    result = realpath("nested/../target", supplied);
    check(result == supplied && !strcmp(supplied, target), "supplied relative normalized file");

    allocated("nested/../", canonical, "allocated directory trailing slash");
    failure("missing", ENOENT, "nonexistent path preserves ENOENT");
    failure("missing/child", ENOENT, "missing intermediate component");
    failure("", ENOENT, "empty path");
    failure("dangling-link", ENOENT, "dangling symlink");
    failure("loop-link", ELOOP, "symlink loop");
    failure("target/child", ENOTDIR, "non-directory intermediate component");
    failure("target/../target", ENOTDIR, "non-directory parent traversal");
    failure("target/", ENOTDIR, "file trailing slash");

    unlink("loop-link");
    unlink("dangling-link");
    unlink("relative-link");
    unlink("target");
    rmdir("nested");
    chdir("/");
    rmdir(directory);
    printf("realpath %u-bit: %u failures\n", (unsigned)(8 * sizeof(void *)), failures);
    return failures ? 1 : 0;
}
