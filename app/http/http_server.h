/*
 * Minimal HTTP/1.1 server on the lwIP netconn API, port 80.
 * One connection at a time, "Connection: close" after every response.
 * Routes and their JSON are in http_router.h.
 */
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#define HTTP_PORT              80U
#define HTTP_RECV_TIMEOUT_MS   3000

void http_server_start(void);

#endif /* HTTP_SERVER_H */
