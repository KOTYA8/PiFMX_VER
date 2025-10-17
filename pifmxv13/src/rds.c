#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

#include "rds_strings.h"
#include "waveforms.h"

#define RT_LENGTH 64
#define PS_LENGTH 8
#define GROUP_LENGTH 4
#define MAX_AF_FREQUENCIES 25

const uint16_t cyclic_pi_sequence[] = {0xA121, 0x2121, 0x012A, 0xA120, 0x012F, 0x0128, 0x0129, 0xBEEF};
const int cyclic_pi_sequence_size = sizeof(cyclic_pi_sequence) / sizeof(uint16_t);

enum ct_mode { CT_SYSTEM, CT_CUSTOM_TICKING, CT_CUSTOM_STATIC };

typedef struct {
    uint8_t content_type;
    uint8_t start_marker;
    uint8_t length_marker;
    int enabled;
} rds_rtp_tag;

struct {
    uint16_t pi;
    uint16_t original_pi;
    int pi_cyclic_mode;     // <-- Флаг для -pio
    int pi_random_mode;     // <-- Флаг для -rds-bug
    int buggy_pi_index;
    int ta;
    int tp;
    int ms;
    uint8_t di_flags;
    char ps[PS_LENGTH];
    char rt[RT_LENGTH];
    char original_rt[RT_LENGTH];
    char ptyn[PS_LENGTH];
    uint8_t pty;
    uint8_t ecc;
    int ecc_enabled;
    uint8_t lic;
    int lic_enabled;
    uint8_t pin_day;
    uint8_t pin_hour;
    uint8_t pin_minute;
    int pin_enabled;
    int ptyn_enabled;
    int ptyn_second_segment_exists;
    int rt_channel_mode;
    int rt_ab_flag;
    rds_rtp_tag tags[2];
    int rtp_enabled;
    uint8_t rtp_item_toggle_bit;
    uint8_t rtp_item_running_bit;
    char rt_mode;
    int ct_enabled;
    int ct_offset_minutes;
    enum ct_mode ct_mode;
    struct tm custom_tm;
    time_t custom_time_start_t;
    time_t real_time_at_set_t;
    uint8_t af_list_to_send[MAX_AF_FREQUENCIES + 2]; // +2 для кода количества и заполнителя
    int af_list_size;
    int af_count;
    int af_current_pair_index;
    uint8_t afb_list[256]; // Увеличенный буфер для всех пар
    int afb_list_size;
    int afb_current_pair_index;
    int ps_enabled;
    int rt_enabled;
} rds_params = {
    .pi = 0x1234, .original_pi = 0x1234, .pi_cyclic_mode = 0, .pi_random_mode = 0, .buggy_pi_index = 0, .ta = 0, .tp = 0, .ms = 1, .di_flags = 0,
    .ps = {0}, .rt = {0}, .original_rt = {0}, .ptyn = {0}, .pty = 0,
    .ecc = 0, .ecc_enabled = 0,
    .lic = 0, .lic_enabled = 0,
    .pin_day = 0, .pin_hour = 0, .pin_minute = 0, .pin_enabled = 0,
    .ptyn_enabled = 0,
    .ptyn_second_segment_exists = 0,
    .rt_channel_mode = 0,
    .rt_ab_flag = 0,
    .tags = {{0,0,0,0}, {0,0,0,0}},
    .rtp_enabled = 0,
    .rtp_item_toggle_bit = 0,
    .rtp_item_running_bit = 0,
    .rt_mode = 'P',
    .ct_enabled = 1,
    .ct_offset_minutes = 0,
    .ct_mode = CT_SYSTEM,
    .af_list_size = 0,
    .af_count = 0,
    .af_current_pair_index = 0,
    .afb_list = {0},
    .afb_list_size = 0,
    .afb_current_pair_index = 0,
    .ps_enabled = 1,
    .rt_enabled = 1
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

uint8_t freq_to_code(float freq_mhz) {
    if (freq_mhz < 87.5 || freq_mhz > 108.0) {
        return 255; // Возвращаем специальное значение для ошибки
    }
    return (uint8_t)roundf((freq_mhz - 87.5f) * 10.0f);
}

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
    static int cts_counter = 0; // Счетчик для периодической отправки CTS
    time_t now_t;
    struct tm *time_info;

    if (!rds_params.ct_enabled) {
        return 0;
    }

    switch (rds_params.ct_mode) {
        case CT_SYSTEM:
            now_t = time(NULL);
            time_info = gmtime(&now_t);
            break;
        case CT_CUSTOM_TICKING:
            now_t = rds_params.custom_time_start_t + (time(NULL) - rds_params.real_time_at_set_t);
            time_info = gmtime(&now_t);
            break;
        case CT_CUSTOM_STATIC:
            cts_counter = (cts_counter + 1) % 16;
            if (cts_counter != 1) {
                return 0;
            }
            time_info = &rds_params.custom_tm;
            break;
        default:
            return 0;
    }

    if (time_info->tm_min != latest_minutes || rds_params.ct_mode == CT_CUSTOM_STATIC) {
        latest_minutes = time_info->tm_min;

        int l = time_info->tm_mon < 2 ? 1 : 0;
        int mjd = 14956 + time_info->tm_mday +
                        (int)((time_info->tm_year - l) * 365.25) +
                        (int)((time_info->tm_mon + 2 + l * 12) * 30.6001);

        blocks[1] = 0x4000 | (rds_params.tp ? 0x0400 : 0) | (rds_params.pty << 5) | (mjd >> 15); // <-- ИЗМЕНЕНА ЭТА СТРОКА
        blocks[2] = (mjd << 1) | (time_info->tm_hour >> 4);
        blocks[3] = (time_info->tm_hour & 0xF) << 12 | time_info->tm_min << 6;

        if (rds_params.ct_mode == CT_SYSTEM) {
            struct tm *local_tm = localtime(&now_t);
            int total_offset_minutes = (local_tm->tm_gmtoff / 60) + rds_params.ct_offset_minutes;
            int offset_sign = (total_offset_minutes < 0) ? 1 : 0;
            int offset_val_abs = abs(total_offset_minutes);
            int offset_code = (offset_val_abs / 30);
            blocks[3] |= offset_code & 0x1F;
            if (offset_sign) blocks[3] |= 0x20;
        }

        return 1;
    } else {
        return 0;
    }
}

void get_rds_group(int *buffer) {
    static int state = 0;
    static int ps_state = 0;
    static int rt_state = 0;
    static int group_1a_cycle_idx = 0;
    static int af_toggle = 0;
    uint16_t blocks[GROUP_LENGTH] = {rds_params.pi, 0, 0, 0};

    // --- НАША НОВАЯ, ЧИСТАЯ ЛОГИКА ---
    if (rds_params.pi_random_mode) {
        // Режим -rds-bug: полностью случайный PI
        rds_params.pi = (rand() % 0xFFFE) + 1;
    } else if (rds_params.pi_cyclic_mode) {
        // Режим -pio: циклическая смена PI из последовательности
        rds_params.pi = cyclic_pi_sequence[rds_params.buggy_pi_index];
        rds_params.buggy_pi_index = (rds_params.buggy_pi_index + 1) % cyclic_pi_sequence_size;
    }
    // Присваиваем измененный PI первому блоку
    blocks[0] = rds_params.pi;
    // ------------------------------------

    if (get_rds_ct_group(blocks)) {
        // Группа CT (время) имеет приоритет и была отправлена.
    } else {
        uint16_t block1_base_other = (rds_params.tp ? 0x0400 : 0) | (rds_params.pty << 5);
        int group_sent = 0;

        // Логика генерации групп RT+
        if (rds_params.rtp_enabled && (rds_params.tags[0].enabled || rds_params.tags[1].enabled)) {
            uint64_t payload = 0;
            rds_rtp_tag tag1 = rds_params.tags[0];
            rds_rtp_tag tag2 = rds_params.tags[1];

            payload |= (uint64_t)(rds_params.rtp_item_toggle_bit & 1) << 36;
            payload |= (uint64_t)(rds_params.rtp_item_running_bit & 1) << 35;
            if (tag1.enabled) {
                payload |= (uint64_t)(tag1.content_type & 0x3F) << 29;
                payload |= (uint64_t)(tag1.start_marker & 0x3F) << 23;
                payload |= (uint64_t)(tag1.length_marker & 0x3F) << 17;
            }
            if (tag2.enabled) {
                payload |= (uint64_t)(tag2.content_type & 0x3F) << 11;
                payload |= (uint64_t)(tag2.start_marker & 0x3F) << 5;
                payload |= (uint64_t)(tag2.length_marker & 0x1F);
            }

            uint8_t app_code = (payload >> 32) & 0x1F;

            if (state == 6) { // Группа 3A (Анонс ODA для RT+)
                blocks[1] = 0x3000 | block1_base_other | app_code;
                blocks[2] = 0x0000;
                blocks[3] = 0x4BD7; // AID для RT+
                group_sent = 1;
            } else if (state == 7) { // Группа 12A (Передача тегов RT+)
                blocks[1] = 0xC000 | block1_base_other | app_code;
                blocks[2] = (payload >> 16) & 0xFFFF;
                blocks[3] = payload & 0xFFFF;
                group_sent = 1;
            }
        }

        if (!group_sent) {
            int group_1A_sent = 0;
            if (state == 3) {
                char enabled_1a_types[4];
                int num_enabled = 0;
                if (rds_params.ecc_enabled) enabled_1a_types[num_enabled++] = 'E';
                if (rds_params.lic_enabled) enabled_1a_types[num_enabled++] = 'L';
                if (rds_params.pin_enabled && num_enabled == 0) enabled_1a_types[num_enabled++] = 'P';

                if (num_enabled > 0) {
                    group_1a_cycle_idx %= num_enabled;
                    char type_to_send = enabled_1a_types[group_1a_cycle_idx];
                    uint16_t block1_base = (rds_params.tp ? 0x0400 : 0) | (rds_params.pty << 5);
                    blocks[1] = 0x1000 | block1_base;
                    if (rds_params.pin_enabled) {
                        blocks[3] = (rds_params.pin_day << 11) | (rds_params.pin_hour << 6) | rds_params.pin_minute;
                    } else {
                        blocks[3] = 0x0000;
                    }
                    switch (type_to_send) {
                        case 'E': blocks[2] = (0b0000 << 12) | rds_params.ecc; break;
                        case 'L': blocks[2] = (0b0011 << 12) | rds_params.lic; break;
                        case 'P': blocks[2] = rds_params.pi; break;
                    }
                    group_1A_sent = 1;
                    group_1a_cycle_idx++;
                }
            }

            if (!group_1A_sent) {
                if ((state == 4 || state == 5) && rds_params.rt_enabled) { // Группа 2A (RadioText)
                    uint8_t ab_flag = 0;
                    if (rds_params.rt_channel_mode == 1) ab_flag = 1;
                    else if (rds_params.rt_channel_mode == 2) ab_flag = rds_params.rt_ab_flag;
                    blocks[1] = 0x2000 | block1_base_other | (ab_flag << 4) | rt_state;
                    blocks[2] = rds_params.rt[rt_state*4+0]<<8 | rds_params.rt[rt_state*4+1];
                    blocks[3] = rds_params.rt[rt_state*4+2]<<8 | rds_params.rt[rt_state*4+3];
                    rt_state = (rt_state + 1) % 16;
                } else if (rds_params.ptyn_enabled && state == 1) { // PTYN Сегмент 0
                    blocks[1] = 0xA000 | block1_base_other | 0;
                    blocks[2] = rds_params.ptyn[0*4+0]<<8 | rds_params.ptyn[0*4+1];
                    blocks[3] = rds_params.ptyn[0*4+2]<<8 | rds_params.ptyn[0*4+3];
                } else if (rds_params.ptyn_enabled && rds_params.ptyn_second_segment_exists && state == 2) { // PTYN Сегмент 1
                    blocks[1] = 0xA000 | block1_base_other | 1;
                    blocks[2] = rds_params.ptyn[1*4+0]<<8 | rds_params.ptyn[1*4+1];
                    blocks[3] = rds_params.ptyn[1*4+2]<<8 | rds_params.ptyn[1*4+3];
                } else { // Группа 0A (PS и AF)
                    uint8_t di_bit = 0;
                    switch (ps_state) {
                        case 0: if (rds_params.di_flags & 8) di_bit = 1; break;
                        case 1: if (rds_params.di_flags & 4) di_bit = 1; break;
                        case 2: if (rds_params.di_flags & 2) di_bit = 1; break;
                        case 3: if (rds_params.di_flags & 1) di_bit = 1; break;
                    }
                    blocks[1] = block1_base_other | (rds_params.ta ? 0x10 : 0) | (rds_params.ms ? 0x08 : 0) | (di_bit << 2) | ps_state;

                    int af_sent_this_cycle = 0;
                    
                    if (af_toggle == 1 && rds_params.afb_list_size > 0) {
                        // Отправляем AFB
                        int num_pairs = rds_params.afb_list_size / 2;
                        if (num_pairs > 0) {
                            int pair_index = rds_params.afb_current_pair_index;
                            blocks[2] = (rds_params.afb_list[pair_index * 2] << 8) | rds_params.afb_list[pair_index * 2 + 1];
                            rds_params.afb_current_pair_index = (pair_index + 1) % num_pairs;
                            af_sent_this_cycle = 1;
                        }
                        if (rds_params.af_list_size > 0) af_toggle = 0; // В следующий раз отправляем AFA
                    } else if (rds_params.af_list_size > 0) {
                        // Отправляем AFA
                        int num_pairs = rds_params.af_list_size / 2;
                         if (num_pairs > 0) {
                            int pair_index = rds_params.af_current_pair_index;
                            blocks[2] = (rds_params.af_list_to_send[pair_index * 2] << 8) | rds_params.af_list_to_send[pair_index * 2 + 1];
                            rds_params.af_current_pair_index = (pair_index + 1) % num_pairs;
                            af_sent_this_cycle = 1;
                        }
                        if (rds_params.afb_list_size > 0) af_toggle = 1; // В следующий раз отправляем AFB
                    }

                    if (!af_sent_this_cycle) {
                         blocks[2] = rds_params.pi;
                    }

                    if (rds_params.ps_enabled) {
                        blocks[3] = rds_params.ps[ps_state*2]<<8 | rds_params.ps[ps_state*2+1];
                    } else {
                        blocks[3] = ' '<<8 | ' ';
                    }
                    ps_state = (ps_state + 1) % 4;
                }
            }
        }

        state = (state + 1) % 8;
    }

    // Расчет CRC и формирование битстрима
    for (int i=0; i<GROUP_LENGTH; i++) {
        uint16_t block = blocks[i];
        uint16_t check = crc(block) ^ offset_words[i];
        if (rds_params.pi_cyclic_mode && i == 0) {
            check = check ^ 0x0001; // Инвертируем последний бит CRC
        }
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

int set_rds_af(char* af_list_str) {
    rds_params.af_count = 0;
    rds_params.af_list_size = 0;
    rds_params.af_current_pair_index = 0;

    if (strcmp(af_list_str, "0") == 0) {
        rds_params.af_list_to_send[0] = 224;
        rds_params.af_list_to_send[1] = 205; // Filler
        rds_params.af_list_size = 2;
        return 1;
    }

    uint8_t temp_freq_codes[MAX_AF_FREQUENCIES];
    char* str = strdup(af_list_str);
    char* to_free = str;
    char* token;

    while ((token = strsep(&str, " ,")) != NULL && rds_params.af_count < MAX_AF_FREQUENCIES) {
        if (strlen(token) == 0) continue;
        float freq = atof(token);
        if (freq == 0) continue;
        uint8_t code = freq_to_code(freq);
        if (code != 255) {
            temp_freq_codes[rds_params.af_count++] = code;
        } else {
            fprintf(stderr, "Error: Invalid or out-of-range AF frequency: %s.\n", token);
            free(to_free);
            return 0;
        }
    }
    free(to_free);

    // FIX: Если частота всего одна, дублируем её для лучшей совместимости
    if (rds_params.af_count == 1) {
        temp_freq_codes[1] = temp_freq_codes[0];
        rds_params.af_count = 2;
    }

    rds_params.af_list_to_send[0] = 224 + rds_params.af_count;
    memcpy(&rds_params.af_list_to_send[1], temp_freq_codes, rds_params.af_count);
    rds_params.af_list_size = 1 + rds_params.af_count;

    if (rds_params.af_list_size % 2 != 0) {
        rds_params.af_list_to_send[rds_params.af_list_size++] = 205;
    }

    return 1;
}

int set_rds_af_from_file(int afaf) {
    if (afaf == 0) {
        rds_params.af_count = 0;
        rds_params.af_list_size = 0;
        rds_params.af_current_pair_index = 0;
        // Устанавливаем код "No AF exists"
        rds_params.af_list_to_send[0] = 224;
        rds_params.af_list_size = 1;
        return 1;
    }

    // Пробуем найти файл по разным путям
    const char* paths_to_try[] = {
        "rds/afa.txt",       // Если запуск из папки src/
        "src/rds/afa.txt",   // Если запуск из корня проекта
        "afa.txt"            // Если файл лежит рядом с исполняемым
    };
    FILE* f = NULL;
    char found_path[256] = {0};

    for (int i = 0; i < 3; i++) {
        f = fopen(paths_to_try[i], "r");
        if (f) {
            strncpy(found_path, paths_to_try[i], sizeof(found_path) - 1);
            break;
        }
    }

    if (!f) {
        perror("Error: Could not find afa.txt in default locations (rds/afa.txt, src/rds/afa.txt, afa.txt)");
        return 0;
    }

    printf("Reading AF list from: %s\n", found_path);

    char line[256];
    char all_freqs[1024] = {0};
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        strcat(all_freqs, line);
        strcat(all_freqs, " ");
    }
    fclose(f);

    return set_rds_af(all_freqs);
}

void set_rds_rt_mode(char mode) {
    if (mode == 'P' || mode == 'A' || mode == 'D') {
        rds_params.rt_mode = mode;
        // Переформатируем существующий текст с новым режимом
        fill_rds_string_mode(rds_params.rt, rds_params.original_rt, RT_LENGTH, rds_params.rt_mode);
    }
}

void set_rds_pi(uint16_t pi_code) {
    rds_params.pi = pi_code;
    // Сохраняем код как "оригинальный", если он не нулевой.
    // Это позволит нам восстановить его командой PION.
    if (pi_code != 0x0000) {
        rds_params.original_pi = pi_code;
    }
}

void set_rds_ct(int ct) {
    rds_params.ct_enabled = ct;
}

void set_rds_ctz(int offset_minutes) {
    rds_params.ct_offset_minutes = offset_minutes;
}

void set_rds_cts(int hour, int minute, int day, int month, int year) {
    rds_params.ct_mode = CT_CUSTOM_STATIC;
    rds_params.custom_tm.tm_hour = hour;
    rds_params.custom_tm.tm_min = minute;
    rds_params.custom_tm.tm_mday = day;
    rds_params.custom_tm.tm_mon = month - 1;
    rds_params.custom_tm.tm_year = year - 1900;
    rds_params.custom_tm.tm_isdst = -1; // Let mktime decide
}

void set_rds_ctc(int hour, int minute, int day, int month, int year) {
    set_rds_cts(hour, minute, day, month, year); // Use the same logic to fill the struct
    rds_params.ct_mode = CT_CUSTOM_TICKING;
    rds_params.real_time_at_set_t = time(NULL);
    // timegm treats the struct as UTC and converts to UTC time_t, which is correct for us.
    rds_params.custom_time_start_t = timegm(&rds_params.custom_tm);
}

void set_rds_rt(char *rt) {
    // Если включен режим AB, переключаем канал (A -> B -> A)
    if (rds_params.rt_channel_mode == 2) {
        rds_params.rt_ab_flag = !rds_params.rt_ab_flag;
    }
    // Сохраняем "чистую" версию текста
    strncpy(rds_params.original_rt, rt, RT_LENGTH - 1);
    rds_params.original_rt[RT_LENGTH - 1] = '\0'; // Гарантируем завершающий ноль

    // Форматируем текст для отправки с учётом текущего режима
    fill_rds_string_mode(rds_params.rt, rds_params.original_rt, RT_LENGTH, rds_params.rt_mode);
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

void set_rds_ptyn(char *ptyn) {
    fill_rds_string(rds_params.ptyn, ptyn, 8);
    rds_params.ptyn_enabled = 1;

    // Проверяем, есть ли во втором сегменте (символы 4-7) что-то кроме пробелов
    rds_params.ptyn_second_segment_exists = 0;
    for (int i = 4; i < 8; i++) {
        if (rds_params.ptyn[i] != ' ') {
            rds_params.ptyn_second_segment_exists = 1;
            break;
        }
    }
}

void set_rds_rt_channel(int mode) {
    if (mode >= 0 && mode <= 2) {
        rds_params.rt_channel_mode = mode;
    }
}

void reset_rds_ct() {
    rds_params.ct_mode = CT_SYSTEM;
    rds_params.ct_offset_minutes = 0;
}

void disable_rds_rtp() {
    rds_params.rtp_enabled = 0;
    // Сбрасываем теги на всякий случай
    rds_params.tags[0].enabled = 0;
    rds_params.tags[1].enabled = 0;
}

int set_rds_rtp(char *rtp_string) {
    // Временно храним теги здесь, чтобы не испортить текущие рабочие теги в случае ошибки
    rds_rtp_tag temp_tags[2] = {{0,0,0,0}, {0,0,0,0}};

    char *str = strdup(rtp_string);
    if (str == NULL) return 0; // Ошибка выделения памяти
    char *to_free = str;
    char *token;
    int tag_index = 0;

    while ((token = strsep(&str, ",")) != NULL && tag_index < 2) {
        if (strlen(token) == 0) continue; // Пропускаем пустые сегменты (например, из-за двойной запятой)

        int type, start, len;
        if (sscanf(token, "%d.%d.%d", &type, &start, &len) == 3) {
            // Проверяем диапазон
            if (type < 0 || type > 63 || start < 0 || start > 63 || len < 0 || len > 63) {
                free(to_free);
                return 0; // Ошибка: значение вне диапазона
            }
            // Сохраняем во временный массив
            temp_tags[tag_index].content_type = type;
            temp_tags[tag_index].start_marker = start;
            temp_tags[tag_index].length_marker = len;
            temp_tags[tag_index].enabled = 1;
            tag_index++;
        } else {
            // Если токен не пустой, но парсинг не удался - это ошибка формата
            free(to_free);
            return 0;
        }
    }

    free(to_free);

    if (tag_index == 0) return 0; // Не найдено ни одного корректного тега

    // Успех! Теперь применяем изменения в основной структуре параметров.
    rds_params.rtp_item_toggle_bit = !rds_params.rtp_item_toggle_bit;
    rds_params.rtp_item_running_bit = 1;

    rds_params.tags[0] = temp_tags[0];
    rds_params.tags[1] = temp_tags[1];

    // Длина второго тега ограничена 5 битами
    if (rds_params.tags[1].enabled) {
        rds_params.tags[1].length_marker &= 0x1F;
    }

    rds_params.rtp_enabled = 1;

    return 1; // Возвращаем успех
}

void disable_rds_ecc() {
    rds_params.ecc_enabled = 0;
}

void disable_rds_lic() {
    rds_params.lic_enabled = 0;
}

void disable_rds_pin() {
    rds_params.pin_enabled = 0;
}

void disable_rds_ptyn() {
    rds_params.ptyn_enabled = 0;
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

void set_rds_pi_cyclic_mode(int enabled) {
    rds_params.pi_cyclic_mode = enabled;
}

void set_rds_pi_random_mode(int enabled) {
    rds_params.pi_random_mode = enabled;
}

void set_rds_ps_enabled(int enabled) {
    rds_params.ps_enabled = enabled;
}

void set_rds_rt_enabled(int enabled) {
    rds_params.rt_enabled = enabled;
}

void set_rds_pi_null(int nullify) {
    if (nullify) {
        rds_params.pi = 0x0000;
    } else {
        rds_params.pi = rds_params.original_pi;
    }
}

int set_rds_afb(char* afb_list_str) {
    rds_params.afb_list_size = 0;
    rds_params.afb_current_pair_index = 0;

    if (strcmp(afb_list_str, "0") == 0) {
        return 1; // Выключаем
    }

    uint8_t all_pairs_list[256];
    int total_pairs = 0;

    char* str_variants = strdup(afb_list_str);
    if (!str_variants) return 0;
    char* to_free_variants = str_variants;
    char* variant_token;

    while ((variant_token = strsep(&str_variants, "|")) != NULL) {
        uint8_t af_codes_in_variant[MAX_AF_FREQUENCIES];
        uint8_t rv_flags_in_variant[MAX_AF_FREQUENCIES];
        int variant_af_count = 0;
        uint8_t tuned_freq_code = 0;

        char* str_freqs = strdup(variant_token);
        if (!str_freqs) { free(to_free_variants); return 0; }
        char* to_free_freqs = str_freqs;
        char* freq_token;
        int is_first_freq = 1;

        while ((freq_token = strsep(&str_freqs, " ,")) != NULL) {
            if (strlen(freq_token) == 0) continue;

            int is_regional = 0;
            int len = strlen(freq_token);
            if (tolower(freq_token[len - 1]) == 'r') {
                is_regional = 1;
                freq_token[len - 1] = '\0';
            }

            float freq = atof(freq_token);
            if (freq == 0) continue;
            uint8_t code = freq_to_code(freq);

            if (code == 255) {
                fprintf(stderr, "Error: Invalid AFB frequency: %s\n", freq_token);
                free(to_free_freqs); free(to_free_variants); return 0;
            }

            if (is_first_freq) {
                tuned_freq_code = code;
                is_first_freq = 0;
            } else if (variant_af_count < MAX_AF_FREQUENCIES) {
                af_codes_in_variant[variant_af_count] = code;
                rv_flags_in_variant[variant_af_count] = is_regional;
                variant_af_count++;
            }
        }
        free(to_free_freqs);

        if (tuned_freq_code != 0 && variant_af_count > 0) {
            all_pairs_list[total_pairs * 2] = 224 + variant_af_count;
            all_pairs_list[total_pairs * 2 + 1] = tuned_freq_code;
            total_pairs++;

            for (int i = 0; i < variant_af_count; i++) {
                uint8_t af_code = af_codes_in_variant[i];
                uint8_t is_rv = rv_flags_in_variant[i];
                uint8_t F1 = is_rv ? (af_code > tuned_freq_code ? af_code : tuned_freq_code) : (af_code < tuned_freq_code ? af_code : tuned_freq_code);
                uint8_t F2 = is_rv ? (af_code < tuned_freq_code ? af_code : tuned_freq_code) : (af_code > tuned_freq_code ? af_code : tuned_freq_code);
                if (F1==F2) {F1=tuned_freq_code; F2=af_code;}

                all_pairs_list[total_pairs * 2] = F1;
                all_pairs_list[total_pairs * 2 + 1] = F2;
                total_pairs++;
            }
        }
    }
    free(to_free_variants);

    if (total_pairs > 0) {
        memcpy(rds_params.afb_list, all_pairs_list, total_pairs * 2);
        rds_params.afb_list_size = total_pairs * 2;
    }

    return 1;
}

int set_rds_afb_from_file(int afbf) {
    if (afbf == 0) {
        rds_params.afb_list_size = 0;
        return 1;
    }

    const char* paths_to_try[] = {"rds/afb.txt", "src/rds/afb.txt", "afb.txt"};
    FILE* f = NULL;
    char found_path[256] = {0};
    for (int i = 0; i < 3; i++) {
        f = fopen(paths_to_try[i], "r");
        if (f) {
            strncpy(found_path, paths_to_try[i], sizeof(found_path) - 1);
            break;
        }
    }

    if (!f) {
        perror("Error: Could not find afb.txt in default locations");
        return 0;
    }
    printf("Reading AFB list from: %s\n", found_path);

    char line[1024];
    char all_freqs[2048] = {0};
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        strcat(all_freqs, line);
        strcat(all_freqs, "|");
    }
    fclose(f);

    if (strlen(all_freqs) > 0) {
        all_freqs[strlen(all_freqs) - 1] = '\0';
    }

    return set_rds_afb(all_freqs);
}