/*
 * Eco-Connect gateway - one configuration header for every optional demo.
 *
 * Demo selection is done by the build, not here:
 *
 *   make                    DEMO=none (default): this header is not used by any linked code
 *   make DEMO=tcp_echo      -DAPP_DEMO=1 -DAPP_DEMO_TCP_ECHO=1
 *   make DEMO=mqtt          -DAPP_DEMO=1 -DAPP_DEMO_MQTT=1
 *   make DEMO=plusio        -DAPP_DEMO=1 -DAPP_DEMO_PLUSIO=1
 *   make DEMO=ota           -DAPP_DEMO=1 -DAPP_DEMO_OTA=1
 *
 * Exactly one demo task is started (demo_start() from main) when a demo is selected.
 * All values below are plain #defines with #ifndef guards, so a build can override any
 * of them with -D... on the make command line (e.g. make DEMO=mqtt EXTRA_DEFS=-DMQTT_QOS=0).
 *
 * Network addresses are placeholders for a lab LAN. lwIP is built without DNS
 * (LWIP_DNS 0 in lwipopts.h), so hosts must be IPv4 literals. No credentials are
 * configured anywhere: the MQTT demo connects without user name / password.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ------------------------------------------------------------------ demo selection */
#ifndef APP_DEMO
#define APP_DEMO            0
#endif
#ifndef APP_DEMO_TCP_ECHO
#define APP_DEMO_TCP_ECHO   0
#endif
#ifndef APP_DEMO_MQTT
#define APP_DEMO_MQTT       0
#endif
#ifndef APP_DEMO_PLUSIO
#define APP_DEMO_PLUSIO     0
#endif
#ifndef APP_DEMO_OTA
#define APP_DEMO_OTA        0
#endif

#if (APP_DEMO_TCP_ECHO + APP_DEMO_MQTT + APP_DEMO_PLUSIO + APP_DEMO_OTA) > 1
#error "select at most one demo (make DEMO=...)"
#endif

/* Stack and priority shared by the demo tasks (only one runs). */
#define DEMO_TASK_STACK_WORDS   1024U
#define DEMO_TASK_PRIORITY      2U          /* tskIDLE_PRIORITY + 2, same as http/proto */

/* ------------------------------------------------------------------ TCP echo */
#ifndef TCP_ECHO_PORT
#define TCP_ECHO_PORT           7U          /* RFC 862 echo; HTTP keeps :80 */
#endif
#define TCP_ECHO_RECV_TIMEOUT_MS 10000U     /* idle client is closed after this */
#define TCP_ECHO_MAX_BYTES      (64UL * 1024UL)  /* per connection, then closed */

/* ------------------------------------------------------------------ MQTT */
#ifndef MQTT_BROKER_HOST
#define MQTT_BROKER_HOST        "192.168.1.10"   /* IPv4 literal (no DNS); e.g. a LAN mosquitto */
#endif
#ifndef MQTT_BROKER_PORT
#define MQTT_BROKER_PORT        1883U            /* plain TCP; TLS (8883) is not built in */
#endif
#ifndef MQTT_CLIENT_ID_PREFIX
#define MQTT_CLIENT_ID_PREFIX   "eco-gw-"        /* + node id, e.g. "eco-gw-1" */
#endif
#ifndef MQTT_TOPIC_PREFIX
#define MQTT_TOPIC_PREFIX       "eco/"           /* topic = "eco/<node>/temp" */
#endif
#ifndef MQTT_TOPIC_SUFFIX
#define MQTT_TOPIC_SUFFIX       "/temp"
#endif
#ifndef MQTT_QOS
#define MQTT_QOS                1U               /* 0 or 1 (2 is not used) */
#endif
#ifndef MQTT_KEEPALIVE_S
#define MQTT_KEEPALIVE_S        60U
#endif
#ifndef MQTT_PUBLISH_INTERVAL_MS
#define MQTT_PUBLISH_INTERVAL_MS 10000U          /* publishes the latest reading this often */
#endif
#define MQTT_CONNACK_TIMEOUT_MS 5000U
#define MQTT_RECONNECT_MIN_MS   2000U            /* back-off doubles up to the max */
#define MQTT_RECONNECT_MAX_MS   60000U
#define MQTT_NET_BUFFER_SIZE    512U             /* coreMQTT fixed buffer (one packet) */
#define MQTT_TOPIC_MAX          48U
#define MQTT_CLIENT_ID_MAX      24U
/* payload = json_reading_write() output, JSON_READING_MAX (112) bytes at most */

#if (MQTT_QOS != 0U) && (MQTT_QOS != 1U)
#error "MQTT_QOS must be 0 or 1"
#endif

/* ------------------------------------------------------------------ OTA download */
#ifndef OTA_SERVER_HOST
#define OTA_SERVER_HOST         "192.168.1.10"   /* default for the ota_url key */
#endif
#ifndef OTA_SERVER_PORT
#define OTA_SERVER_PORT         8080U
#endif
#ifndef OTA_SERVER_PATH
#define OTA_SERVER_PATH         "/fw/eco-connect-gateway.bin"
#endif
#ifndef OTA_BLOCK_SIZE
#define OTA_BLOCK_SIZE          1024U            /* bytes per HTTP Range request */
#endif
#ifndef OTA_MAX_IMAGE_SIZE
#define OTA_MAX_IMAGE_SIZE      (16UL * 1024UL)  /* RAM backend size; FatFs backend may go larger */
#endif
#define OTA_URL_MAX             96U
#define OTA_HTTP_TIMEOUT_MS     5000U            /* per block request */
#define OTA_BLOCK_RETRIES       3U

/* verification: size + CRC32 is always done; SHA-256 is not built (see docs/demos/ota.md) */
#define OTA_VERIFY_CRC32        1
#define OTA_VERIFY_SHA256       0
#ifndef OTA_VERIFY
#define OTA_VERIFY              OTA_VERIFY_CRC32
#endif

/* image store: RAM buffer (default) or a file on the FatFs volume */
#define OTA_STORAGE_RAM         1
#define OTA_STORAGE_FATFS       2
#ifndef OTA_STORAGE
#define OTA_STORAGE             OTA_STORAGE_RAM
#endif
#define OTA_FATFS_FILE          "OTA.BIN"

#if (OTA_MAX_IMAGE_SIZE % OTA_BLOCK_SIZE) != 0
#error "OTA_MAX_IMAGE_SIZE must be a multiple of OTA_BLOCK_SIZE"
#endif

#endif /* APP_CONFIG_H */
