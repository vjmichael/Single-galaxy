// SPACE-05E: adversarial POSIX stdout-inheritance fixture.
// The worker leader exits cleanly without a report, but its detached
// descendant retains stdout for five seconds after escaping the PGID.
// Report collection must fail promptly rather than block past its deadline.
// This fixture is NOT an assertion that process groups contain setsid escape.
#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h>
int main(){
    const pid_t descendant=fork();
    if(descendant<0)return 80;
    if(descendant==0){
        if(setsid()<0)_exit(81);
        sleep(5);
        _exit(0);
    }
    return 0;
}
#else
int main(){return 0;}
#endif
