/*
 * coreMQTT configuration for the Eco-Connect MQTT demo (make DEMO=mqtt).
 * coreMQTT includes this file from core_mqtt_config_defaults.h; anything not set here
 * keeps the library default.
 */
#ifndef CORE_MQTT_CONFIG_H
#define CORE_MQTT_CONFIG_H

#include <stdint.h>

/* Library errors and warnings go to our log under the "demo" tag. coreMQTT passes the
   arguments in double parentheses: LogError( ( "fmt %d", x ) ). */
void mqtt_log_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void mqtt_log_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define LogError(message)  mqtt_log_error message
#define LogWarn(message)   mqtt_log_warn message
/* LogInfo / LogDebug stay empty (library default): one line per packet is too much for the log queue. */

/* Upper bound for sending one whole packet; the transport also has its own send timeout. */
#define MQTT_SEND_TIMEOUT_MS              5000U
/* How long ProcessLoop keeps reading once a packet has started to arrive. */
#define MQTT_RECV_POLLING_TIMEOUT_MS      100U
#define MQTT_PINGRESP_TIMEOUT_MS          5000U

#endif /* CORE_MQTT_CONFIG_H */
