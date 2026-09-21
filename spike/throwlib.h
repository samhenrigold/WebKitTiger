#pragma once
#include <stdexcept>
struct __attribute__((visibility("default"))) LibError : std::runtime_error { using std::runtime_error::runtime_error; };
__attribute__((visibility("default"))) void throwFromLib(int n);
__attribute__((visibility("default"))) int catchInLib(void (*fn)());
