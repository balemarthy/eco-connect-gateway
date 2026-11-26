#include "json_reading.h"

void json_reading_put(json_writer_t *w, const reading_t *r)
{
    jw_object_begin(w);
    jw_kv_uint(w, "node", r->node);
    jw_kv_fixed2(w, "value", r->value_centi);
    jw_kv_uint(w, "timestamp", r->timestamp_ms);
    jw_kv_uint(w, "seq", r->seq);
    if (r->status != READING_OK) {
        jw_kv_str(w, "status", "sensor_error");
    }
    jw_object_end(w);
}

size_t json_reading_write(char *buf, size_t size, const reading_t *r)
{
    json_writer_t w;
    jw_init(&w, buf, size);
    json_reading_put(&w, r);
    return jw_finish(&w);
}
