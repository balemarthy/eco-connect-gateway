# Demo: MQTT publish (`make DEMO=mqtt`)

Publishes the newest reading to an MQTT 3.1.1 broker on the LAN. The payload is exactly the
JSON that `GET /temp` returns, produced by the same function (`json_reading_write()`), so
the data path stays `reading_t` until `app/json/json_reading.c` formats it.

| Item | Value (`app/config/app_config.h`) |
|---|---|
| Library | coreMQTT v2.3.1+ (MIT), `third_party/coreMQTT/` unmodified; config `app/mqtt/core_mqtt_config.h` |
| Transport | `app/mqtt/mqtt_transport_lwip.c`: plain TCP over lwIP netconn (no sockets, no TLS) |
| Broker | `MQTT_BROKER_HOST` "192.168.1.10" : `MQTT_BROKER_PORT` 1883 - IPv4 literal, lwIP has no DNS here |
| Client id | `MQTT_CLIENT_ID_PREFIX` + node, e.g. `eco-gw-1`; clean session, no will, no user/password |
| Topic | `eco/<node>/temp` (`MQTT_TOPIC_PREFIX`, node id from `SET node`, `MQTT_TOPIC_SUFFIX`) |
| QoS | `MQTT_QOS` 1 (0 also supported, compile time) - QoS 1 tracks up to 4 unacked publishes |
| Keep-alive | `MQTT_KEEPALIVE_S` 60 s (PINGREQ from `MQTT_ProcessLoop`) |
| Interval | `MQTT_PUBLISH_INTERVAL_MS` 10 s, independent of the sampling interval |
| Reconnect | 2 s, doubling to 60 s |
| State | registry key `mqtt` -> `GET mqtt` on the serial link: state, broker, topic, connects, published, acked, errors |
| Task | `mqtt`, 1024 words static stack, priority idle+2, supervisor window 30 s (TCP connect to a silent host can take ~20 s) |
| Cost (measured) | +16,348 B flash, +4,920 B RAM over `DEMO=none` |

coreMQTT objects are compiled with `-DNDEBUG` (Makefile, target-specific): their parameter
`assert()`s would otherwise link newlib's `__assert_func` and `fprintf` (+4.9 KB flash).

## Flow
```
mqtt_task
  wait net_is_up()
  mqtt_transport_connect(MQTT_BROKER_HOST, 1883)        netconn_new / netconn_connect
  MQTT_Init(transport, now_ms, on_event, 512 B buffer)  + MQTT_InitStatefulQoS for QoS 1
  MQTT_Connect(clean session, keepalive, client id, 5 s CONNACK timeout)
  every 100 ms : MQTT_ProcessLoop  (PUBACK -> on_event, PINGREQ/PINGRESP)
  every 10 s   : app_latest() -> json_reading_write() -> MQTT_Publish(topic, qos)
  error        : MQTT_Disconnect / close -> back-off -> reconnect
```

## Transport contract (what coreMQTT expects)
`TransportRecv_t` / `TransportSend_t` with an application-defined `struct NetworkContext`
(`transport_interface.h`). recv returns bytes copied, 0 when nothing arrived within 10 ms,
-1 on a closed connection; the context keeps a partly used netbuf between calls because
coreMQTT reads a packet in pieces. send uses `netconn_write_partly()` with a 2 s send timeout
and may return fewer bytes than asked. `writev` is NULL (coreMQTT then uses send).

## Try it (not run in this job)
```
mosquitto -v                                  # on 192.168.1.10, port 1883, anonymous allowed
mosquitto_sub -h 192.168.1.10 -t 'eco/+/temp' -v
make DEMO=mqtt                                # or .\build.ps1 -Demo mqtt
```
Expected: `eco/1/temp {"node":1,"value":25.10,"timestamp":12345,"seq":12}` every 10 s.
For a broker that requires TLS or credentials this demo needs a TLS transport (e.g. mbedTLS
or wolfSSL over the same netconn) - not part of this template.
