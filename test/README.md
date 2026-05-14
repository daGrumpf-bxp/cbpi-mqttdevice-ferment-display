# Host-side tests

Tests of `cbpi_proto.cpp` against real CBPi4 payloads, run on the development
host (no D1 mini required). Useful when iterating on the parser.

## Run

```bash
make test
```

Requires `g++` (any version supporting C++17) and `curl` (to fetch ArduinoJson
single-header on first run).
