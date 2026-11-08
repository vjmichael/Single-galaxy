// SPACE-05E: adversarial POSIX stdout-inheritance fixture.
// The leader exits with status 0 WITHOUT a report only after a child has
// successfully escaped its process group. That detached child retains the
// stdout pipe for five seconds. The host must reject promptly without
// waiting for a future EOF; this does not assert full setsid containment.
#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h>
int main(){
    int handshake[2]={-1,-1};
    if(pipe(handshake)!=0)return 80;
    const pid_t descendant=fork();
    if(descendant<0){close(handshake[0]);close(handshake[1]);return 81;}
    if(descendant==0){
        close(handshake[0]);
        if(setsid()<0)_exit(82);
        const char ready='R';
        if(write(handshake[1],&ready,1)!=1)_exit(83);
        close(handshake[1]);
        sleep(5);
        _exit(0);
    }
    close(handshake[1]);
    char ready=0;
    const bool escaped=read(handshake[0],&ready,1)==1 && ready=='R';
    close(handshake[0]);
    return escaped?0:84;
}
#else
int main(){return 0;}
#endif
