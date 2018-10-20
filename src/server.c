#include <stdio.h>      //printf 
#include <time.h>      //clock 
#include <assert.h>     //assert 
#include <unistd.h>     //close
#include <stdlib.h>     //exit
#include <string.h>     //memset
#include <sys/socket.h> //socket etc.
#include <errno.h>      //perror
#include <arpa/inet.h>  //inet_ntop
#include <signal.h>     //sigaction
#include <sys/wait.h>   //waitpid
#include <netdb.h>      //getaddrinfo
#include <sys/stat.h>   //stat
#include <dirent.h>     //opendir

#define END sprintf(NoneUse, "%d", BUFLEN)
#define BACKLOG 5
#define PORT "2326"
#define PATH_LEN 512
#define BUFLEN 4096

int rel = 0;
char desname[PATH_LEN];
char NoneUse[10];

struct timespec start_t, finish_t;

struct Header{
    char type;      //0:document, 1:directory 2:content
    char flag;      //0:unfinish, 1:finished, 2:all done
    char len[10];   //the length of content
} header;

int HEADLEN = sizeof(header);

int sendname(char *file, int *acceptfd, char *type, char *buf)
{
    memset(desname, 0, PATH_LEN);
    memset(&header, 0, sizeof(header));
    memset(buf, 0, BUFLEN);
    header.flag = '1';

    if(strcmp(type, "dir") == 0)
        header.type = '1';
    else if(strcmp(type, "doc") == 0)
        header.type = '0';
    else
        return -1;

    char *p = file + rel;   

    /*abstract target file name to 'desname'(des:destination)*/
    strcat(desname, p);    

    int len = strlen(desname);
    sprintf(header.len, "%d", len); //convert integer type to string
    strcpy(buf, (char*)&header);
    strcat(buf+sizeof(header), desname);

    int sendlen;
    if((sendlen = send(*acceptfd, buf, BUFLEN, 0)) == -1)
    {
        perror("send file name");
    }
    printf("desname=%s sendlen=%d\n\n", p, sendlen);
    return 1;
}

void finish(int *acceptfd, char *buf)
{
    memset(buf, 0, BUFLEN);
    header.flag = '2';
    strcpy(buf, (char*)&header);
    if(send(*acceptfd, buf, BUFLEN, 0) > 0)
    {
        printf("\nAll files send successful, send finish flag done\n");
    }
    else 
        printf("\nSend finish flag failed\n");
}

int sendfile(char *file, int *acceptfd, char *type, char *buf)
{
    printf("\npath=%s\t", file);
    if(strcmp(type, "dir") == 0)
        return sendname(file, acceptfd, type, buf);

    else if(strcmp(type, "doc") == 0)
    {
        if(sendname(file, acceptfd, type, buf) != 1)
        {
            fprintf(stderr, "File name send failed\n");
            return -1;
        }
    }
    else
    {
        fprintf(stderr, "Type error in sendfile function\n");
        return -1;
    }

    struct stat st;
    int loop = 1;   //loop times
    stat(file, &st);

    if(st.st_size > BUFLEN)
    {
        loop = st.st_size / (BUFLEN - HEADLEN);

        /*st.st_size is not an integer multiple of BUFLEN, loop++*/
        if(st.st_size % (BUFLEN - HEADLEN) != 0)
            loop++;
    }

    FILE *fr = fopen(file, "rb");
    assert(fr != NULL);

    int HEADLEN = sizeof(header);
    char *b = buf;
    char *h = (char*)&header;

    /*the start insert address of content*/
    char *start; 
    int sendlen = 0;
    int readlen = 0;
    int remain =  0;
    int flen =  0;
    int sum = 0;
    int times = 1;

    memset(&header, 0, HEADLEN);
    header.flag = '0';    //unfinish
    header.type = '2';    //content
    sprintf(header.len, "%d", BUFLEN-HEADLEN);

    /*add ending flag*/
    header.len[END] = '\0';  

    memset(buf, 0, BUFLEN);
    strcpy(buf, (char*)&header);

    /*Send the data with header and fixed length*/
    while(1)
    {
        memset(buf+HEADLEN, 0, BUFLEN-HEADLEN);

        if((readlen = fread(buf+HEADLEN, sizeof(char), BUFLEN-HEADLEN, fr)) < 0)
        {
            fprintf(stderr, "fread error, exit...\n");
            printf("Waiting for new request...\n");
            fclose(fr);
            exit(1);
        }

        if(loop != 1 && readlen != (BUFLEN-HEADLEN) )
        {
            fprintf(stderr, "Read file length error in sendfile function\n");
            exit(1);
        }

        if(loop == 1)             //last transmit
        {
            header.flag = '1';    //finish flag
            buf[1] = header.flag;

            sprintf(header.len, "%d", readlen);

            /*add ending flag*/
            header.len[END] = '\0';  

            /*insert header*/
            for(b=buf+2*sizeof(char), h=(char*)&header+2*sizeof(char);\
                    b<buf+HEADLEN; b++, h++)
            {
                *b = *h;
            }
        }

        printf("times=%d\t", times++);

        start = buf;
        remain = BUFLEN;
        flen = 0;
        sum = sum + atoi(header.len);

        /*Loop until all data is sent*/
        for(;remain != 0; start += flen)
        {
            if((sendlen = send(*acceptfd, start, BUFLEN-flen, 0)) == -1)
            {
                perror("send");
                printf("Waiting for new request...\n");
                exit(1);
            }
            printf("header.len=%d, sendlen=%d\n", atoi(header.len), sendlen);
            remain = BUFLEN - sendlen;
            flen = sendlen;
        }

        if(--loop == 0)
            break;
    }
    fclose(fr);
    printf("Data transfered successful, sent length sum = %d\n", sum);
    return 1;
}

/*serch file by recusive*/
void listdir(DIR *dir, char *path, int *acceptfd, char *buf)
{
    struct dirent *dent;
    DIR *tmpdir;
    char tpath[PATH_LEN] = { '\0' };

    /*send directory name*/
    sendfile(path, acceptfd, "dir", buf);

    while((dent = readdir(dir)) != NULL)
    {
        /*omit the entrance of current and parent directory*/
        if(strcmp(dent->d_name, ".") == 0 || 
                strcmp(dent->d_name, "..") == 0)
            continue;

        strcpy(tpath, path);
        strcat(tpath, "/");

        strcat(tpath, dent->d_name);//generate new path
        tmpdir = opendir(tpath);

        if(tmpdir == NULL)
        {
            /*ENOTDIR: error not dirctory(document)*/
            if(errno == ENOTDIR)
                sendfile(tpath, acceptfd, "doc", buf);
        }
        else
        {
            listdir(tmpdir, tpath, acceptfd, buf);
        }
    }
    closedir(dir);
}

void handle(int acceptfd, char *buf)
{
    char inputname[PATH_LEN] = { '\0' };
    while(1)
    {
        memset(inputname, 0, sizeof(inputname));
        printf("Please input directory or file you want to transfer: \n>> ");
        scanf("%s", inputname);

        /*detect whether the file is exist or not*/
        if(access(inputname, F_OK) != 0)
        {
            fprintf(stderr, "File not exist\n");
            continue;
        }
        break;
    }

    if(clock_gettime(CLOCK_REALTIME, &start_t) == -1)
        printf("Got start time error\n");


    DIR *dir;
    char *p = inputname;
    char *tp; 

    while(*p++ != '\0');

    tp = p;
    for(; p>inputname; p--)
    {
        if(*p == '/')
            break;
    }

    /*remove '/' which might be exist at the end of the inputname*/
    if(*(tp-2) == '/')
        *(tp-2) = '\0';


    /*The displacement of the target directory relative
     * to the beginning of input name*/
    rel = p - inputname + 1;
    if(p == inputname)
        rel--;

    dir = opendir(inputname);
    if(dir == NULL)
    {
        /*errno==ENOTDIR means that the file is not a directory, 
         *just send it without recursive*/
        if(errno == ENOTDIR)
        {
            if(sendfile(inputname, &acceptfd, "doc", buf) == 1)
            {
                printf("Send file successful\n");
                return;
            }
        }
        perror("errno");
    }


    listdir(dir, inputname, &acceptfd, buf);
}

void *get_addr(struct sockaddr *sa)
{
    if(sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void ctrl_c(int singo)
{
    printf("\nexit...\n");
    exit(0);
}

void handler(int signo)
{
    /*若检测到有子进程状态改变，父进程将获取其相关
     * 信息，之后子进程占用的entry将被释放，waitpid
     * 则返回子进程的pid,此时满足循环条件继续循环直至
     * 还没有子进程的状态改变，因为有wait no hang(wnohang)
     * 标志，则函数立即返回0，打破循环退出handler*/
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

int main(int argc, char **argv)
{
    int sockfd, acceptfd;
    struct addrinfo hint, *res, *p;
    struct sigaction sa;
    struct sockaddr_storage stor_addr;
    char str[INET6_ADDRSTRLEN];
    int recv;
    int opt = 1;

    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_PASSIVE;

    if((recv = getaddrinfo(NULL, PORT, &hint, &res)) != 0)
    {
        fprintf(stderr, "getaddrinfo %s\n", gai_strerror(recv));
        exit(1);
    }

    /*Bind the first sockaddr which can be binded*/
    for(p=res; p!=NULL; p=p->ai_next)
    {
        if((sockfd = socket(p->ai_family, p->ai_socktype,
                        p->ai_protocol)) == -1)
        {
            perror("server: socket");
            continue;
        }

        /* 在socket level 上重用地址，即在socket关闭后不等待
         * linger time,直接绑定相同的sockaddr*/

        /*reuse address in socket level, don't wait for the linger time
         *, bind the same sockaddr after closing socket directly*/
        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
                    &opt, sizeof(int)) == -1)
        {
            perror("setsockopt");
            exit(1);
        }

        if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("server bind");
            continue;
        }

        break;
    }

    /*如果for循环退出后p==NULL,说明for循环到了尽头
     *都没有绑定到任何sockaddr*/
    /*p=NULL: didn't bind any sockaddr, so exit*/
    if(p == NULL)
    {
        fprintf(stderr, "server failed to bind");
        exit(1);
    }

    /*free addrinfo*/
    freeaddrinfo(res);

    if(listen(sockfd, BACKLOG) == -1)
    {
        perror("listen");
        exit(1);
    }

    /*捕获子进程退出信号,回收进程防止僵尸进程出现*/
    /*Capture the child process exit signal, recycling process\
     * to prevent the emergence of zombies process*/
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);

    /*如果accept等函数调用被中断打断，不返回错误，直接重新执行*/
    /*if accept is interrupted, no error is returned, just restart*/
    sa.sa_flags = SA_RESTART;

    if(sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }

    signal(SIGINT, ctrl_c);

    socklen_t sockaddr_len = sizeof(stor_addr);
    printf("Waiting for connect request...\n");
    char *buf = (char *)calloc(BUFLEN, sizeof(char));

    printf("\n!!Please check your buffer length whether eaual to client's, BUFLEN = %d\n\n", BUFLEN);

    /*infinite loop, waiting for the connection request*/
    while(1)
    {
        /*Waiting for connection requests*/
        acceptfd = accept(sockfd, (struct sockaddr *)&stor_addr, &sockaddr_len);

        if(acceptfd == -1)
        {
            perror("accept");
            continue;
           //accept error, waiting for the next connection requests 
        }

        /*convert a binary address in network byte order into a 
         * text string*/
        inet_ntop(stor_addr.ss_family,
                get_addr((struct sockaddr*)&stor_addr), str, sizeof(str));

        printf("\nServer have connected to %s\n", str);

        if(fork() == 0) /*child process*/
        {
            close(sockfd);
            handle(acceptfd, buf);

            if(clock_gettime(CLOCK_REALTIME, &finish_t) == -1)
                printf("Got finish time error\n");

            printf("time = %.4f\n", ((1.0 * finish_t.tv_sec - start_t.tv_sec) * 1000000000 \
                        + (finish_t.tv_nsec - start_t.tv_nsec)) / 1000000000);

            printf("exited handle\n");
            finish(&acceptfd, buf);
            close(acceptfd);
            printf("Waiting for new request...\n");
            exit(0);
        }
        else   /*parent process*/
        {
            close(acceptfd);    
        }
    }
    free(buf);
    return 0;
}
