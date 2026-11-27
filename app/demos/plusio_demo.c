/*
 * SPDX-License-Identifier: MIT
 *
 * IO layer demo (make DEMO=plusio): one task that uses each part of the API next to the
 * gateway tasks, which use the same handles.
 *
 *   once:       request checks - an unknown path, a mode SPI does not offer, a baud rate
 *               out of range, an I2C address in the reserved range (all refused, logged)
 *   /UART2/     RX switched to character-queue mode (64 chars); every key typed on the
 *               ST-LINK terminal is read back and logged. TX stays with the log task.
 *   /SPI2/      every 5 s: bus mutex (zero copy), W25Q chip select, JEDEC ID (9Fh + 3
 *               bytes) - while the ENC28J60 and the storage task share the same bus
 *   /I2C1/      every 5 s: register 0 of address PIO_DEMO_I2C_ADDR (2 bytes) with
 *               IO_IOCTL_I2C_WRITE_READ; without a sensor this logs the NACK
 *   /UART1/     every 5 s: statistics of the protocol link (bytes, overruns, errors)
 * It only logs; nothing in the gateway depends on it.
 */
#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#define LOG_TAG LOG_MOD_DEMO
#include "log.h"

#include "demo.h"
#include "io.h"
#include "io_board.h"
#include "supervisor.h"

#define PIO_DEMO_PERIOD_MS   5000U
#define PIO_DEMO_KEY_WAIT_MS 500U         /* console read timeout = loop tick */
#define PIO_DEMO_KEY_QUEUE   64U
#define PIO_DEMO_I2C_ADDR    0x48U        /* placeholder: typical I2C temperature sensor address */

static StaticTask_t task_tcb;
static StackType_t  task_stack[DEMO_TASK_STACK_WORDS];
static sup_id_t     sup_id = SUP_INVALID;

static void show_checks(io_handle_t spi, io_handle_t link, io_handle_t i2c)
{
    LOG_I("pio: open /UART9/ -> %s", (io_open("/UART9/", 0U) == NULL) ? "NULL (not in the table)" : "?");
    LOG_I("pio: /SPI2/ circular buffer RX -> %s",
          io_status_str(io_ioctl(spi, IO_IOCTL_USE_CIRCULAR_BUFFER_RX, IO_VAL(64U))));
    LOG_I("pio: /UART1/ speed 300 -> %s", io_status_str(io_ioctl(link, IO_IOCTL_SET_SPEED, IO_VAL(300U))));
    LOG_I("pio: /I2C1/ address 0x03 -> %s",
          io_status_str(io_ioctl(i2c, IO_IOCTL_I2C_SET_SLAVE_ADDRESS, IO_VAL(0x03U))));
}

static void demo_spi(io_handle_t spi)
{
    static const uint8_t cmd = 0x9FU;                 /* JEDEC ID */
    uint8_t id[3] = { 0U, 0U, 0U };
    io_status_t st = io_ioctl(spi, IO_IOCTL_OBTAIN_WRITE_MUTEX, IO_VAL(50U));
    if (st != IO_OK) {
        LOG_W("pio: /SPI2/ bus mutex: %s", io_status_str(st));
        return;
    }
    st = io_ioctl(spi, IO_IOCTL_SPI_SELECT, IO_VAL(IO_SPI2_CS_W25Q));
    if (st == IO_OK && (io_write(spi, &cmd, 1U) != 1U || io_read(spi, id, sizeof id) != sizeof id)) {
        (void)io_ioctl(spi, IO_IOCTL_GET_LAST_STATUS, &st);
    }
    (void)io_ioctl(spi, IO_IOCTL_SPI_DESELECT, NULL);
    (void)io_ioctl(spi, IO_IOCTL_RELEASE_WRITE_MUTEX, NULL);
    if (st == IO_OK) {
        LOG_I("pio: /SPI2/ W25Q JEDEC %02X %02X %02X", id[0], id[1], id[2]);
    } else {
        LOG_W("pio: /SPI2/ JEDEC read: %s", io_status_str(st));
    }
}

static void demo_i2c(io_handle_t i2c)
{
    uint8_t reg = 0x00U;
    uint8_t raw[2] = { 0U, 0U };
    io_i2c_xfer_t x = { &reg, 1U, raw, sizeof raw };
    io_status_t st = io_ioctl(i2c, IO_IOCTL_I2C_SET_SLAVE_ADDRESS, IO_VAL(PIO_DEMO_I2C_ADDR));
    if (st == IO_OK) {
        st = io_ioctl(i2c, IO_IOCTL_I2C_WRITE_READ, &x);
    }
    if (st == IO_OK) {
        LOG_I("pio: /I2C1/ 0x%02X reg 0 -> %02X %02X", (unsigned)PIO_DEMO_I2C_ADDR, raw[0], raw[1]);
    } else {
        LOG_I("pio: /I2C1/ 0x%02X reg 0: %s", (unsigned)PIO_DEMO_I2C_ADDR, io_status_str(st));
    }
}

static void demo_link_stats(io_handle_t link)
{
    io_stats_t s;
    if (io_ioctl(link, IO_IOCTL_GET_STATS, &s) == IO_OK) {
        LOG_I("pio: /UART1/ rx %lu tx %lu overruns %lu errors %lu", (unsigned long)s.rx_bytes,
              (unsigned long)s.tx_bytes, (unsigned long)s.rx_overruns, (unsigned long)s.rx_errors);
    }
}

static void pio_task(void *arg)
{
    (void)arg;
    io_handle_t con  = io_open(IO_BOARD_CONSOLE_PATH, 0U);
    io_handle_t spi  = io_open("/SPI2/", 0U);
    io_handle_t i2c  = io_open("/I2C1/", 0U);
    io_handle_t link = io_open("/UART1/", 0U);

    if (con == NULL || spi == NULL || i2c == NULL || link == NULL) {
        LOG_E("pio: open failed (con %p spi %p i2c %p link %p)", (void *)con, (void *)spi, (void *)i2c,
              (void *)link);
    }
    show_checks(spi, link, i2c);
    io_status_t st = io_ioctl(con, IO_IOCTL_USE_CHAR_QUEUE_RX, IO_VAL(PIO_DEMO_KEY_QUEUE));
    (void)io_ioctl(con, IO_IOCTL_SET_RX_TIMEOUT, IO_VAL(PIO_DEMO_KEY_WAIT_MS));
    LOG_I("pio: %s RX in character-queue mode: %s - type on the console", io_path(con), io_status_str(st));

    TickType_t last = xTaskGetTickCount() - pdMS_TO_TICKS(PIO_DEMO_PERIOD_MS);
    for (;;) {
        char keys[16];
        size_t n = io_read(con, keys, sizeof keys - 1U);  /* waits up to PIO_DEMO_KEY_WAIT_MS */
        sup_checkin(sup_id);
        if (n > 0U) {
            for (size_t i = 0U; i < n; i++) {
                if (keys[i] < 0x20 || keys[i] > 0x7E) {
                    keys[i] = '.';
                }
            }
            keys[n] = '\0';
            LOG_I("pio: keys \"%s\"", keys);
        }
        if (xTaskGetTickCount() - last >= pdMS_TO_TICKS(PIO_DEMO_PERIOD_MS)) {
            last = xTaskGetTickCount();
            demo_spi(spi);
            demo_i2c(i2c);
            demo_link_stats(link);
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
