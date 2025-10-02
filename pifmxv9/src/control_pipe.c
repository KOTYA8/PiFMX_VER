#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

#include "rds.h"
#include "control_pipe.h"
#include <ctype.h>

#define CTL_BUFFER_SIZE 100

FILE *f_ctl;

/*
 * Opens a file (pipe) to be used to control the RDS coder, in non-blocking mode.
 */
int open_control_pipe(char *filename) {
	int fd = open(filename, O_RDONLY);
    if(fd < 0) return -1;

	int flags;
	flags = fcntl(fd, F_GETFL, 0);
	flags |= O_NONBLOCK;
	if( fcntl(fd, F_SETFL, flags) == -1 ) return -1;

	f_ctl = fdopen(fd, "r");
	if(f_ctl == NULL) return -1;

	return 0;
}


/*
 * Polls the control file (pipe), non-blockingly, and if a command is received,
 * processes it and updates the RDS data.
 */
int poll_control_pipe() {
	static char buf[CTL_BUFFER_SIZE];

    char *res = fgets(buf, CTL_BUFFER_SIZE, f_ctl);
    if(res == NULL) return -1;

    // Убираем символ новой строки в конце
    if(res[strlen(res)-1] == '\n') res[strlen(res)-1] = 0;

    // Используем strncmp для безопасного сравнения команд
    // Формат команд: "CMD <value>"

    if (strncmp(res, "PS ", 3) == 0) {
        char *arg = res + 3;
        arg[8] = 0; // PS текст не длиннее 8 символов
        set_rds_ps(arg);
        printf("PS set to: \"%s\"\n", arg);
        fflush(stdout);
        return CONTROL_PIPE_PS_SET;
    }

    if (strncmp(res, "RT ", 3) == 0) {
        char *arg = res + 3;
        arg[64] = 0; // RT текст не длиннее 64 символов
        set_rds_rt(arg);
        printf("RT set to: \"%s\"\n", arg);
        fflush(stdout);
        return CONTROL_PIPE_RT_SET;
    }

    if (strncmp(res, "PI ", 3) == 0) {
        char *arg = res + 3;
        // Конвертируем строку (шестнадцатеричную) в число
        uint16_t pi_val = (uint16_t)strtol(arg, NULL, 16);
        set_rds_pi(pi_val);
        printf("PI set to: 0x%04X\n", pi_val);
        fflush(stdout);
        return CONTROL_PIPE_PI_SET;
    }

    if (strncmp(res, "ECC ", 4) == 0) {
        char *arg = res + 4;
        // Конвертируем строку (шестнадцатеричную) в число
        uint8_t ecc_val = (uint8_t)strtol(arg, NULL, 16);
        set_rds_ecc(ecc_val);
        printf("ECC set to: 0x%02X\n", ecc_val);
        fflush(stdout);
        return CONTROL_PIPE_ECC_SET;
    }

    if (strncmp(res, "PTY ", 4) == 0) {
        char *arg = res + 4;
        // Конвертируем строку (десятичную) в число
        uint8_t pty_val = (uint8_t)atoi(arg);
        if (pty_val > 31) {
            printf("ERROR: PTY value must be between 0 and 31.\n");
        } else {
            set_rds_pty(pty_val);
            printf("PTY set to: %u\n", pty_val);
        }
        fflush(stdout);
        return CONTROL_PIPE_PTY_SET;
    }

    if (strncmp(res, "TA ", 3) == 0) {
        char *arg = res + 3;
        int ta = atoi(arg);
        set_rds_ta(ta);
        // <<< ИЗМЕНЕНО: теперь выводится ON/OFF
        printf("TA set to %s\n", ta ? "ON" : "OFF");
        fflush(stdout);
        return CONTROL_PIPE_TA_SET;
    }

    if (strncmp(res, "TP ", 3) == 0) {
        char *arg = res + 3;
        int tp = atoi(arg);
        set_rds_tp(tp);
        // <<< ИЗМЕНЕНО: теперь выводится ON/OFF
        printf("TP set to %s\n", tp ? "ON" : "OFF");
        fflush(stdout);
        return CONTROL_PIPE_TP_SET;
    }

    if (strncmp(res, "MS ", 3) == 0) {
        char *arg = res + 3;
        int ms = 0;
        if (strcmp(arg, "M") == 0 || strcmp(arg, "m") == 0) {
            ms = 1;
        }
        set_rds_ms(ms);
        printf("M/S set to %s\n", ms ? "Music" : "Speech");
        fflush(stdout);
        return CONTROL_PIPE_MS_SET;
    }

    if (strncmp(res, "DI ", 3) == 0) {
        char *arg = res + 3;
        uint8_t di_flags = 0;
        if (strchr(arg, 'S') || strchr(arg, 's')) di_flags |= 1; // Stereo
        if (strchr(arg, 'A') || strchr(arg, 'a')) di_flags |= 2; // Artificial Head
        if (strchr(arg, 'C') || strchr(arg, 'c')) di_flags |= 4; // Compressed
        if (strchr(arg, 'D') || strchr(arg, 'd')) di_flags |= 8; // Dynamic PTY
        set_rds_di(di_flags);
        printf("DI set to: S(%d) A(%d) C(%d) D(%d)\n", (di_flags & 1) > 0, (di_flags & 2) > 0, (di_flags & 4) > 0, (di_flags & 8) > 0);
        fflush(stdout);
        return CONTROL_PIPE_DI_SET;
    }

    if (strncmp(res, "LIC ", 4) == 0) {
        char *arg = res + 4;
        uint8_t lic_val = (uint8_t)strtol(arg, NULL, 16);
        set_rds_lic(lic_val);
        printf("LIC set to: 0x%02X\n", lic_val);
        fflush(stdout);
        return CONTROL_PIPE_LIC_SET;
    }

    if (strncmp(res, "RTS ", 4) == 0) {
        char *arg = res + 4;
        int mode = 0; // По умолчанию A
        if (strcmp(arg, "B") == 0) {
            mode = 1;
        } else if (strcmp(arg, "AB") == 0) {
            mode = 2;
        }
        set_rds_rt_channel(mode);
        printf("RT Channel set to: %s\n", arg);
        fflush(stdout);
        return CONTROL_PIPE_RTS_SET;
    }

    if (strncmp(res, "PIN ", 4) == 0) {
        char *arg = res + 4;
        int day, hour, minute;
        if (sscanf(arg, "%d,%d,%d", &day, &hour, &minute) == 3) {
            set_rds_pin(day, hour, minute);
            printf("PIN set to: Day %d, %02d:%02d\n", day, hour, minute);
        } else {
            printf("ERROR: Invalid PIN format. Use DD,HH,MM.\n");
        }
        fflush(stdout);
        return CONTROL_PIPE_PIN_SET;
    }

    if (strncmp(res, "PTYN ", 5) == 0) {
        char *arg = res + 5;
        arg[8] = 0; // PTYN текст не длиннее 8 символов
        set_rds_ptyn(arg);
        printf("PTYN set to: \"%s\"\n", arg);
        fflush(stdout);
        return CONTROL_PIPE_PTYN_SET;
    }

    if (strncmp(res, "RTP ", 4) == 0) {
        char *arg = res + 4;
        if (strcmp(arg, "0") == 0) {
        disable_rds_rtp();
        printf("RTP disabled.\n");
    } else if (set_rds_rtp(arg)) {
        printf("RTP set to: \"%s\"\n", arg);
    } else {
        printf("ERROR: Invalid RTP value from control pipe.\n");
    }
        fflush(stdout);
        return CONTROL_PIPE_RTP_SET;
    }

    if (strncmp(res, "RTM ", 4) == 0) {
        char *arg = res + 4;
        char mode = 'P'; // По умолчанию 'P'
        if (strcmp(arg, "A") == 0) {
        mode = 'A';
        } else if (strcmp(arg, "D") == 0) {
        mode = 'D';
        } else if (strcmp(arg, "P") != 0) {
        // Если не P, A или D, выводим ошибку, но не меняем режим
        printf("ERROR: Invalid RTM value from control pipe. Use 'P', 'A', or 'D'.\n");
        fflush(stdout);
        return -1;
    }

    set_rds_rt_mode(mode);
    printf("RTM set to: %c\n", mode);
    fflush(stdout);
    return CONTROL_PIPE_RTM_SET;
    }

    if (strcmp(res, "CT R") == 0) {
       reset_rds_ct();
       printf("CT settings reset to system default.\n");
       fflush(stdout);
       return CONTROL_PIPE_CT_RESET;
    }

    if (strncmp(res, "CT ", 3) == 0) {
        char *arg = res + 3;
        int ct = atoi(arg);
        set_rds_ct(ct);
        printf("CT set to %s\n", ct ? "ON" : "OFF");
        fflush(stdout);
        return CONTROL_PIPE_CT_SET;
    }

    if (strncmp(res, "CTZ ", 4) == 0) {
        char *arg = res + 4;
        int sign = 1;

        if (*arg == 'm' || *arg == 'M') {
            sign = -1;
            arg++;
        } else if (*arg == 'p' || *arg == 'P') {
            arg++;
        } else {
            printf("ERROR: Invalid CTZ format. Must start with 'p' or 'm'.\n");
            fflush(stdout);
            return -1;
        }

        if (strlen(arg) == 0) {
            printf("ERROR: Invalid CTZ format. Missing hour/minute value.\n");
            fflush(stdout);
            return -1;
        }

        int hours = 0;
        int minutes = 0;
        char *colon = strchr(arg, ':');

        for (char *c = arg; *c; c++) {
            if (!isdigit(*c) && *c != ':') {
                printf("ERROR: Invalid characters in CTZ value '%s'.\n", arg);
                fflush(stdout);
                return -1;
            }
        }

        if (colon) {
            *colon = '\0';
            if (strlen(arg) > 0) hours = atoi(arg);
            minutes = atoi(colon + 1);
        } else {
            hours = atoi(arg);
        }

        if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
            printf("ERROR: Invalid CTZ value. Hours (0-23) or minutes (0-59) out of range.\n");
            fflush(stdout);
            return -1;
        }

        int total_offset_minutes = sign * (hours * 60 + minutes);
        set_rds_ctz(total_offset_minutes);
        printf("CTZ set to: %c%d:%02d\n", sign > 0 ? 'p' : 'm', hours, minutes);
        fflush(stdout);
        return CONTROL_PIPE_CTZ_SET;
    }

    if (strncmp(res, "CTC ", 4) == 0 || strncmp(res, "CTS ", 4) == 0) {
        int is_static = (res[2] == 'S');
        char *arg = res + 4;
        int hour, minute, day, month, year;

        if (sscanf(arg, "%d:%d,%d.%d.%d", &hour, &minute, &day, &month, &year) == 5) {
            // TODO: добавить валидацию значений
            if (is_static) {
                set_rds_cts(hour, minute, day, month, year);
                printf("CTS set to: %02d:%02d, %02d/%02d/%04d\n", hour, minute, day, month, year);
            } else {
                set_rds_ctc(hour, minute, day, month, year);
                printf("CTC set to: %02d:%02d, %02d/%02d/%04d\n", hour, minute, day, month, year);
            }
        } else {
            printf("ERROR: Invalid format for %s. Use HH:MM,DD.MM.YYYY.\n", is_static ? "CTS" : "CTC");
        }
        fflush(stdout);
        return is_static ? CONTROL_PIPE_CTS_SET : CONTROL_PIPE_CTC_SET;
    }
    // Новые команды AFA и AFAF
    if (strncmp(res, "AFA ", 4) == 0) {
        char *arg = res + 4;
        set_rds_af(arg);
        printf("AFA set to: %s\n", strcmp(arg, "0") == 0 ? "OFF" : arg);
        fflush(stdout);
        return CONTROL_PIPE_AFA_SET;
    }

    if (strncmp(res, "AFAF ", 5) == 0) {
        char *arg = res + 5;
        
        // <<< НАЧАЛО ИЗМЕНЕНИЙ
        if (strcmp(arg, "R") == 0 || strcmp(arg, "r") == 0) {
            // Если аргумент 'R', просто перечитываем файл
            if(set_rds_af_from_file(1)) {
                 printf("AFA list reloaded from file.\n");
             } else {
                 printf("ERROR: Failed to reload AFA list from file.\n");
             }
        } else {
            // Иначе, обрабатываем как 0 или 1
            int afaf = atoi(arg);
            if (afaf == 0 || afaf == 1) {
                 if(set_rds_af_from_file(afaf)) {
                     printf("AFA from file set to %s\n", afaf ? "ON" : "OFF");
                 } else {
                     printf("ERROR: AFA from file failed. Could not open rds/afa.txt\n");
                 }
            } else {
                printf("ERROR: Invalid AFAF value. Use 0, 1, or R.\n");
            }
        }
        // <<< КОНЕЦ ИЗМЕНЕНИЙ

        fflush(stdout);
        return CONTROL_PIPE_AFAF_SET;
    }

    // Если ни одна команда не подошла
    printf("ERROR: Unknown command '%s'\n", res);
    fflush(stdout);

    return -1;
}

/*
 * Closes the control pipe.
 */
int close_control_pipe() {
	if(f_ctl != NULL) {
		fclose(f_ctl);
		f_ctl = NULL;
	}
	return 0;
}