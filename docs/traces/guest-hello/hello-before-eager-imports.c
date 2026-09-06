/* Independently authored qualification program; no SDK headers or syscalls.
 * The three declarations use the original 32-bit Darwin libSystem ABI.
 * Link against the read-only original library; do not copy its implementation.
 */
extern int getpid(void);
extern int write(int, const void *, unsigned int);
extern void _exit(int) __attribute__((noreturn));
void start(void) {
    char line[] = "IPHONE1_OWN_PROGRAM_V1 pid=0000000000\n";
    unsigned int pid = (unsigned int)getpid();
    unsigned int number = pid;
    for (int i = sizeof(line) - 3; i >= (int)sizeof(line) - 12; --i) { line[i] = '0' + number % 10; number /= 10; }
    int count = write(1, line, sizeof(line) - 1);
    _exit(pid > 1 && count == sizeof(line) - 1 ? 0 : 71);
}
