#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    // 用户忘记传参数 打印错误信息
    if(argc <= 1){
        fprintf(2, "usage: sleep [int]\n");
        exit(1);
    }
    // 使用sleep系统调用
    sleep(atoi(argv[1]));
    exit(0);
}