/*
 * Plus-IO style adapter demo (make DEMO=plusio).
 *
 * Every PIO_DEMO_PERIOD_MS the task
 *   - opens /SPI2/, selects the W25Q chip select and reads the JEDEC ID (0x9F, 3 bytes)
 *     in one CS cycle through PIO_IOCTL_SPI_XFER - the same bytes w25q_probe() reads;
 *   - opens /I2C1/, sets the sensor address and reads 2 bytes (the I2C driver is the
 *     mentee stub, so this reports "not ready" until i2c_bus.c is written);
 *   - shows that /UART1/ is refused (owned by the protocol task) - TODO in plusio.c.
 * It only logs; nothing else in the gateway depends on it.
 */
#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#define LOG_TAG LOG_MOD_DEMO
#include "log.h"

#include "board.h"
#include "demo.h"
#include "plusio.h"
#include "spi_bus.h"
#include "supervisor.h"

#define PIO_DEMO_PERIOD_MS   5000U
#define PIO_DEMO_I2C_ADDR    0x48U        /* placeholder: typical I2C temperature sensor address */

static StaticTask_t task_tcb;
static StackType_t  task_stack[DEMO_TASK_STACK_WORDS];
static sup_id_t     sup_id = SUP_INVALID;

/* Same chip select as drivers/w25q/w25q.c; the SPI bus mutex keeps both users apart. */
static const spi_device_t w25q_cs = {
    .name = "w25q-pio", .cs_port = BOARD_W25Q_CS_PORT, .cs_pin = BOARD_W25Q_CS_PIN
};

static void demo_spi(void)
{
    pio_handle_t spi = pio_open("/SPI2/", 0U);
    if (spi == NULL) {
        LOG_E("pio: /SPI2/ not in the device table");
        return;
    }
    (void)pio_ioctl(spi, PIO_IOCTL_SET_TX_TIMEOUT, (void *)(uintptr_t)50U);
    (void)pio_ioctl(spi, PIO_IOCTL_SPI_SET_DEVICE, (void *)&w25q_cs);

    static const uint8_t cmd[1] = { 0x9FU };         /* JEDEC ID */
    uint8_t id[3] = { 0U, 0U, 0U };
    pio_spi_xfer_t x = { cmd, sizeof cmd, id, sizeof id };
    pio_status_t st = pio_ioctl(spi, PIO_IOCTL_SPI_XFER, &x);
    if (st == PIO_OK) {
        LOG_I("pio: /SPI2/ JEDEC %02X %02X %02X", id[0], id[1], id[2]);
    } else {
        LOG_W("pio: /SPI2/ xfer: %s", pio_status_str(st));
    }
}

static void demo_i2c(void)
{
    pio_handle_t i2c = pio_open("/I2C1/", 0U);
    if (i2c == NULL) {
        LOG_E("pio: /I2C1/ not in the device table");
        return;
    }
    (void)pio_ioctl(i2c, PIO_IOCTL_I2C_SET_SLAVE_ADDR, (void *)(uintptr_t)PIO_DEMO_I2C_ADDR);
    uint8_t raw[2];
    pio_status_t st = PIO_OK;
    size_t n = pio_read(i2c, raw, sizeof raw);
    (void)pio_ioctl(i2c, PIO_IOCTL_GET_LAST_STATUS, &st);
    if (n == sizeof raw) {
        LOG_I("pio: /I2C1/ 0x%02X -> %02X %02X", (unsigned)PIO_DEMO_I2C_ADDR, raw[0], raw[1]);
    } else {
        LOG_I("pio: /I2C1/ read: %s", pio_status_str(st));
    }
}

static void demo_uart(void)
{
    pio_handle_t u = pio_open("/UART1/", 0U);
    static const char msg[] = "hello\r\n";
    pio_status_t st = PIO_OK;
    if (u != NULL && pio_write(u, msg, sizeof msg - 1U) == 0U) {
        (void)pio_ioctl(u, PIO_IOCTL_GET_LAST_STATUS, &st);
        LOG_I("pio: /UART1/ write: %s (TODO)", pio_status_str(st));
    }
}

static void pio_task(void *arg)
{
    (void)arg;
    bool first = true;
    for (;;) {
        demo_spi();
        demo_i2c();
        if (first) {
            demo_uart();                             /* once: the answer does not change */
            first = false;
        }
        for (uint32_t t = 0U; t < PIO_DEMO_PERIOD_MS; t += 500U) {
            sup_checkin(sup_id);
            vTaskDelay(pdMS_TO_TICKS(500U));
        }
    }
}

void demo_start(void)
{
    sup_id = sup_register("pio", 3000U);
    TaskHandle_t h = xTaskCreateStatic(pio_task, "pio", DEMO_TASK_STACK_WORDS, NULL,
                                       tskIDLE_PRIORITY + DEMO_TASK_PRIORITY, task_stack, &task_tcb);
    configASSERT(h != NULL);
}
