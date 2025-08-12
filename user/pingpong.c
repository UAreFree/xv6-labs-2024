#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int pipe1[2], pipe2[2];
    pipe(pipe1);
    pipe(pipe2);
    char c = 'p';
    // 父进程向子进程发送一个字节 通过管道
    int pid = fork();
    if (pid < 0) {
        fprintf(2, "fork failed\n");
        exit(1);
    }
    if (pid == 0) {
        // 从pipe1里读
        close(pipe1[1]);
        read(pipe1[0], &c, 1);
        close(pipe1[0]);
        fprintf(1,"%d: received ping\n", getpid());
        // 向pipe2里写
        close(pipe2[0]);
        write(pipe2[1], &c, 1);
        close(pipe2[1]);
        exit(0);
    }else {
        // 向pipe1里写p
        close(pipe1[0]);
        write(pipe1[1], &c, 1);
        close(pipe1[1]);
        // 等子进程退出
        wait(0);
        // 从pipe2里读p
        close(pipe2[1]);
        read(pipe2[0], &c, 1);
        close(pipe2[0]);
        fprintf(1,"%d: received pong\n", getpid());
    }
    // 子进程向父进程发送上述同一个字节 通过另一个管道
    exit(0);
}