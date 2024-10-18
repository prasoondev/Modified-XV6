#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
char name[][33]={"","fork","exit","wait","pipe",
                "read","kill","exec","fstat",
                "chdir","dup","getpid","sbrk",
                "sleep","uptime","open","write",
                "mknod","unlink","link","mkdir",
                "close","waitx","getSysCount","sigalarm",
                "sigreturn","settickets"};
int main(int argc,char *argv[]){
    if(argc<2){
        fprintf(2,"Usage: syscount mask ....\n");
        exit(1);
    }
    int mask=atoi(argv[1]);
    int temp=0;
    for(int i=0;i<32;i++){
        if(1<<i==mask){
            temp=1;
        }
    }
    if(temp!=1){
        fprintf(2,"Invalid mask.\n");
        return 0;
    }
    getSysCount(mask);
    int w=fork();
    if(w==0){
        exec(argv[2],&argv[2]);
        fprintf(2,"exec failed.\n");
        exit(0);
    }
    else{
        w=wait(&w);
    }
    int count=0;
    for(int i=0;i<32;i++){
        if(1<<i==mask){
            count=i;
            break;
        }
    }
    int countf=getSysCount(count);
    // printf("%d\n",countf);
    if(strcmp(name[count],"fork")==0||strcmp(name[count],"exec")==0||strcmp(name[count],"wait")==0){
        countf--;
    }
    if(strcmp(name[count],"getSysCount")==0){
        countf=countf-2;
    }
    fprintf(2,"PID %d called %s %d times.\n",w,name[count],countf);
    exit(0);
}