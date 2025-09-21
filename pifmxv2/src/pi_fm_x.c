#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sndfile.h>

#include "rds.h"
#include "fm_mpx.h"
#include "control_pipe.h"

#include "mailbox.h"
#define MBFILE            DEVICE_FILE_NAME    /* From mailbox.h */

#if (RASPI)==1
#define PERIPH_VIRT_BASE 0x20000000
#define PERIPH_PHYS_BASE 0x7e000000
#define DRAM_PHYS_BASE 0x40000000
#define MEM_FLAG 0x0c
#define PLLFREQ 500000000.
#elif (RASPI)==2
#define PERIPH_VIRT_BASE 0x3f000000
#define PERIPH_PHYS_BASE 0x7e000000
#define DRAM_PHYS_BASE 0xc0000000
#define MEM_FLAG 0x04
#define PLLFREQ 500000000.
#elif (RASPI)==4
#define PERIPH_VIRT_BASE 0xfe000000
#define PERIPH_PHYS_BASE 0x7e000000
#define DRAM_PHYS_BASE 0xc0000000
#define MEM_FLAG 0x04
#define PLLFREQ 750000000.
#else
#error Unknown Raspberry Pi version (variable RASPI)
#endif

#define NUM_SAMPLES        50000
#define NUM_CBS            (NUM_SAMPLES * 2)

#define BCM2708_DMA_NO_WIDE_BURSTS    (1<<26)
#define BCM2708_DMA_WAIT_RESP        (1<<3)
#define BCM2708_DMA_D_DREQ        (1<<6)
#define BCM2708_DMA_PER_MAP(x)        ((x)<<16)
#define BCM2708_DMA_END            (1<<1)
#define BCM2708_DMA_RESET        (1<<31)
#define BCM2708_DMA_INT            (1<<2)

#define DMA_CS            (0x00/4)
#define DMA_CONBLK_AD        (0x04/4)
#define DMA_DEBUG        (0x20/4)

#define DMA_BASE_OFFSET        0x00007000
#define DMA_LEN            0x24
#define PWM_BASE_OFFSET        0x0020C000
#define PWM_LEN            0x28
#define CLK_BASE_OFFSET            0x00101000
#define CLK_LEN            0xA8
#define GPIO_BASE_OFFSET    0x00200000
#define GPIO_LEN        0x100

#define DMA_VIRT_BASE        (PERIPH_VIRT_BASE + DMA_BASE_OFFSET)
#define PWM_VIRT_BASE        (PERIPH_VIRT_BASE + PWM_BASE_OFFSET)
#define CLK_VIRT_BASE        (PERIPH_VIRT_BASE + CLK_BASE_OFFSET)
#define GPIO_VIRT_BASE        (PERIPH_VIRT_BASE + GPIO_BASE_OFFSET)
#define PCM_VIRT_BASE        (PERIPH_VIRT_BASE + PCM_BASE_OFFSET)

#define PWM_PHYS_BASE        (PERIPH_PHYS_BASE + PWM_BASE_OFFSET)
#define PCM_PHYS_BASE        (PERIPH_PHYS_BASE + PCM_BASE_OFFSET)
#define GPIO_PHYS_BASE        (PERIPH_PHYS_BASE + GPIO_BASE_OFFSET)


#define PWM_CTL            (0x00/4)
#define PWM_DMAC        (0x08/4)
#define PWM_RNG1        (0x10/4)
#define PWM_FIFO        (0x18/4)

#define PWMCLK_CNTL        40
#define PWMCLK_DIV        41

#define CM_GP0DIV (0x7e101074)

#define GPCLK_CNTL        (0x70/4)
#define GPCLK_DIV        (0x74/4)

#define PWMCTL_MODE1        (1<<1)
#define PWMCTL_PWEN1        (1<<0)
#define PWMCTL_CLRF        (1<<6)
#define PWMCTL_USEF1        (1<<5)

#define PWMDMAC_ENAB        (1<<31)
// I think this means it requests as soon as there is one free slot in the FIFO
// which is what we want as burst DMA would mess up our timing.
#define PWMDMAC_THRSHLD        ((15<<8)|(15<<0))

#define GPFSEL0            (0x00/4)

// The deviation specifies how wide the signal is. Use 25.0 for WBFM
// (broadcast radio) and about 3.5 for NBFM (walkie-talkie style radio)
#define DEVIATION        25.0


typedef struct {
    uint32_t info, src, dst, length,
         stride, next, pad[2];
} dma_cb_t;

#define BUS_TO_PHYS(x) ((x)&~0xC0000000)


static struct {
    int handle;            /* From mbox_open() */
    unsigned mem_ref;    /* From mem_alloc() */
    unsigned bus_addr;    /* From mem_lock() */
    uint8_t *virt_addr;    /* From mapmem() */
} mbox;



static volatile uint32_t *pwm_reg;
static volatile uint32_t *clk_reg;
static volatile uint32_t *dma_reg;
static volatile uint32_t *gpio_reg;

struct control_data_s {
    dma_cb_t cb[NUM_CBS];
    uint32_t sample[NUM_SAMPLES];
};

#define PAGE_SIZE    4096
#define PAGE_SHIFT    12
#define NUM_PAGES    ((sizeof(struct control_data_s) + PAGE_SIZE - 1) >> PAGE_SHIFT)

static struct control_data_s *ctl;

static void
udelay(int us)
{
    struct timespec ts = { 0, us * 1000 };

    nanosleep(&ts, NULL);
}

static void
terminate(int num)
{
    // Stop outputting and generating the clock.
    if (clk_reg && gpio_reg && mbox.virt_addr) {
        // Set GPIO4 to be an output (instead of ALT FUNC 0, which is the clock).
        gpio_reg[GPFSEL0] = (gpio_reg[GPFSEL0] & ~(7 << 12)) | (1 << 12);

        // Disable the clock generator.
        clk_reg[GPCLK_CNTL] = 0x5A;
    }

    if (dma_reg && mbox.virt_addr) {
        dma_reg[DMA_CS] = BCM2708_DMA_RESET;
        udelay(10);
    }

    fm_mpx_close();
    close_control_pipe();

    if (mbox.virt_addr != NULL) {
        unmapmem(mbox.virt_addr, NUM_PAGES * 4096);
        mem_unlock(mbox.handle, mbox.mem_ref);
        mem_free(mbox.handle, mbox.mem_ref);
    }

    printf("Terminating: cleanly deactivated the DMA engine and killed the carrier.\n");

    exit(num);
}

static void
fatal(char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    terminate(0);
}

static size_t
mem_virt_to_phys(void *virt)
{
    size_t offset = (size_t)virt - (size_t)mbox.virt_addr;

    return mbox.bus_addr + offset;
}

static size_t
mem_phys_to_virt(size_t phys)
{
    return (size_t) (phys - mbox.bus_addr + mbox.virt_addr);
}

static void *
map_peripheral(uint32_t base, uint32_t len)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    void * vaddr;

    if (fd < 0)
        fatal("Failed to open /dev/mem: %m.\n");
    vaddr = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, base);
    if (vaddr == MAP_FAILED)
        fatal("Failed to map peripheral at 0x%08x: %m.\n", base);
    close(fd);

    return vaddr;
}



#define SUBSIZE 1
#define DATA_SIZE 5000


int tx(uint32_t carrier_freq, char *audio_file, uint16_t pi, char *ps, char *rt, uint8_t pty, int tp, int ta, float ppm, char *control_pipe) {
    // Catch all signals possible - it is vital we kill the DMA engine
    // on process exit!
    for (int i = 0; i < 64; i++) {
        struct sigaction sa;

        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = terminate;
        sigaction(i, &sa, NULL);
    }

    dma_reg = map_peripheral(DMA_VIRT_BASE, DMA_LEN);
    pwm_reg = map_peripheral(PWM_VIRT_BASE, PWM_LEN);
    clk_reg = map_peripheral(CLK_VIRT_BASE, CLK_LEN);
    gpio_reg = map_peripheral(GPIO_VIRT_BASE, GPIO_LEN);

    // Use the mailbox interface to the VC to ask for physical memory.
    mbox.handle = mbox_open();
    if (mbox.handle < 0)
        fatal("Failed to open mailbox. Check kernel support for vcio / BCM2708 mailbox.\n");
    printf("Allocating physical memory: size = %zu     ", NUM_PAGES * 4096);
    if(! (mbox.mem_ref = mem_alloc(mbox.handle, NUM_PAGES * 4096, 4096, MEM_FLAG))) {
        fatal("Could not allocate memory.\n");
    }
    // TODO: How do we know that succeeded?
    printf("mem_ref = %u     ", mbox.mem_ref);
    if(! (mbox.bus_addr = mem_lock(mbox.handle, mbox.mem_ref))) {
        fatal("Could not lock memory.\n");
    }
    printf("bus_addr = %x     ", mbox.bus_addr);
    if(! (mbox.virt_addr = mapmem(BUS_TO_PHYS(mbox.bus_addr), NUM_PAGES * 4096))) {
        fatal("Could not map memory.\n");
    }
    printf("virt_addr = %p\n", mbox.virt_addr);


    // GPIO4 needs to be ALT FUNC 0 to output the clock
    gpio_reg[GPFSEL0] = (gpio_reg[GPFSEL0] & ~(7 << 12)) | (4 << 12);

    // Program GPCLK to use MASH setting 1, so fractional dividers work
    clk_reg[GPCLK_CNTL] = 0x5A << 24 | 6;
    udelay(100);
    clk_reg[GPCLK_CNTL] = 0x5A << 24 | 1 << 9 | 1 << 4 | 6;

    ctl = (struct control_data_s *) mbox.virt_addr;
    dma_cb_t *cbp = ctl->cb;
    uint32_t phys_sample_dst = CM_GP0DIV;
    uint32_t phys_pwm_fifo_addr = PWM_PHYS_BASE + 0x18;


    // Calculate the frequency control word
    // The fractional part is stored in the lower 12 bits
    uint32_t freq_ctl = ((float)(PLLFREQ / carrier_freq)) * ( 1 << 12 );


    for (int i = 0; i < NUM_SAMPLES; i++) {
        ctl->sample[i] = 0x5a << 24 | freq_ctl;    // Silence
        // Write a frequency sample
        cbp->info = BCM2708_DMA_NO_WIDE_BURSTS | BCM2708_DMA_WAIT_RESP;
        cbp->src = mem_virt_to_phys(ctl->sample + i);
        cbp->dst = phys_sample_dst;
        cbp->length = 4;
        cbp->stride = 0;
        cbp->next = mem_virt_to_phys(cbp + 1);
        cbp++;
        // Delay
        cbp->info = BCM2708_DMA_NO_WIDE_BURSTS | BCM2708_DMA_WAIT_RESP | BCM2708_DMA_D_DREQ | BCM2708_DMA_PER_MAP(5);
        cbp->src = mem_virt_to_phys(mbox.virt_addr);
        cbp->dst = phys_pwm_fifo_addr;
        cbp->length = 4;
        cbp->stride = 0;
        cbp->next = mem_virt_to_phys(cbp + 1);
        cbp++;
    }
    cbp--;
    cbp->next = mem_virt_to_phys(mbox.virt_addr);

    // Here we define the rate at which we want to update the GPCLK control
    // register.
    //
    // Set the range to 2 bits. PLLD is at 500 MHz, therefore to get 228 kHz
    // we need a divisor of 500000000 / 2000 / 228 = 1096.491228
    //
    // This is 1096 + 2012*2^-12 theoretically
    //
    // However the fractional part may have to be adjusted to take the actual
    // frequency of your Pi's oscillator into account. For example on my Pi,
    // the fractional part should be 1916 instead of 2012 to get exactly
    // 228 kHz. However RDS decoding is still okay even at 2012.
    //
    // So we use the 'ppm' parameter to compensate for the oscillator error

    float divider = (PLLFREQ/(2000*228*(1.+ppm/1.e6)));
    uint32_t idivider = (uint32_t) divider;
    uint32_t fdivider = (uint32_t) ((divider - idivider)*pow(2, 12));

    printf("ppm corr is %.4f, divider is %.4f (%d + %d*2^-12) [nominal 1096.4912].\n",
                ppm, divider, idivider, fdivider);

    pwm_reg[PWM_CTL] = 0;
    udelay(10);
    clk_reg[PWMCLK_CNTL] = 0x5A000006;              // Source=PLLD and disable
    udelay(100);
    // theorically : 1096 + 2012*2^-12
    clk_reg[PWMCLK_DIV] = 0x5A000000 | (idivider<<12) | fdivider;
    udelay(100);
    clk_reg[PWMCLK_CNTL] = 0x5A000216;              // Source=PLLD and enable + MASH filter 1
    udelay(100);
    pwm_reg[PWM_RNG1] = 2;
    udelay(10);
    pwm_reg[PWM_DMAC] = PWMDMAC_ENAB | PWMDMAC_THRSHLD;
    udelay(10);
    pwm_reg[PWM_CTL] = PWMCTL_CLRF;
    udelay(10);
    pwm_reg[PWM_CTL] = PWMCTL_USEF1 | PWMCTL_PWEN1;
    udelay(10);


    // Initialise the DMA
    dma_reg[DMA_CS] = BCM2708_DMA_RESET;
    udelay(10);
    dma_reg[DMA_CS] = BCM2708_DMA_INT | BCM2708_DMA_END;
    dma_reg[DMA_CONBLK_AD] = mem_virt_to_phys(ctl->cb);
    dma_reg[DMA_DEBUG] = 7; // clear debug error flags
    dma_reg[DMA_CS] = 0x10880001;    // go, mid priority, wait for outstanding writes


    size_t last_cb = (size_t)ctl->cb;

    // Data structures for baseband data
    float data[DATA_SIZE];
    int data_len = 0;
    int data_index = 0;

    // Initialize the baseband generator
    if(fm_mpx_open(audio_file, DATA_SIZE) < 0) return 1;

    // Initialize the RDS modulator
    char myps[9] = {0};
    set_rds_pi(pi);
    set_rds_rt(rt);
    set_rds_pty(pty);
    set_rds_tp(tp); // <<< ДОБАВИТЬ
    set_rds_ta(ta); // <<< ДОБАВИТЬ
    uint16_t count = 0;
    uint16_t count2 = 0;
    int varying_ps = 0;

    if(ps) {
        set_rds_ps(ps);
        printf("PI: %04X, PS: \"%s\".\n", pi, ps);
    } else {
        printf("PI: %04X, PS: <Varying>.\n", pi);
        varying_ps = 1;
    }
    printf("RT: \"%s\"\n", rt);

    // Initialize the control pipe reader
    if(control_pipe) {
        printf("Waiting for control pipe `%s` to be opened by the writer, e.g. "
               "by running `cat >%s`.\n", control_pipe, control_pipe);
        if(open_control_pipe(control_pipe) == 0) {
            printf("Reading control commands on %s.\n", control_pipe);
        } else {
            printf("Failed to open control pipe: %s.\n", control_pipe);
            control_pipe = NULL;
        }
    }


    printf("Starting to transmit on %3.1f MHz.\n", carrier_freq/1e6);

    for (;;) {
        // Default (varying) PS
        if(varying_ps) {
            if(count == 512) {
                snprintf(myps, 9, "%08d", count2);
                set_rds_ps(myps);
                count2++;
            }
            if(count == 1024) {
                set_rds_ps("RPi-Live");
                count = 0;
            }
            count++;
        }

        if(control_pipe && poll_control_pipe() == CONTROL_PIPE_PS_SET) {
            varying_ps = 0;
        }

        usleep(5000);

        size_t cur_cb = mem_phys_to_virt(dma_reg[DMA_CONBLK_AD]);
        int last_sample = (last_cb - (size_t)mbox.virt_addr) / (sizeof(dma_cb_t) * 2);
        int this_sample = (cur_cb - (size_t)mbox.virt_addr) / (sizeof(dma_cb_t) * 2);
        int free_slots = this_sample - last_sample;

        if (free_slots < 0)
            free_slots += NUM_SAMPLES;

        while (free_slots >= SUBSIZE) {
            // get more baseband samples if necessary
            if(data_len == 0) {
                if( fm_mpx_get_samples(data) < 0 ) {
                    terminate(0);
                }
                data_len = DATA_SIZE;
                data_index = 0;
            }

            float dval = data[data_index] * (DEVIATION / 10.);
            data_index++;
            data_len--;

            int intval = (int)((floor)(dval));
            //int frac = (int)((dval - (float)intval) * SUBSIZE);


            ctl->sample[last_sample++] = (0x5A << 24 | freq_ctl) + intval; //(frac > j ? intval + 1 : intval);
            if (last_sample == NUM_SAMPLES)
                last_sample = 0;

            free_slots -= SUBSIZE;
        }
        last_cb = (size_t)(mbox.virt_addr + last_sample * sizeof(dma_cb_t) * 2);
    }

    return 0;
}


int main(int argc, char **argv) {
    char *audio_file = NULL;
    char *control_pipe = NULL;
    uint32_t carrier_freq = 107900000;
    char *ps = NULL;
    char *rt = "PiFmX: FM transmitter and full RDS functions";
    uint16_t pi = 0x1234;
    uint8_t pty = 0;
    int tp_flag = 0; // <<< ДОБАВИТЬ: Флаг для TP
    int ta_flag = 0; // <<< ДОБАВИТЬ: Флаг для TA
    float ppm = 0;
    char *ecc_str = NULL;

    // Parse command-line arguments
    for(int i=1; i<argc; i++) {
        char *arg = argv[i];
        char *param = NULL;

        if(arg[0] == '-' && i+1 < argc) param = argv[i+1];

        if((strcmp("-wav", arg)==0 || strcmp("-audio", arg)==0) && param != NULL) {
            i++;
            audio_file = param;
        } else if(strcmp("-freq", arg)==0 && param != NULL) {
            i++;
            carrier_freq = 1e6 * atof(param);
            if(carrier_freq < 76e6 || carrier_freq > 108e6)
                fatal("Incorrect frequency specification. Must be in megahertz, of the form 107.9, between 76 and 108.\n");
        } else if(strcmp("-pi", arg)==0 && param != NULL) {
            i++;
            pi = (uint16_t) strtol(param, NULL, 16);
        } else if(strcmp("-ps", arg)==0 && param != NULL) {
            i++;
            ps = param;
        } else if(strcmp("-rt", arg)==0 && param != NULL) {
            i++;
            rt = param;
        } else if(strcmp("-ppm", arg)==0 && param != NULL) {
            i++;
            ppm = atof(param);
        } else if(strcmp("-ctl", arg)==0 && param != NULL) {
            i++;
            control_pipe = param;
        } else if(strcmp("-ecc", arg)==0 && param != NULL) {
            i++;
            ecc_str = param;
        } else if(strcmp("-pty", arg)==0 && param != NULL) { // <<< НАЧАЛО НОВОГО БЛОКА
            i++;
            int pty_val = atoi(param);
            if (pty_val < 0 || pty_val > 31) {
                fatal("Invalid PTY value: %s. Must be between 0 and 31.\n", param);
            }
            pty = (uint8_t)pty_val;
        } else if(strcmp("-tp", arg)==0 && param != NULL) { // <<< НАЧАЛО НОВОГО БЛОКА
            i++;
            tp_flag = atoi(param);
            if (tp_flag < 0 || tp_flag > 1) fatal("Invalid TP value. Use 0 for OFF, 1 for ON.\n");
        } else if(strcmp("-ta", arg)==0 && param != NULL) {
            i++;
            ta_flag = atoi(param);
            if (ta_flag < 0 || ta_flag > 1) fatal("Invalid TA value. Use 0 for OFF, 1 for ON.\n");
        } // <-- КОНЕЦ НОВОГО БЛОКА
    else {
        fatal("Unrecognised argument: %s.\n"
        "Syntax: pi_fm_rds [-freq freq] [-audio file] [-ppm ppm_error] [-pi pi_code]\n"
        "                  [-ps ps_text] [-rt rt_text] [-ctl control_pipe] [-ecc code] [-pty code] [-tp 0|1] [-ta 0|1]\n", arg); // <-- ОБНОВИТЬ СПРАВКУ
    }
}

    // Set locale based on the environment variables. This is necessary to decode
    // non-ASCII characters using mbtowc() in rds_strings.c.
    char* locale = setlocale(LC_ALL, "");
    printf("Locale set to %s.\n", locale);

// === НАЧАЛО НОВОГО БЛОКА ===
if (ecc_str) {
    uint8_t ecc_code = (uint8_t)strtol(ecc_str, NULL, 16);
    // PI-код должен соответствовать коду страны. Первая цифра PI - это код страны.
    // Например, для Испании (E) PI-код должен начинаться с E.
    // Мы можем автоматически установить это.
    set_rds_ecc(ecc_code);
    printf("ECC set to: 0x%02X\n", ecc_code);
}
// === КОНЕЦ НОВОГО БЛОКА ===

printf("PTY set to: %u\n", pty);
printf("TP set to: %s\n", tp_flag ? "ON" : "OFF");
printf("TA set to: %s\n", ta_flag ? "ON" : "OFF");

int errcode = tx(carrier_freq, audio_file, pi, ps, rt, pty, tp_flag, ta_flag, ppm, control_pipe);

terminate(errcode);
}
