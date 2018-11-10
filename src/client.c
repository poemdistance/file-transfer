#include <stdio.h>      //printf 
#include <unistd.h>     //close
#include <stdlib.h>     //exit
#include <string.h>     //memset
#include <sys/socket.h> //socket etc.
#include <errno.h>      //perror
#include <arpa/inet.h>  //inet_ntop
#include <signal.h>     //sigaction
#include <sys/wait.h>   //waitpid
#include <netdb.h>      //getaddrinfo
#include <sys/stat.h>   //mkdir
#include <dirent.h>     //opendir
#include <time.h>
#include <assert.h>


#define BACKLOG 5
#define PORT "2326"
#define PATH_LEN 256*2
#define BUFLEN 1024*64*1
#define END sprintf(NoneUse, "%d", BUFLEN)

#define ISDOC     "0"
#define ISDIR     "1"
#define ISCONTENT "2"
#define SYMLNK_F  "3"   //Follow symbol link
#define SYMLNK_N  "4"   //Does not follow symbol link
#define UNFINISH  "0"
#define FINISH    "1"
#define ALLDONE   "2"

void dsymlink(char*, struct stat*);
void rdsymlink(char*, DIR*);


struct Header{
    char type;      //0:document, 1:directory 2:content
    char flag;      //0:unfinish, 1:finished, 2:all done
    char len[20];   //the length of content
} header;

struct timespec starttime, endtime;
char NoneUse[20];
int HEADLEN = sizeof(header);
int oa = 0; //overwrite all

struct Parameters {
    char symlnkmod;
};

void *get_addr(struct sockaddr *sa)
{
    if(sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*delete symlink*/
void dsymlink(char *file, struct stat *st)
{
     /* Do not put st=NULL behind, we need to judge
      * it firstly, or it will cause segmentation fault*/
    if(st == NULL || S_ISLNK(st->st_mode))
    {
        if(remove(file) != 0)
        {
            perror("errno");
            exit(1);
        }
        printf("Delete symlink %s\n", file);
    }
    else if(S_ISDIR(st->st_mode))
    {
        DIR *dir;
        dir = opendir(file);
        if(dir == NULL)
        {
            perror("errno");
            exit(1);
        }
        rdsymlink(file, dir);
    }
}

/*recursive delete symlink*/
void rdsymlink(char *file, DIR *dir)
{
    struct dirent *dent;
    struct stat st;
    char tpath[PATH_LEN] = { '\0' };

    while((dent = readdir(dir)) != NULL)
    {
        /*omit the entrance of current and parent directory*/
        if(strcmp(dent->d_name, ".") == 0 ||
                strcmp(dent->d_name, "..") == 0)
            continue;

        /*generate absoluetly path*/
        strcpy(tpath, file);
        strcat(tpath, "/");
        strcat(tpath, dent->d_name);

        lstat(tpath, &st);
        dsymlink(tpath, &st);

    }//while loop

    closedir(dir);
}

void fstatus(char *file)
{
    char ch;
    int flag;
    struct stat st;

    while(1)
    {
        /*return 0 if file exist*/
        flag = lstat(file, &st);    

        if(flag == 0)
        {
            if(oa == 1)
            {
                dsymlink(file, &st);
                return;
            }

            fprintf(stderr, "'%s' already exist\n", file);
            printf("o(overwrite)/q(quit)/a(overwrite all) file?: ");
            scanf("%c", &ch);
            getchar();

            switch(ch) {
                case 'q': exit(0);
                case 'o': dsymlink(file, &st);       return;
                case 'a': dsymlink(file, &st); oa=1; return;

                default: printf("Input error, ch=%c\n", ch);continue;
            }
        }
        else
        {
            /*File not exist, just return*/
            if(errno == ENOENT) 
                return;
            else
            {
                perror("errno");
                exit(1);
            }
        }
    }
}

void writein(int sockfd, FILE *fp, char *buf, char *mode)
{
    int recvlen = 0;
    int writelen = 0;
    int flen = 0;   //finish len
    int remain = 0; //remaining length
    int flag = 1;
    long wsum = 0;   //write sum
    long rsum = 0;   //read sum
    int i = 0;
    int loop;
    char *sympath = NULL;
    struct stat st;

    header.flag = *UNFINISH;

    printf("In writein function\n");
    while(1)
    {
        flag = 1;
        flen = 0;
        remain = BUFLEN;
        memset(&header, 0, HEADLEN);

        /*don't put it into next loop, it will clear the data before it receive all*/
        loop = 0;
        memset(buf, 0, BUFLEN); 

        /*Loop until we receive all data in buffer (len=BUFLEN)*/
        while(1)
        {
            if((recvlen = recv(sockfd, buf+flen, remain, 0)) < 0)
                perror("recv");

            if(loop>1)
                printf("\n");

            flen = recvlen + flen;  //finish length
            remain = BUFLEN - flen; //remaining length

            /*must enssure we receive full header,
             *or we might be obtain wrong header.len*/
            if(flag && (flen > HEADLEN))
            {
                flag = 0;
                header.flag = buf[1];

                /*buf[0] & buf[1] are both flag bit and occupying 2 bytes.
                 *so start from buf[2]*/
                for(i=0; i<END; i++)
                    header.len[i] = buf[i+2];   

                header.len[END] = '\0';
                printf("In writein buf=%s\n", buf); //log
            }

            if(remain == 0)
                break;

        }//inner loop

        rsum += atoi(header.len); //log

        if(*mode == *SYMLNK_F)
        {
            if((writelen = fwrite(buf+HEADLEN, sizeof(char),\
                            atoi(header.len), fp)) != atoi(header.len))
            {
                perror("fwrite");
                exit(1);
            }
        }
        else
        {
            sympath = (char*)fp;
            /*If the file exists, delete it regardless of the type*/
            if(lstat(sympath, &st) == 0)
                dsymlink(sympath, NULL);
            printf("symlink path=%s sympath=%s\n", buf+HEADLEN, sympath);
            if(symlink(buf+HEADLEN, sympath) != 0)
            {
                fprintf(stderr, "Create symlink failed in writein func\n");
                exit(1);
            }
            printf("Create symlink %s successful\n", buf+HEADLEN);
        }
        wsum = wsum + writelen;

        if(header.flag == *FINISH)
            break;

    }//outer loop

    if(*mode == *SYMLNK_F)
        printf("rsum=%ld, wsum=%ld\n", rsum, wsum);
}

int main(int argc, char **argv)
{
    int sockfd;
    struct addrinfo hint, *res, *p;
    char *buf;

    /*INET6_ADDRSTRLEN is large enough to hold a
     *text string representing an IPv6 address.*/
    char str[INET6_ADDRSTRLEN];

    int recvlen;
    int errval;

    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;

    if(argc < 2)
    {
        fprintf(stderr, "USAGE: './client host'\n");
        exit(1);
    }

    if((errval = getaddrinfo(argv[1], PORT, &hint, &res)) != 0)
    {
        fprintf(stderr, "getaddrinfo %s\n", gai_strerror(errval));
        exit(1);
    }

    for(p=res; p!=NULL; p = p->ai_next)
    {
        if((sockfd = socket(p->ai_family, p->ai_socktype,
                        p->ai_protocol)) == -1)
        {
            perror("socket");
            continue;
        }

        if(connect(sockfd, (struct sockaddr*)p->ai_addr,
                    p->ai_addrlen) == -1)
        {
            perror("client connect");
            close(sockfd);
            continue;
        }
        break;
    }

    if(p == NULL)
    {
        fprintf(stderr, "Failed to connect\n");
        exit(1);
    }

    inet_ntop(p->ai_family, get_addr((struct sockaddr*)p->ai_addr),
            str, sizeof(str));
    printf("Connect to %s\n", str);

    freeaddrinfo(res);

    printf("BUFLEN = %d\n", BUFLEN);
    printf("Waiting for the data transfer from the server endpoint...\n");

    char fname[PATH_LEN] = { '\0' };
    buf = (char *)calloc(BUFLEN, sizeof(char));

    int remain = 0; /*remaining length*/
    int flen = 0;   /*finish length*/
    int flag = 1;
    int i = 0;

    FILE *fp;

    while(1)
    {
        flen = 0;
        remain = BUFLEN;

        memset(buf, 0, BUFLEN);
        memset(fname, 0, PATH_LEN);
        memset(&header, 0, HEADLEN);

        /*Loop until all data is received*/
        while(1)
        {
            if((recvlen = recv(sockfd, buf+flen, remain, 0)) < 0)
            {
                perror("recv");
                exit(1);
            }

            if(flag)
            {
                if(clock_gettime(CLOCK_REALTIME, &starttime) == -1)
                    printf("Got start time error\n");
                flag = 0;
            }


            flen = recvlen + flen;  /*finish length*/
            remain = BUFLEN - flen; /*remaining length*/

            if(remain == 0)
                break;
        }

        printf("recvlen=%d\n", recvlen);    //log

        buf[HEADLEN-1] = '\0';
        header.flag = buf[1];
        header.type = buf[0];

        if(header.flag == *ALLDONE)
        {
            printf("\nAll done ^_^\n");
            break;
        }

        printf("Important log: buf=%s\n", buf);         //log
        printf("Receive Name=%s\n", buf+HEADLEN);   //log

        header.len[END] = '\0';
        strcpy(fname, buf+HEADLEN);

        if(header.type == *ISDOC ||\
                header.type == *SYMLNK_F)
        {
            printf("In follow\n");

            if(!oa)             /*oa: 'overwrite all' flag*/
                fstatus(fname); /*detect file status*/

            fp = fopen(fname, "wb");
            assert(fp != NULL);

            for(i=0; i<END; i++)
                header.len[i] = buf[i+2];   

            if(header.len[0] != '0')
                /*receive and write the data into disk*/
                writein(sockfd, fp, buf, SYMLNK_F); 

            fclose(fp);
            printf("\n\n");
        }
        else if(header.type == *SYMLNK_N) 
        {
            printf("In not follow\n");
            fstatus(fname);
            writein(sockfd, (FILE*)fname, buf, SYMLNK_N); 
        }
        else if(header.type == *ISDIR)
        {
            fstatus(fname);
            mkdir(fname, 0775);
            if(access(fname, F_OK) != 0)
                perror("errno");

            continue;
        }
        else
        {
            fprintf(stderr, "Type receive error in main function\n");
            exit(1);
        }
    }

    if(clock_gettime(CLOCK_REALTIME, &endtime) == -1)
        printf("Got end time error\n");

    printf("\nUsed time = %.4f\n", ((1.0 * endtime.tv_sec - starttime.tv_sec) * 1000000000 \
                + (endtime.tv_nsec - starttime.tv_nsec)) / 1000000000);

    close(sockfd);
    free(buf);
    return 0;
}
