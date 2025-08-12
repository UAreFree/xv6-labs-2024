#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

void
lsa(char *path, char* filename)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, O_RDONLY)) < 0){
        fprintf(2, "ls: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){ // fstat系统调用 把fd文件的信息存入st结构体中
        fprintf(2, "ls: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch(st.type){
        case T_DEVICE:
        case T_FILE:
            break;

        case T_DIR:
            if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
                printf("ls: path too long\n");
                break;
            }
            strcpy(buf, path); // 路径名copy到buf中
            p = buf+strlen(buf); // 指向buf末尾
            *p++ = '/'; // 当前目录末尾+/ 继续指向末尾
            while(read(fd, &de, sizeof(de)) == sizeof(de)){ // 目录也是文件 可以read 目录里包含一系列dirent结构体数据（存有inode及对应的路径名）
                if(de.inum == 0)
                    continue;
                memmove(p, de.name, DIRSIZ); // 复制目录项的路径名到p中
                p[DIRSIZ] = 0;
                if(stat(buf, &st) < 0){
                    printf("ls: cannot stat %s\n", buf);
                    continue;
                }
                if (st.type == T_DIR) { // 该目录下这个目录项对应的依旧是目录 递归遍历
                    if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                        continue;
                    lsa(buf, filename);
                }else { // 这个目录项对应的是文件 进行判断输出
                    if (strcmp(de.name, filename) == 0) {
                        printf("%s\n", buf);
                    }
                }
            }
            break;
    }
    close(fd);
}

void
main(int argc, char *argv[])
{
    if (argc <= 1) {
        fprintf(2,"usage: find <.> <filename>\n");
        return;
    }
    char *path = argv[1];
    char *filename = argv[2];
    lsa(path, filename);
}