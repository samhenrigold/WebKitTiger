#include <cstdio>
#include <stdexcept>
int main() {
    try {
        throw std::runtime_error("boom");
    } catch (const std::exception &e) {
        printf("caught: %s\n", e.what());
        return 0;
    }
    return 1;
}
