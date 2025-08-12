
#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 读取每一行 出现\n进行分割
int readline(char* buf) {
    memset(buf, 0, sizeof(buf));
    char c;
    char* p = buf;
    while (read(0, &c, 1) > 0) {
        if (c == '\n')
            return 1;
        *p++ = c;
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    char buf[512];
    // 对标准输入进行读取
    while (readline(buf)) {
        char* argvexec[MAXARG];
        argvexec[0] = argv[1];
        argvexec[1] = argv[2];
        argvexec[2] = buf;
        argvexec[3] = 0;
        // 对每一行执行exec
        if (fork() == 0) {
            exec(argv[1], argvexec);
            exit(0);
        }else {
            wait(0);
        }
    }
}