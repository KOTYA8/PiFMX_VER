#define CONTROL_PIPE_PS_SET 1
#define CONTROL_PIPE_RT_SET 2
#define CONTROL_PIPE_TA_SET 3

extern int open_control_pipe(char *filename);
extern int close_control_pipe();
extern int poll_control_pipe();
