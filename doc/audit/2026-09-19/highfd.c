#define _XOPEN_SOURCE 600
#include "cgra.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void){
 int f=-1;while(f<1100){f=open("/dev/null",O_RDONLY);if(f<0)return 2;}
 int m=posix_openpt(O_RDWR|O_NOCTTY);if(m<0||grantpt(m)||unlockpt(m))return 3;
 cgra_t *d=cgra_open(ptsname(m),115200);if(!d)return 4;cgra_set_timeout(d,1);
 int rc=cgra_identify(d,NULL);printf("identify %d\n",rc);cgra_close(d);return 0;
}
