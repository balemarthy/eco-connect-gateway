/*
 * MQTT demo (make DEMO=mqtt): publishes the latest reading as JSON to the broker.
 *
 *   topic    MQTT_TOPIC_PREFIX <node> MQTT_TOPIC_SUFFIX     e.g. eco/1/temp
 *   payload  json_reading_write() output, the same text GET /temp returns:
 *            {"node":1,"value":25.10,"timestamp":12345,"seq":12}
 *   QoS      MQTT_QOS (0 or 1), clean session, no will, no credentials
 *
 * Task loop (one connection at a time):
 *   wait for the netif -> TCP connect -> MQTT_Init (+ StatefulQoS for QoS 1) -> CONNECT
 *   -> every 100 ms MQTT_ProcessLoop (keep-alive PINGREQ, PUBACKs)
 *   -> every MQTT_PUBLISH_INTERVAL_MS: app_latest() -> JSON -> MQTT_Publish
 *   any error -> close, wait (2 s doubling to 60 s), reconnect.
 * All settings: app/config/app_config.h. State: registry key "mqtt" (GET mqtt).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#define LOG_TAG LOG_MOD_DEMO
#include "log.h"

#include "app_task.h"
#include "core_mqtt.h"
#include "demo.h"
#include "json_reading.h"
#include "mqtt_transport_lwip.h"
#include "net_init.h"
#include "registry.h"
#include "sensor.h"
#include "supervisor.h"

#define MQTT_LOOP_MS          100U
#define MQTT_OUTGOING_QOS1    4U          /* unacknowledged QoS 1 publishes tracked at once */
/* TCP connect can take ~20 s against a silent host (see mqtt_transport_lwip.h) */
#define MQTT_SUP_WINDOW_MS    30000U

typedef enum { MQ_WAIT_NET = 0, MQ_CONNECTING, MQ_CONNECTED, MQ_BACKOFF } mq_state_t;
static const char *const mq_state_names[] = { "wait_net", "connecting", "connected", "backoff" };

static StaticTask_t task_tcb;
static StackType_t  task_stack[DEMO_TASK_STACK_WORDS];
static sup_id_t     sup_id = SUP_INVALID;

static NetworkContext_t   net;
static MQTTContext_t      mqtt;
static uint8_t            net_buf[MQTT_NET_BUFFER_SIZE];
#if MQTT_QOS == 1U
static MQTTPubAckInfo_t   outgoing[MQTT_OUTGOING_QOS1];
#endif

static char topic[MQTT_TOPIC_MAX];
static char client_id[MQTT_CLIENT_ID_MAX];

static volatile mq_state_t state;
static volatile uint32_t   n_connects, n_published, n_acked, n_errors;

/* ---------------------------------------------------------------- coreMQTT hooks */
static void log_v(log_level_t lvl, const char *fmt, va_list ap)
{
    char line[96];
    (void)vsnprintf(line, sizeof line, fmt, ap);
    log_write(LOG_TAG, lvl, "mqtt: %s", line);
}

void mqtt_log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_v(LOG_LVL_ERROR, fmt, ap);
    va_end(ap);
}

void mqtt_log_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_v(LOG_LVL_WARN, fmt, ap);
    va_end(ap);
}

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void on_event(MQTTContext_t *ctx, MQTTPacketInfo_t *pkt, MQTTDeserializedInfo_t *info)
{
    (void)ctx;
    if ((pkt->type & 0xF0U) == MQTT_PACKET_TYPE_PUBACK) {
        n_acked++;
        LOG_D("puback id=%u", (unsigned)info->packetIdentifier);
    }
    /* nothing is subscribed, so no incoming PUBLISH is expected */
}

/* ---------------------------------------------------------------- helpers */
/* Waits ms in MQTT_LOOP_MS steps, checking in with the supervisor. */
static void wait_ms(uint32_t ms)
{
    for (uint32_t t = 0U; t < ms; t += MQTT_LOOP_MS) {
        sup_checkin(sup_id);
        vTaskDelay(pdMS_TO_TICKS(MQTT_LOOP_MS));
    }
}

static void make_names(void)
{
    unsigned node = (unsigned)sensor_get_node_id();
    (void)snprintf(topic, sizeof topic, "%s%u%s", MQTT_TOPIC_PREFIX, node, MQTT_TOPIC_SUFFIX);
    (void)snprintf(client_id, sizeof client_id, "%s%u", MQTT_CLIENT_ID_PREFIX, node);
}

static bool session_open(void)
{
    TransportInterface_t tr = {
        .recv = mqtt_transport_recv,
        .send = mqtt_transport_send,
        .writev = NULL,                              /* coreMQTT falls back to send() */
        .pNetworkContext = &net,
    };
    MQTTFixedBuffer_t fb = { .pBuffer = net_buf, .size = sizeof net_buf };
    MQTTConnectInfo_t ci;
    bool present = false;

    make_names();
    sup_checkin(sup_id);
    if (!mqtt_transport_connect(&net, MQTT_BROKER_HOST, (uint16_t)MQTT_BROKER_PORT)) {
        LOG_W("mqtt: no TCP connection to %s:%u", MQTT_BROKER_HOST, (unsigned)MQTT_BROKER_PORT);
        return false;
    }
    sup_checkin(sup_id);

    MQTTStatus_t st = MQTT_Init(&mqtt, &tr, now_ms, on_event, &fb);
#if MQTT_QOS == 1U
    if (st == MQTTSuccess) {
        st = MQTT_InitStatefulQoS(&mqtt, outgoing, MQTT_OUTGOING_QOS1, NULL, 0U);
    }
#endif
    if (st == MQTTSuccess) {
        memset(&ci, 0, sizeof ci);
        ci.cleanSession = true;
        ci.keepAliveSeconds = (uint16_t)MQTT_KEEPALIVE_S;
        ci.pClientIdentifier = client_id;
        ci.clientIdentifierLength = (uint16_t)strlen(client_id);
        st = MQTT_Connect(&mqtt, &ci, NULL, MQTT_CONNACK_TIMEOUT_MS, &present);
    }
    if (st != MQTTSuccess) {
        LOG_W("mqtt: connect failed: %s", MQTT_Status_strerror(st));
        mqtt_transport_close(&net);
        return false;
    }
    n_connects++;
    LOG_I("mqtt: connected to %s:%u as %s, topic %s qos %u", MQTT_BROKER_HOST,
          (unsigned)MQTT_BROKER_PORT, client_id, topic, (unsigned)MQTT_QOS);
    return true;
}

static void session_close(bool graceful)
{
    if (graceful) {
        (void)MQTT_Disconnect(&mqtt);
    }
    mqtt_transport_close(&net);
}

/* Publishes the newest reading. Nothing to send yet is not an error. */
static bool publish_latest(void)
{
    reading_t r;
    char json[JSON_READING_MAX];

    if (!app_latest(&r)) {
        return true;
    }
    size_t n = json_reading_write(json, sizeof json, &r);
    if (n == 0U) {
        return true;
    }
    MQTTPublishInfo_t pi;
    memset(&pi, 0, sizeof pi);
    pi.qos = (MQTT_QOS == 1U) ? MQTTQoS1 : MQTTQoS0;
    pi.pTopicName = topic;
    pi.topicNameLength = (uint16_t)strlen(topic);
    pi.pPayload = json;
    pi.payloadLength = n;

    uint16_t id = (MQTT_QOS == 1U) ? MQTT_GetPacketId(&mqtt) : 0U;
    MQTTStatus_t st = MQTT_Publish(&mqtt, &pi, id);
    if (st != MQTTSuccess) {
        LOG_W("mqtt: publish failed: %s", MQTT_Status_strerror(st));
        return false;
    }
    n_published++;
    LOG_D("mqtt: %s %s", topic, json);
    return true;
}

/* ---------------------------------------------------------------- task */
static void mqtt_task(void *arg)
{
    (void)arg;
    uint32_t backoff = MQTT_RECONNECT_MIN_MS;

    for (;;) {
        state = MQ_WAIT_NET;
        while (!net_is_up()) {
            wait_ms(500U);
        }

        state = MQ_CONNECTING;
        if (!session_open()) {
            n_errors++;
            state = MQ_BACKOFF;
            wait_ms(backoff);
            backoff = (backoff * 2U > MQTT_RECONNECT_MAX_MS) ? MQTT_RECONNECT_MAX_MS : backoff * 2U;
            continue;
        }
        backoff = MQTT_RECONNECT_MIN_MS;
        state = MQ_CONNECTED;

        uint32_t next_pub = now_ms();
        bool ok = true;
        while (ok && net_is_up()) {
            sup_checkin(sup_id);
            MQTTStatus_t st = MQTT_ProcessLoop(&mqtt);   /* keep-alive and acks */
            if (st != MQTTSuccess && st != MQTTNeedMoreBytes) {
                LOG_W("mqtt: process loop: %s", MQTT_Status_strerror(st));
                ok = false;
                break;
            }
            if ((int32_t)(now_ms() - next_pub) >= 0) {
                next_pub += MQTT_PUBLISH_INTERVAL_MS;
                ok = publish_latest();
            }
            vTaskDelay(pdMS_TO_TICKS(MQTT_LOOP_MS));
        }
        if (!ok) {
            n_errors++;
        }
        session_close(ok);
        LOG_W("mqtt: disconnected");
        state = MQ_BACKOFF;
        wait_ms(backoff);
    }
}

/* ---------------------------------------------------------------- registry: GET mqtt */
static reg_status_t get_mqtt(json_writer_t *w, void *ctx)
{
    (void)ctx;
    jw_kv_str(w, "state", mq_state_names[state]);
    jw_kv_str(w, "broker", MQTT_BROKER_HOST);
    jw_kv_uint(w, "port", MQTT_BROKER_PORT);
    jw_kv_str(w, "topic", topic);
    jw_kv_uint(w, "qos", MQTT_QOS);
    jw_kv_uint(w, "connects", n_connects);
    jw_kv_uint(w, "published", n_published);
    jw_kv_uint(w, "acked", n_acked);
    jw_kv_uint(w, "errors", n_errors);
    return REG_OK;
}

static const reg_entry_t mqtt_entry = {
    "mqtt", "MQTT demo: state, broker, counters", get_mqtt, NULL, NULL
};

void demo_start(void)
{
    make_names();
    if (registry_register(&mqtt_entry) != REG_OK) {
        LOG_E("mqtt: registry full");
    }
    sup_id = sup_register("mqtt", MQTT_SUP_WINDOW_MS);
    TaskHandle_t h = xTaskCreateStatic(mqtt_task, "mqtt", DEMO_TASK_STACK_WORDS, NULL,
                                       tskIDLE_PRIORITY + DEMO_TASK_PRIORITY, task_stack, &task_tcb);
    configASSERT(h != NULL);
}
