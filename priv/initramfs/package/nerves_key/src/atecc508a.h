/*
   Copyright 2020 Nerves Project Developers

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef ATECC508A_H
#define ATECC508A_H

#include <stdint.h>

#define ATECC508A_DEFAULT_ADDR 0x60

#define ATECC508A_ZONE_CONFIG 0
#define ATECC508A_ZONE_OTP    1
#define ATECC508A_ZONE_DATA   2

struct atecc508a_session {
    int fd;
    uint8_t addr;
};

int atecc508a_open(const char *filename, uint8_t i2c_address, struct atecc508a_session *session);
void atecc508a_close(struct atecc508a_session *session);
int atecc508a_wakeup(const struct atecc508a_session *session);
int atecc508a_sleep(const struct atecc508a_session *session);
int atecc508a_read_serial(const struct atecc508a_session *session, uint8_t *serial_number);
int atecc508a_derive_public_key(const struct atecc508a_session *session, uint8_t slot, uint8_t *key);
int atecc508a_sign(const struct atecc508a_session *session, uint8_t slot, const uint8_t *data, uint8_t *signature);
int atecc508a_read_zone_nowake(const struct atecc508a_session *session, uint8_t zone, uint16_t slot, uint8_t block, uint8_t offset, uint8_t *data, uint8_t len);

#endif // ATECC508A_H