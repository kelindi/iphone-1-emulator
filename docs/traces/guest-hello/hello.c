/* Independently authored qualification program; no SDK headers or syscalls.
 * The three declarations use the original 32-bit Darwin libSystem ABI.
 * Link against the read-only original library; do not copy its implementation.
 */
extern int getpid(void);
extern int write(int, const void *, unsigned int);
extern void _exit(int) __attribute__((noreturn));
/* Eager data relocations avoid a CRT-owned lazy-binding trampoline.
 * Volatile preserves indirect calls; dyld still resolves real library exports.
 */
static int (*volatile own_getpid)(void) = getpid;
static int (*volatile own_write)(int, const void *, unsigned int) = write;
static void (*volatile own_exit)(int) = _exit;
void start(void) {
    char line[] = "IPHONE1_OWN_PROGRAM_V1 pid=0000000000\n";
    unsigned int pid = (unsigned int)own_getpid();
    unsigned int number = pid;
    for (int i = sizeof(line) - 3; i >= (int)sizeof(line) - 12; --i) { line[i] = '0' + number % 10; number /= 10; }
    int count = own_write(1, line, sizeof(line) - 1);
    own_exit(pid > 1 && count == sizeof(line) - 1 ? 0 : 71);
}
