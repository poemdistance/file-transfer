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

#define BACKLOG 5
#define PORT "2326"
#define PATH_LEN 512
#define BUFLEN 1024*64
#define END sprintf(NoneUse, "%d", BUFLEN)

#define ISDOC     "0"
#define ISDIR     "1"
#define ISCONTENT "2"
#define SYMLNK_F  "3"   //Follow symbol link
#define SYMLNK_N  "4"   //Do not follow symbol link
#define UNFINISH  "0"
#define FINISH    "1"
#define ALLDONE   "2"

#define SEND 's'
#define RECV 'r'

int rel = 0;
char desname[PATH_LEN];
char NoneUse[20];

struct timespec start_t, finish_t;

struct Header{
    char type;      //0:document, 1:directory 2:content 3/4:symbol link
    char flag;      //0:unfinish, 1:finished, 2:all done
    char len[20];   //the length of content
} header;

int HEADLEN = sizeof(header);

struct Parameters {
    char action;
    char symlnkmod;
    char addr[20];
    char target[PATH_LEN];
} param;

/*Now it's only support parameter '-f' & '-h'*/
void help_info(char *executablefile)
{
    printf("Usage: %s [option] ...\n\n", executablefile);
    printf("-s --send. As server to send files\n");
    printf("-r --receive. As client to receive files\n");
    printf("-a --address. Must add server address after this option\n");
    printf("-f --follow. Following symbol link when transfer a symlink file\n");
    printf("-t --target. Using for server, assigning the transfer files before connection establish\n");
    printf("-h --help print this help message\n\n");
}
void extract_argv(int argc,char **argv)
{
    char *p = NULL;
    for(int i=1; i<argc; i++)
    {
        printf("%s\n", argv[i]);

        p = argv[i];
        if(*p++ == '-')
            while( *p )
            {
                switch( *p )
                {
                    case 'a': strcpy(param.addr, argv[++i]);     break;
                    case 't': strcpy(param.target, argv[++i]);   break;
                    case 'r': param.action = RECV;               break;
                    case 's': param.action = SEND;               break;
                    case 'f': param.symlnkmod  = *SYMLNK_F;      break;
                    case 'h': help_info(argv[0]);                exit(0);

                    default:
                          printf("Input error\n"); help_info(argv[0]);
                          exit(1);
                }
                p++;
            }
    }
}

int sendname(char *file, int *acceptfd, char *type, char *buf)
{
    printf("Send file name=%s ", file);
    memset(desname, 0, PATH_LEN);
    memset(&header, 0, sizeof(header));
    memset(buf, 0, BUFLEN);
    header.flag = *FINISH;
    header.type = *type;

    char *p = file + rel;   

    /*abstract target file name to 'desname'(des:destination)*/
    strcat(desname, p);    

    struct stat st;
    lstat(file, &st);
    if(header.type == *SYMLNK_F)
        stat(file, &st);

    buf[HEADLEN] = '\0';
    sprintf(header.len, "%ld",st.st_size);
    strcpy(buf, (char*)&header);
    strcat(buf+sizeof(header), desname);

    printf("buf=%s\n", buf);
    int sendlen;
    if((sendlen = send(*acceptfd, buf, BUFLEN, 0)) == -1)
        perror("send file name");

    return 1;
}

void finish(int *acceptfd, char *buf)
{
    memset(buf, 0, BUFLEN);
    header.flag = *ALLDONE;
    strcpy(buf, (char*)&header);
    if(send(*acceptfd, buf, BUFLEN, 0) > 0)
        printf("\nAll files send successful.\n");
    else 
        printf("\nSend finish flag failed.\n");
}

int sendfile(char *file, int *acceptfd, char *type, char *buf)
{
    if(strcmp(type, "dir") == 0)
        return sendname(file, acceptfd, ISDIR, buf);
    else if(strcmp(type, "doc") == 0)
    {
        if(sendname(file, acceptfd, ISDOC, buf) != 1)
        {
            fprintf(stderr, "File name send failed\n");
            return -1;
        }
    }
    else if(strcmp(type, "symlink") == 0)
    {
        if(sendname(file, acceptfd, SYMLNK_N, buf) != 1)
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
    FILE *fr = NULL;
    int loop = 1;   //loop times
    int flag = 0;
    char *b = buf;
    char *h = (char*)&header;
    char *start; 
    int sendlen = 0;
    int readlen = 0;
    int remain =  0;//remaining len
    int flen =  0;  //finish len
    long sum = 0;

    memset(&header, 0, HEADLEN);
    memset(buf, 0, BUFLEN);

    lstat(file, &st);
    header.type = *SYMLNK_N;

    /*when file isn't symlink or we follow symlink,
     *treat it as doc*/
    if(param.symlnkmod == *SYMLNK_F || \
            !S_ISLNK(st.st_mode))
    {
        flag = 1;
        stat(file, &st);
        header.type = *ISCONTENT;
        fr = fopen(file, "rb");
        assert(fr != NULL);
    }

    if(st.st_size == 0)
    {
        printf("empty doc '%s' no need to write\n", file);
        return 1;
    }

    if(st.st_size > BUFLEN)
    {
        loop = st.st_size / (BUFLEN - HEADLEN);
        /*st.st_size is not an integer multiple of BUFLEN, loop++*/
        if(st.st_size % (BUFLEN - HEADLEN) != 0)
            loop++;
    }

    header.flag = *UNFINISH;
    sprintf(header.len, "%d", BUFLEN-HEADLEN);
    header.len[END] = '\0';  

    strcpy(buf, (char*)&header);

    /*Send the data with header and fixed length*/
    while(1)
    {
        memset(buf+HEADLEN, 0, BUFLEN-HEADLEN);

        if(flag == 1)
        {
            if((readlen = fread(buf+HEADLEN, sizeof(char), BUFLEN-HEADLEN, fr)) < 0)
            {
                fprintf(stderr, "fread error, exit...\n");
                printf("Waiting for new request...\n");
                fclose(fr);
                exit(1);
            }
        }
        else 
        {
            if((readlen = readlink(file, buf+HEADLEN, BUFLEN-HEADLEN)) < 0)
            {
                fprintf(stderr, "readlink error, exit...\n");
                printf("Waiting for new request...\n");
                exit(1);
            }
        }

        if(loop != 1 && readlen != (BUFLEN-HEADLEN) )
        {
            fprintf(stderr, "Func sendfile: read file length error\n");
            exit(1);
        }

        if(loop == 1)     //last transmit
        {
            header.flag = *FINISH;
            buf[1] = header.flag;

            sprintf(header.len, "%d", readlen);
            header.len[END] = '\0';  

            /*insert header*/
            b=buf+2*sizeof(char);
            h=(char*)&header+2*sizeof(char);

            for(; b<buf+HEADLEN; b++, h++) 
                *b = *h;
        }

        start = buf;
        remain = BUFLEN;
        flen = 0;
        sum = sum + atoi(header.len);

        /*Loop until all data is sent*/
        for(;remain != 0; start += flen)
        {
            if((sendlen = send(*acceptfd, start, remain, 0)) == -1)
            {
                perror("send");
                printf("Waiting for new request...\n");
                exit(1);
            }
            printf("sendlen=%d buf=%s\n", sendlen, buf);   //log

            flen = sendlen + flen;
            remain = BUFLEN - flen;
        }

        if(--loop == 0)
            break;

    }//while loop

    if(fr != NULL)
        fclose(fr);

    printf("\n\n");
    return 1;
}

/*serch file by recusive*/
void listdir(DIR *dir, char *path, int *acceptfd, char *buf)
{
    struct dirent *dent;
    struct stat st;
    DIR *tmpdir;
    char tpath[PATH_LEN] = { '\0' };
    int status;

    /*send directory name*/
    status = sendfile(path, acceptfd, "dir", buf);
    if(status != 1)
    {
        printf("sendfile failed, file=%s\n", path);
        exit(1);
    }

    while((dent = readdir(dir)) != NULL)
    {
        /*omit the entrance of current and parent directory*/
        if(strcmp(dent->d_name, ".") == 0 || 
                strcmp(dent->d_name, "..") == 0)
            continue;

        strcpy(tpath, path);
        strcat(tpath, "/");
        strcat(tpath, dent->d_name);//generate new path

        lstat(tpath, &st);
        if(S_ISLNK(st.st_mode))
        {
            if(param.symlnkmod == *SYMLNK_F)
                sendfile(tpath, acceptfd, "doc", buf);
            else
                sendfile(tpath, acceptfd, "symlink", buf);
        }
        else if(S_ISDIR(st.st_mode))
        {
            tmpdir = opendir(tpath);
            listdir(tmpdir, tpath, acceptfd, buf);
        }
        else if(S_ISREG(st.st_mode))
            sendfile(tpath, acceptfd, "doc", buf);
        else
        {
            fprintf(stderr, "Unsupport file type");
            exit(1);
        }
    }

    closedir(dir);
}

void start(int acceptfd, char *buf)
{
    char inputname[PATH_LEN] = { '\0' };
    struct stat st;
    int i = 0;
    int flag;
    while(1)
    {
        i = 0;
        memset(inputname, 0, sizeof(inputname));
        printf("Please input directory or file you want to transfer: \n>> ");

        /*Do not use scanf, or we cannot input Spase character*/
        while((inputname[i++] = getchar()) != '\n');
        inputname[i-1] = '\0';
        errno = 0;  //clear errno

        /*file exist:return 0*/
        flag = lstat(inputname, &st);
        if(param.symlnkmod == *SYMLNK_F)
            flag = stat(inputname, &st);

        /*detect whether the file is exist or not*/
        if(errno == ENOENT)
        {
            fprintf(stderr, "File not exist\n");
            printf("inputname=%s\n", inputname);
            continue;
        }
        else if(flag == 0)
            break;  //file exist just return
        else
        {
            perror("errno");
            exit(1);
        }
    }

    if(clock_gettime(CLOCK_REALTIME, &start_t) == -1)
        printf("Got start time error\n");


    DIR *dir;
    char *p = inputname;
    char *tp; 

    while(*p++ != '\0');

    tp = p;
    for(; p>inputname; p--)
        if(*p == '/')
            break;

    /*remove '/' which might be exist at the end of the inputname*/
    if(*(tp-2) == '/')
        *(tp-2) = '\0';


    /*The displacement of the target directory relative
     * to the beginning of input name*/
    rel = p - inputname + 1;
    if(p == inputname)
        rel--;

    /*Default: not follow the symbol link*/
    header.type = *SYMLNK_N;

    /*If we add the parameter: -f*/
    if(param.symlnkmod == *SYMLNK_F)
        header.type = *SYMLNK_F;

    if(S_ISDIR(st.st_mode))
    {
        dir = opendir(inputname);
        listdir(dir, inputname, &acceptfd, buf);
    }
    else if(S_ISREG(st.st_mode) ||\
            header.type == *SYMLNK_F)
    {
        sendfile(inputname, &acceptfd, "doc", buf);
    }
    else if(S_ISLNK(st.st_mode))
        sendfile(inputname, &acceptfd, "symlink", buf);
    else
    {
        printf("Unsupport file type\n");
        exit(1);
    }
}

void *get_addr(struct sockaddr *sa)
{
    if(sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void ctrl_c(int singo)
{
    /*Actually no need to do this*/
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
    memset(&param, 0, sizeof(param));
    param.symlnkmod = *SYMLNK_N;
    if(argc >= 2)
        extract_argv(argc, argv);

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
    char *buf = (char *)calloc(BUFLEN, sizeof(char));

    printf("\nImportant Note: BUFLEN=%d\n", BUFLEN);
    printf("check whether your BUFLEN is equal to client's or not\n\n");

    printf("Waiting for the request...\n");

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
            start(acceptfd, buf);

            if(clock_gettime(CLOCK_REALTIME, &finish_t) == -1)
                printf("Got finish time error\n");

            printf("time = %.4f\n", ((1.0 * finish_t.tv_sec - start_t.tv_sec) * 1000000000 \
                        + (finish_t.tv_nsec - start_t.tv_nsec)) / 1000000000);

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
