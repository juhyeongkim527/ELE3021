#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
    int studentId = 2021093518;
    int pid = getpid();
    int gpid = getgpid();
    
    printf(1, "My student id is %d\n", studentId);
    printf(1, "My pid is %d\n", pid);
    printf(1, "My gpid is %d\n", gpid);
    exit();
}