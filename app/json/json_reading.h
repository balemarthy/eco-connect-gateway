/*
 * reading_t <-> JSON.
 *
 *   {"node":1,"value":25.10,"timestamp":12345,"seq":12}
 *
 * value     temperature in degC, two decimals
 * timestamp ms since boot (no wall clock on the board yet)
 * seq       sample counter from the sensor task
 * A failed sensor read adds "status":"sensor_error" (value is then the last good one).
 */
#ifndef JSON_READING_H
#define JSON_READING_H

#include <stddef.h>

#include "json_writer.h"
#include "readings.h"

#define JSON_READING_MAX  112U    /* widest case is 99 chars + NUL (see test_json.c) */

/* Writes only the members (node, value, ...) into an already open object. */
void   json_reading_members(json_writer_t *w, const reading_t *r);
/* Writes the reading as one object value into an open writer. */
void   json_reading_put(json_writer_t *w, const reading_t *r);
/* Stand-alone object into buf; returns length or 0 if buf is too small. */
size_t json_reading_write(char *buf, size_t size, const reading_t *r);

#endif /* JSON_READING_H */
