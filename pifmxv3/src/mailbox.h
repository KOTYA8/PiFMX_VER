#include <linux/ioctl.h>

// Newer kernels (>= 4.1) use major 249, older ones major 100.
#define MAJOR_NUM_A 249
#define MAJOR_NUM_B 100
#define IOCTL_MBOX_PROPERTY _IOWR(MAJOR_NUM_B, 0, char *)
#define DEVICE_FILE_NAME "/dev/vcio"
#define LOCAL_DEVICE_FILE_NAME "mbox"

int mbox_open();
void mbox_close(int file_desc);

unsigned get_version(int file_desc);
unsigned mem_alloc(int file_desc, unsigned size, unsigned align, unsigned flags);
unsigned mem_free(int file_desc, unsigned handle);
unsigned mem_lock(int file_desc, unsigned handle);
unsigned mem_unlock(int file_desc, unsigned handle);
void *mapmem(unsigned base, unsigned size);
void *unmapmem(void *addr, unsigned size);

unsigned execute_code(int file_desc, unsigned code, unsigned r0, unsigned r1, unsigned r2, unsigned r3, unsigned r4, unsigned r5);
unsigned execute_qpu(int file_desc, unsigned num_qpus, unsigned control, unsigned noflush, unsigned timeout);
unsigned qpu_enable(int file_desc, unsigned enable);
