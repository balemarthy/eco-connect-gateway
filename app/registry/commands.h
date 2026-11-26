/*
 * The gateway's GET-SET commands (registry entries).
 *
 *   key       rw  value / result
 *   temp      r   {"node":1,"value":25.10,"timestamp":1234,"seq":12}
 *   interval  rw  {"interval_ms":2000}          set: "100".."60000"
 *   node      rw  {"node":1}                    set: "1".."65535"
 *   log       rw  {"log":{"main":"info",...}}   set: "debug" (all) or "net debug"
 *   status    r   uptime, counters, link, ip, heap
 *   version   r   {"fw":"...","freertos":"...","lwip":"..."}
 */
#ifndef COMMANDS_H
#define COMMANDS_H

#define FW_VERSION "0.1.0"

/* Registers every command. Call once before the scheduler starts. */
void commands_init(void);

#endif /* COMMANDS_H */
