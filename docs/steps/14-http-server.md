# Step 14 - HTTP server with /temp and /interval

## What we are solving
The customer side of the 4C map: a browser or a script asks the gateway for the current
temperature and changes the sampling interval (brief G4).

## Why
- lwIP's bundled httpd serves files and CGI; the brief needs JSON endpoints and a JSON
  body on a write. A small request parser + router on top of `netconn` is less code than
  bending httpd, and every byte of it is ours to test.
- "SET /interval" is not an HTTP method. Decision: **`POST /interval`** with the JSON body
  `{"interval_ms":2000}`. `GET /interval` reads it back.
- The router only talks to the registry, so `POST /interval` and the serial
  `SET interval 2000` run the same setter with the same limits (100..60000 ms).
- Limits are explicit: request 1024 bytes, body 512, path 63, receive timeout 3 s. Over the
  limit is a JSON error (413/400/408), never a buffer overrun. One connection at a time,
  `Connection: close` - enough for a sensor endpoint and easy to reason about.
- Parser and router are pure C and run in the host tests against fake registry entries.

## What changed
- `app/http/http_router.c/.h` - request parser (`http_parse_request`, incremental:
  returns INCOMPLETE until headers and `Content-Length` bytes are in), route table,
  handlers for `/`, `/temp`, `/interval` (GET, POST), `/status`, `/version`, JSON errors
  400/404/405 (+ `Allow`)/413/503.
- `app/http/http_response.c/.h` - status line and headers, body helpers, HTML escaping.
- `app/http/http_server.c/.h` - netconn task on port 80, static buffers.
- `tests/test_http.c` - 76 checks: parsing (query, body, incomplete, malformed, too large),
  every route and error, the HTML page, the header format.

## How to verify
```
make test
.\build.ps1 clean all
```
Host: `test_http: 76 checks, 0 failed -> PASS`. Console: `[I][http] listening :80`.

From the PC (board at 192.168.1.50):
```
curl http://192.168.1.50/temp
{"node":1,"value":23.13,"timestamp":40004,"seq":20}

curl -X POST -H "Content-Type: application/json" -d "{\"interval_ms\":5000}" http://192.168.1.50/interval
{"interval_ms":5000}

curl -i http://192.168.1.50/nope
HTTP/1.1 404 Not Found ... {"error":"not found","detail":"/nope"}
```
Console: `[I][http] GET /temp 200`, `[I][http] POST /interval 200`.

Evidence for milestone 3: capture one full request and response in Wireshark.
