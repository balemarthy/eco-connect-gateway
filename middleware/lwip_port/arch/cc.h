/*
 * lwIP compiler/platform abstraction for arm-none-eabi-gcc + newlib on the STM32F411.
 */
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdint.h>

/* newlib provides errno.h */
#define LWIP_ERRNO_STDINCLUDE  1
/* newlib's sys/time.h already has struct timeval */
#define LWIP_TIMEVAL_PRIVATE   0

#define BYTE_ORDER  LITTLE_ENDIAN

void     lwip_port_assert(const char *msg, const char *file, int line);
uint32_t lwip_port_rand(void);

/* Diagnostics are off in this build (LWIP_DEBUG not set); asserts stop the system. */
#define LWIP_PLATFORM_DIAG(x)    do { } while (0)
#define LWIP_PLATFORM_ASSERT(x)  lwip_port_assert((x), __FILE__, __LINE__)

/* newlib rand() would allocate its state on first use: use our own generator. */
#define LWIP_RAND()  lwip_port_rand()

#endif /* LWIP_ARCH_CC_H */
