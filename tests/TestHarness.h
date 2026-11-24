// Dependency-free micro test harness + allocation tracker.
#pragma once

#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace mbx::test
{
struct Failure : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct Case
{
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry()
{
    static std::vector<Case> cases;
    return cases;
}

struct Registrar
{
    Registrar (const char* n, void (*f)()) { registry().push_back ({ n, f }); }
};

/** Counts heap allocations while `tracking` is true (see TestMain.cpp operator new). */
inline std::atomic<bool> tracking { false };
inline std::atomic<long> allocations { 0 };

struct AllocationScope
{
    AllocationScope()  { allocations = 0; tracking = true; }
    ~AllocationScope() { tracking = false; }
    long count() const { return allocations.load(); }
};
} // namespace mbx::test

#define MBX_TEST(name)                                                    \
    static void name();                                                   \
    static ::mbx::test::Registrar name##_registrar (#name, &name);        \
    static void name()

#define MBX_CHECK(cond, ...)                                                                   \
    do {                                                                                       \
        if (! (cond))                                                                          \
        {                                                                                      \
            char msg_[400];                                                                    \
            std::snprintf (msg_, sizeof (msg_), "" __VA_ARGS__);                               \
            char buf_[600];                                                                    \
            std::snprintf (buf_, sizeof (buf_), "%s:%d: CHECK(%s) failed. %s",                 \
                           __FILE__, __LINE__, #cond, msg_);                                   \
            throw ::mbx::test::Failure (buf_);                                                 \
        }                                                                                      \
    } while (0)
