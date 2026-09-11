#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
extern char **own_test_environ;
extern char **environ;
int main(void) {
    char **host = environ;
    assert(own_test_environ != NULL);
    assert(own_test_environ[0] == NULL);
    assert(own_test_environ != environ);
    assert(environ == host);
    /* Read through the same extra indirection used by the original accessor. */
    char ***accessor_result = &own_test_environ;
    assert(*accessor_result == own_test_environ);
    assert((*accessor_result)[0] == NULL);
    puts("PASS 6 owned empty-environment/host-isolation checks");
    return 0;
}
