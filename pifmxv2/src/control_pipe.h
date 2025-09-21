#define CONTROL_PIPE_PS_SET 1
#define CONTROL_PIPE_RT_SET 2
#define CONTROL_PIPE_TA_SET 3
#define CONTROL_PIPE_TP_SET 4
#define CONTROL_PIPE_PI_SET 5
#define CONTROL_PIPE_ECC_SET 6
#define CONTROL_PIPE_PTY_SET 7

extern int open_control_pipe(char *filename);
extern int close_control_pipe();
extern int poll_control_pipe();
