# Step 11 - Heap-free JSON writer and parser with host tests

## What we are solving
The gateway speaks JSON (G4: `{node, value, timestamp, seq}`, JSON errors, a JSON body for
`POST /interval`). The earlier prototype used malloc/realloc per key; we need the same job
done in fixed buffers, with proof that it works.

## Why
- Writer: the caller owns the buffer. Overflow is sticky: every later call is ignored, the
  buffer stays NUL-terminated and `jw_finish()` returns 0, so a short buffer can never
  produce half a JSON document on the wire.
- Parser: flat objects only (that is all commands and config need). Zero-copy: each pair
  points into the input; strings are unescaped only when asked. Nested values, duplicate
  keys, too many members, bad numbers and bad escapes are errors, not guesses.
- Parsers are the easiest code to test off target. The tests run on the PC in milliseconds
  and they already paid off: they found that the first guess for `JSON_READING_MAX`
  (96 bytes) was too small for the widest reading.

## What changed
- `app/json/json_writer.c/.h` - objects, arrays, keys, strings with escaping, int, uint,
  fixed-point (`jw_fixed2`: 2510 -> `25.10`), bool, null, raw.
- `app/json/json_parser.c/.h` - `json_parse_object`, `json_get_int/bool/string`, `json_find`.
- `app/json/json_reading.c/.h` - `reading_t` to `{"node":..,"value":..,"timestamp":..,"seq":..}`.
- `tests/test_util.h`, `tests/test_json.c`, `tests/Makefile` - host tests (73 checks).
- `Makefile` - `make test` runs the host tests.
- `app/core/app_task.c` - prints the first reading as JSON (later ones at DEBUG).

## How to verify
```
make test            (host gcc on PATH, e.g. C:\msys64\ucrt64\bin)
```
Expected:
```
test_json: 73 checks, 0 failed -> PASS
ALL HOST TESTS PASS
```
Firmware (`.\build.ps1 clean all`), console:
```
[I][app] json {"node":1,"value":23.00,"timestamp":2004,"seq":1}
```
Mentee task: add a test for a string value that contains `\u00e9` and decide what the
parser should return for non-ASCII code points.
