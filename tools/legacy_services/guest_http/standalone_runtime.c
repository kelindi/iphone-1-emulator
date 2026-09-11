/* Minimal independently authored startup data for fixed-configuration tools.
 * Original libSystem _NSGetEnviron dynamically finds the executable's exported
 * _environ variable. Its allocator dereferences that accessor before first use.
 * This owned empty vector deliberately omits inherited environment variables.
 * No full CRT, argument vector, program-name or constructor support is claimed.
 * Native tests rename this symbol so the host libc environment is untouched. */
#ifndef OWN_STANDALONE_ENVIRON
#define OWN_STANDALONE_ENVIRON environ
#endif
static char *empty_environment[] = {0};
char **OWN_STANDALONE_ENVIRON = empty_environment;
