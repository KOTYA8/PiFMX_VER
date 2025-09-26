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

enum ct_mode { CT_SYSTEM, CT_CUSTOM_TICKING, CT_CUSTOM_STATIC };

typedef struct {
    uint8_t content_type;
    uint8_t start_marker;
    uint8_t length_marker;
    int enabled;
} rds_rtp_tag;

struct {
    uint16_t pi;
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
} rds_params = {
    .pi = 0x1234, .ta = 0, .tp = 0, .ms = 1, .di_flags = 0,
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
    .ct_mode = CT_SYSTEM
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
    uint16_t blocks[GROUP_LENGTH] = {rds_params.pi, 0, 0, 0};

    if (get_rds_ct_group(blocks)) {
        // Группа CT (время) имеет приоритет и была отправлена.
    } else {
        uint16_t block1_base_other = (rds_params.tp ? 0x0400 : 0) | (rds_params.pty << 5);
        int group_sent = 0;

        // Логика генерации групп RT+
        if (rds_params.rtp_enabled && (rds_params.tags[0].enabled || rds_params.tags[1].enabled)) {
            // Сначала формируем полную 37-битную полезную нагрузку для RT+
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

            // Извлекаем первые 5 бит нагрузки - это будет наш "код приложения"
            uint8_t app_code = (payload >> 32) & 0x1F;

            if (state == 6) { // Состояние 6 -> Группа 3A (Анонс ODA для RT+)
                blocks[1] = 0x3000 | block1_base_other | app_code;
                blocks[2] = 0x0000; // По данным Stereo Tool, этот блок должен быть нулевым
                blocks[3] = 0x4BD7; // AID для RT+
                group_sent = 1;
            } else if (state == 7) { // Состояние 7 -> Группа 12A (Передача тегов RT+)
                blocks[1] = 0xC000 | block1_base_other | app_code;
                blocks[2] = (payload >> 16) & 0xFFFF;
                blocks[3] = payload & 0xFFFF;
                group_sent = 1;
            }
        }

        if (!group_sent) {
            int group_1A_sent = 0;
            // Слот state == 3 зарезервирован для отправки групп типа 1A
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
                if (state == 4 || state == 5) { // Состояния 4,5 -> Группа 2A (RadioText)
                    uint8_t ab_flag = 0;
                    if (rds_params.rt_channel_mode == 1) ab_flag = 1;
                    else if (rds_params.rt_channel_mode == 2) ab_flag = rds_params.rt_ab_flag;
                    blocks[1] = 0x2000 | block1_base_other | (ab_flag << 4) | rt_state;
                    blocks[2] = rds_params.rt[rt_state*4+0]<<8 | rds_params.rt[rt_state*4+1];
                    blocks[3] = rds_params.rt[rt_state*4+2]<<8 | rds_params.rt[rt_state*4+3];
                    rt_state = (rt_state + 1) % 16;
                } else if (rds_params.ptyn_enabled && state == 1) { // Состояние 1 -> PTYN Сегмент 0
                    blocks[1] = 0xA000 | block1_base_other | 0;
                    blocks[2] = rds_params.ptyn[0*4+0]<<8 | rds_params.ptyn[0*4+1];
                    blocks[3] = rds_params.ptyn[0*4+2]<<8 | rds_params.ptyn[0*4+3];
                } else if (rds_params.ptyn_enabled && rds_params.ptyn_second_segment_exists && state == 2) { // Состояние 2 -> PTYN Сегмент 1
                    blocks[1] = 0xA000 | block1_base_other | 1;
                    blocks[2] = rds_params.ptyn[1*4+0]<<8 | rds_params.ptyn[1*4+1];
                    blocks[3] = rds_params.ptyn[1*4+2]<<8 | rds_params.ptyn[1*4+3];
                } else { // Все остальные состояния -> Группа 0A (PS)
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
        }
        
        state = (state + 1) % 8; // У нас 8 состояний
    }

    // Расчет CRC и формирование битстрима
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

void set_rds_rt_mode(char mode) {
    if (mode == 'P' || mode == 'A' || mode == 'D') {
        rds_params.rt_mode = mode;
        // Переформатируем существующий текст с новым режимом
        fill_rds_string_mode(rds_params.rt, rds_params.original_rt, RT_LENGTH, rds_params.rt_mode);
    }
}

void set_rds_pi(uint16_t pi_code) {
    rds_params.pi = pi_code;
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