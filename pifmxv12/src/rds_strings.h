#ifndef RDS_H
#define RDS_H


#include <stdlib.h>

extern void fill_rds_string(char* rds_string, char* src_string, size_t rds_string_size);
extern void fill_rds_string_mode(char* rds_string, char* src_string, size_t rds_string_size, char mode);


#endif /* RDS_H */