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

extern uint16_t get_rds_pi();
extern uint8_t get_rds_pty();
extern int get_rds_tp();
extern int get_rds_ta();
extern uint8_t get_rds_ecc();
extern int get_rds_ms();
extern uint8_t get_rds_di();
extern uint8_t get_rds_lic();
extern int set_rds_rtp(char *rtp_string);

#endif /* RDS_H */