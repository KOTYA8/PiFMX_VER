#ifndef RDS_H
#define RDS_H


#include <stdint.h>

extern void get_rds_samples(float *buffer, int count);
extern void set_rds_pi(uint16_t pi_code);
extern void set_rds_rt(char *rt);
extern void set_rds_ps(char *ps);
extern void set_rds_ta(int ta);
extern void set_rds_tp(int tp);
extern void set_rds_pty(uint8_t pty_code);
extern void set_rds_ecc(uint8_t ecc_code);
extern void set_rds_ms(int ms);
extern void set_rds_di(uint8_t flags);
extern void set_rds_lic(uint8_t lic_code);
extern void set_rds_pin(uint8_t day, uint8_t hour, uint8_t minute);
extern void set_rds_ptyn(char *ptyn);
extern void set_rds_rt_channel(int channel);
extern void set_rds_rt_mode(char mode);
extern void set_rds_ct(int ct);
extern void set_rds_ctz(int offset_minutes);
extern void set_rds_ctc(int hour, int minute, int day, int month, int year);
extern void set_rds_cts(int hour, int minute, int day, int month, int year);
extern void reset_rds_ct();
extern void disable_rds_rtp();
extern void disable_rds_ecc();
extern void disable_rds_lic();
extern void disable_rds_pin();
extern void disable_rds_ptyn();

extern uint16_t get_rds_pi();
extern uint8_t get_rds_pty();
extern int get_rds_tp();
extern int get_rds_ta();
extern uint8_t get_rds_ecc();
extern int get_rds_ms();
extern uint8_t get_rds_di();
extern uint8_t get_rds_lic();
extern int set_rds_rtp(char *rtp_string);
extern int set_rds_af(char* af_list_str);
extern int set_rds_af_from_file(int afaf);
extern int set_rds_afb(char* afb_list_str);
extern int set_rds_afb_from_file(int afbf);

#endif /* RDS_H */