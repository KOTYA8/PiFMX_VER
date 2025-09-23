#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#include "rds_strings.h"
#include "waveforms.h"

#define RT_LENGTH 64
#define PS_LENGTH 8
#define GROUP_LENGTH 4

struct {
    uint16_t pi;
    int ta;
    int tp;
    int ms;
    uint8_t di_flags;
    char ps[PS_LENGTH];
    char rt[RT_LENGTH];
    uint8_t pty;
    uint8_t ecc;
    int ecc_enabled;
    uint8_t lic;
    int lic_enabled;
    uint8_t pin_day;
    uint8_t pin_hour;
    uint8_t pin_minute;
    int pin_enabled;
} rds_params = {
    .pi = 0x1234, .ta = 0, .tp = 0, .ms = 1, .di_flags = 0,
    .ps = {0}, .rt = {0}, .pty = 0,
    .ecc = 0, .ecc_enabled = 0,
    .lic = 0, .lic_enabled = 0,
    .pin_day = 0, .pin_hour = 0, .pin_minute = 0, .pin_enabled = 0
};

/* The RDS error-detection code generator polynomial is
   x^10 + x^8 + x^7 + x^5 + x^4 + x^3 + x^0
*/
#define POLY 0x1B9
#define POLY_DEG 10
#define MSB_BIT 0x8000
#define BLOCK_SIZE 16

#define BITS_PER_GROUP (GROUP_LENGTH * (BLOCK_SIZE+POLY_DEG))
#define SAMPLES_PER_BIT 192
#define FILTER_SIZE (sizeof(waveform_biphase)/sizeof(float))
#define SAMPLE_BUFFER_SIZE (SAMPLES_PER_BIT + FILTER_SIZE)


uint16_t offset_words[] = {0x0FC, 0x198, 0x168, 0x1B4};

/* Classical CRC computation */
uint16_t crc(uint16_t block) {
    uint16_t crc = 0;
    for(int j=0; j<BLOCK_SIZE; j++) {
        int bit = (block & MSB_BIT) != 0;
        block <<= 1;
        int msb = (crc >> (POLY_DEG-1)) & 1;
        crc <<= 1;
        if((msb ^ bit) != 0) {
            crc = crc ^ POLY;
        }
    }
    return crc;
}

/* Possibly generates a CT (clock time) group if the minute has just changed
   Returns 1 if the CT group was generated, 0 otherwise
*/
int get_rds_ct_group(uint16_t *blocks) {
    static int latest_minutes = -1;
    time_t now;
    struct tm *utc;
    now = time (NULL);
    utc = gmtime (&now);

    if(utc->tm_min != latest_minutes) {
        latest_minutes = utc->tm_min;
        int l = utc->tm_mon <= 1 ? 1 : 0;
        int mjd = 14956 + utc->tm_mday +
                        (int)((utc->tm_year - l) * 365.25) +
                        (int)((utc->tm_mon + 2 + l*12) * 30.6001);
        blocks[1] = 0x4400 | (mjd>>15);
        blocks[2] = (mjd<<1) | (utc->tm_hour>>4);
        blocks[3] = (utc->tm_hour & 0xF)<<12 | utc->tm_min<<6;
        utc = localtime(&now);
        int offset = utc->tm_gmtoff / (30 * 60);
        blocks[3] |= abs(offset);
        if(offset < 0) blocks[3] |= 0x20;
        return 1;
    } else return 0;
}


void get_rds_group(int *buffer) {
    static int state = 0;
    static int ps_state = 0;
    static int rt_state = 0;
    static int group_1a_cycle_idx = 0;
    uint16_t blocks[GROUP_LENGTH] = {rds_params.pi, 0, 0, 0};

    if (get_rds_ct_group(blocks)) {
        // Группа CT (время) имеет приоритет и была отправлена.
    } else {
        int group_1A_sent = 0;
        // Слот state == 3 зарезервирован для отправки групп типа 1A
        if (state == 3) {
            char enabled_1a_types[4];
            int num_enabled = 0;
            // Собираем список активных функций для циклической отправки
            if (rds_params.ecc_enabled) enabled_1a_types[num_enabled++] = 'E';
            if (rds_params.lic_enabled) enabled_1a_types[num_enabled++] = 'L';
            // PIN сам по себе не требует циклической отправки, но его данные используются всегда.
            // Добавляем 'P' в цикл только если нет других активных функций 1A.
            if (rds_params.pin_enabled && num_enabled == 0) enabled_1a_types[num_enabled++] = 'P';


            if (num_enabled > 0) {
                group_1a_cycle_idx %= num_enabled;
                char type_to_send = enabled_1a_types[group_1a_cycle_idx];
                
                uint16_t block1_base = (rds_params.tp ? 0x0400 : 0) | (rds_params.pty << 5);
                blocks[1] = 0x1000 | block1_base; // Базовый Блок B для группы 1A

                // ИСПРАВЛЕНИЕ: Блок D ВСЕГДА содержит данные PIN для любой группы 1A.
                if (rds_params.pin_enabled) {
                    blocks[3] = (rds_params.pin_day << 11) | (rds_params.pin_hour << 6) | rds_params.pin_minute;
                } else {
                    blocks[3] = 0x0000; // Если PIN не активен, отправляем невалидный PIN.
                }

                switch (type_to_send) {
                    case 'E': // Extended Country Code
                        blocks[2] = (0b0000 << 12) | rds_params.ecc;
                        break;
                    case 'L': // Language Identification Code
                        blocks[2] = (0b0011 << 12) | rds_params.lic;
                        break;
                    case 'P': // Programme Item Number (когда только он активен)
                        blocks[2] = rds_params.pi;
                        break;
                }
                
                group_1A_sent = 1;
                group_1a_cycle_idx++;
            }
        }

        if (!group_1A_sent) {
            uint16_t block1_base_other = (rds_params.tp ? 0x0400 : 0) | (rds_params.pty << 5);
            if (state == 4) { // Группа 2A (RadioText)
                blocks[1] = 0x2000 | block1_base_other | (rds_params.ta ? 0x10 : 0) | rt_state;
                blocks[2] = rds_params.rt[rt_state*4+0]<<8 | rds_params.rt[rt_state*4+1];
                blocks[3] = rds_params.rt[rt_state*4+2]<<8 | rds_params.rt[rt_state*4+3];
                rt_state = (rt_state + 1) % 16;
            } else { // Группа 0A (PS Name)
                uint8_t di_bit = 0;
                switch (ps_state) {
                    case 0: if (rds_params.di_flags & 8) di_bit = 1; break;
                    case 1: if (rds_params.di_flags & 4) di_bit = 1; break;
                    case 2: if (rds_params.di_flags & 2) di_bit = 1; break;
                    case 3: if (rds_params.di_flags & 1) di_bit = 1; break;
                }
                blocks[1] = block1_base_other | (rds_params.ta ? 0x10 : 0) | (rds_params.ms ? 0x08 : 0) | (di_bit << 2) | ps_state;
                blocks[3] = rds_params.ps[ps_state*2]<<8 | rds_params.ps[ps_state*2+1];
                ps_state = (ps_state + 1) % 4;
            }
        }
        
        state = (state + 1) % 5;
    }

    // Расчет CRC и формирование битстрима (этот блок остается без изменений)
    for (int i=0; i<GROUP_LENGTH; i++) {
        uint16_t block = blocks[i];
        uint16_t check = crc(block) ^ offset_words[i];
        for (int j=0; j<BLOCK_SIZE; j++) {
            *buffer++ = ((block & (1<<(BLOCK_SIZE-1))) != 0);
            block <<= 1;
        }
        for (int j=0; j<POLY_DEG; j++) {
            *buffer++= ((check & (1<<(POLY_DEG-1))) != 0);
            check <<= 1;
        }
    }
}

/* Get a number of RDS samples... (rest of the file is unchanged) */
void get_rds_samples(float *buffer, int count) {
    static int bit_buffer[BITS_PER_GROUP];
    static int bit_pos = BITS_PER_GROUP;
    static float sample_buffer[SAMPLE_BUFFER_SIZE] = {0};
    static int prev_output = 0;
    static int cur_output = 0;
    static int cur_bit = 0;
    static int sample_count = SAMPLES_PER_BIT;
    static int inverting = 0;
    static int phase = 0;
    static int in_sample_index = 0;
    static int out_sample_index = SAMPLE_BUFFER_SIZE-1;
    for(int i=0; i<count; i++) {
        if(sample_count >= SAMPLES_PER_BIT) {
            if(bit_pos >= BITS_PER_GROUP) {
                get_rds_group(bit_buffer);
                bit_pos = 0;
            }
            cur_bit = bit_buffer[bit_pos];
            prev_output = cur_output;
            cur_output = prev_output ^ cur_bit;
            inverting = (cur_output == 1);
            float *src = waveform_biphase;
            int idx = in_sample_index;
            for(int j=0; j<FILTER_SIZE; j++) {
                float val = (*src++);
                if(inverting) val = -val;
                sample_buffer[idx++] += val;
                if(idx >= SAMPLE_BUFFER_SIZE) idx = 0;
            }
            in_sample_index += SAMPLES_PER_BIT;
            if(in_sample_index >= SAMPLE_BUFFER_SIZE) in_sample_index -= SAMPLE_BUFFER_SIZE;
            bit_pos++;
            sample_count = 0;
        }
        float sample = sample_buffer[out_sample_index];
        sample_buffer[out_sample_index] = 0;
        out_sample_index++;
        if(out_sample_index >= SAMPLE_BUFFER_SIZE) out_sample_index = 0;
        switch(phase) {
            case 0: case 2: sample = 0; break;
            case 1: break;
            case 3: sample = -sample; break;
        }
        phase = (phase + 1) % 4;
        *buffer++ = sample;
        sample_count++;
    }
}

void set_rds_pi(uint16_t pi_code) {
    rds_params.pi = pi_code;
}

void set_rds_rt(char *rt) {
    fill_rds_string(rds_params.rt, rt, 64);
}

void set_rds_ps(char *ps) {
    fill_rds_string(rds_params.ps, ps, 8);
}

void set_rds_ta(int ta) {
    rds_params.ta = ta;
}

void set_rds_tp(int tp) {
    rds_params.tp = tp;
}

void set_rds_ms(int ms) {
    rds_params.ms = ms;
}

void set_rds_pty(uint8_t pty_code) {
    rds_params.pty = pty_code;
}

void set_rds_ecc(uint8_t ecc_code) {
    rds_params.ecc = ecc_code;
    rds_params.ecc_enabled = 1;
}

void set_rds_lic(uint8_t lic_code) {
    rds_params.lic = lic_code;
    rds_params.lic_enabled = 1;
}

void set_rds_pin(uint8_t day, uint8_t hour, uint8_t minute) {
    rds_params.pin_day = day;
    rds_params.pin_hour = hour;
    rds_params.pin_minute = minute;
    rds_params.pin_enabled = 1;
}

void set_rds_di(uint8_t flags) {
    rds_params.di_flags = flags;
}

uint16_t get_rds_pi() {
    return rds_params.pi;
}
uint8_t get_rds_pty() {
    return rds_params.pty;
}
int get_rds_tp() {
    return rds_params.tp;
}
int get_rds_ta() {
    return rds_params.ta;
}
int get_rds_ms() {
    return rds_params.ms;
}
uint8_t get_rds_ecc() {
    return rds_params.ecc;
}

uint8_t get_rds_di() {
    return rds_params.di_flags;
}
