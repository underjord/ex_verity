#include <unistd.h>

#include "atecc508a.h"
#include "serial.h"
#include "util.h"

#define ATECC_SERIAL_LEN 9
#define ATECC_RETRIES 10

static size_t do_atecc_serial(char *buffer, size_t max_length, const char *i2c_filename, uint8_t i2c_address)
{
    (void) max_length;
    struct atecc508a_session atecc;
    if (atecc508a_open(i2c_filename, i2c_address, &atecc) < 0)
        return 0;

    if (atecc508a_wakeup(&atecc) < 0) {
        atecc508a_close(&atecc);
        return 0;
    }

    if (atecc508a_read_serial(&atecc, (uint8_t *) buffer) < 0) {
        atecc508a_close(&atecc);
        return 0;
    }

    atecc508a_sleep(&atecc);

    atecc508a_close(&atecc);

    return ATECC_SERIAL_LEN;
}

size_t atecc_serial(char *buffer, size_t max_length, const char *i2c_filename, uint8_t i2c_address)
{
    if (max_length < ATECC_SERIAL_LEN)
        return 0;

    int retries = ATECC_RETRIES;
    do {
        size_t rc = do_atecc_serial(buffer, max_length, i2c_filename, i2c_address);
        if (rc > 0)
            return rc;

	usleep(50000);
        retries--;
    } while (retries > 0);
    return 0;
}