#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
char buf[512];
void cat_one_file(int fd){
    int len;
    while((len = read(fd, buf, sizeof(buf))) > 0){
        write(1, buf, len);
    }
    if(len < 0){
        printf("cat: read error\n");
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    // TODO
    int fd;
    if(argc <= 1){
        cat_one_file(0);
        exit(0);
    }
    for(int i = 1; i < argc; ++i){
        if ((fd = open(argv[i], 0)) < 0) {
            printf("cat: cannot open %s\n", argv[i]);
            exit(0);
        }
        cat_one_file(fd);
        close(fd);
    }
    exit(0);
}

