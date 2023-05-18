#include <unistd.h>

#define PUT_SYSCALL 134
#define GET_SYSCALL 156
#define INVALIDATE_SYSCALL 174

int main(int argc, char **argv) {

    //TODO: sistemare il codice lato user
    syscall(PUT_SYSCALL, NULL, 0);
    syscall(GET_SYSCALL, 0, NULL, 0);
    syscall(INVALIDATE_SYSCALL, 0);

    return 0;

}
