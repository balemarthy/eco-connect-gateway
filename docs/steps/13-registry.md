# Step 13 - GET-SET command registry

## What we are solving
"Get the interval", "set the node id", "turn on debug logs for the network" must work the
same over the serial link today and over HTTP tomorrow (brief G5: the Eco-Connect GET-SET
registry, bounded buffers, no malloc).

## Why
- One table of `{key, help, get, set}` entries, registered once before the scheduler and
  read-only afterwards, so lookups need no lock.
- Getters write JSON members into a writer the registry opens and closes; setters get the
  value as text. Interfaces only translate their own syntax (`SET interval 5000`,
  `POST /interval {"interval_ms":5000}`) into `registry_set("interval", "5000")`.
- Errors are JSON too: `{"error":"unknown key","key":"foo"}`. A setter that rejects a
  value leaves the old value in place.
- The earlier prototype's CLI table defined globals in a header and printed from inside the
  interpreter; here the registry is pure C and host-tested (74 checks).

## What changed
- `app/registry/registry.c/.h` - register/find/get/set/list, status strings, JSON errors,
  `reg_parse_u32()` for setters.
- `app/registry/commands.c/.h` - the gateway commands: `temp`, `interval`, `node`, `log`,
  `status`, `version`.
- `app/proto/proto_task.c` - `GET`/`SET` packets go through the registry; `GET` with no key
  (or `help`) lists the keys.
- `app/json/json_reading.c/.h` - `json_reading_members()` for the `temp` getter.
- `tests/test_registry.c` - round trip, every error path, write-only and read-only keys,
  small buffers, full table, list output, number parsing.

## How to verify
```
make test
.\build.ps1 clean all
```
Host: `test_registry: 74 checks, 0 failed -> PASS`. Console: `[I][main] registry: 6 commands`.

Over the serial link:
```
$GET S 08 interval*9F                 -> $RSP J 14 {"interval_ms":2000}*..
$SET S 0D interval 5000*A2            -> $RSP J 14 {"interval_ms":5000}*..   readings now every 5 s
$GET N 00 *..                         -> $RSP J .. {"keys":[{"key":"temp","rw":"r"},...]}*..
```
Mentee task: add a `threshold` key (rw) that the app task uses to log a warning when the
temperature crosses it; add its host test first.
