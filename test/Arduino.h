// Minimal Arduino stub for host-side testing
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <string>

inline uint32_t millis() { return 0; }
#define F(s) s

// Pretend Serial
struct SerialStub {
    void printf(const char* fmt, ...) {
        va_list a; va_start(a, fmt);
        vprintf(fmt, a); va_end(a);
    }
    void println(const char* s = "") { printf("%s\n", s); }
    void println(uint32_t s) { printf("%u\n", s); }
};
extern SerialStub Serial;
