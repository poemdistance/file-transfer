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
#include <time.h>

#define END 4
#define BACKLOG 5
#define PORT "2326"
#define PATH_LEN 256
#define BUFLEN 4096+HEADLEN

struct Header{
    char type;      //0:document, 1:directory 2:content
    char flag;      //0:unfinish, 1:finished, 2:all done
    char len[10];   //the length of content
} header;

struct timespec starttime, endtime;
int HEADLEN = sizeof(header);
int oa = 0; //overwrite all

void *get_addr(struct sockaddr *sa)
{
    if(sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void fstatus(char *file)
{
    char ch;
    printf("\nIn fstatus function, file = %s\n", file);
    while(1)
    {
        if(access(file, F_OK) == 0)
        {
            fprintf(stderr, "'%s' already exist\n", file);
            printf("o(overwrite)/q(quit)/r(rename)/a(overwrite all) file?: ");
            scanf("%c", &ch);
            if(ch == 'q')
                exit(0);
            else if(ch == 'o')
                break;
            else if(ch == 'a')
            {
                oa = 1;
                return;
            }
            else if(ch == 'r')
            {
                printf("Please input new file name: ");
                scanf("%s", file);
            }
            else
                continue;
        }
        else 
            break;
    }
    printf("quiting fstatus\n\n");
}

void writein(int sockfd, FILE *fp, char *buf, char *fname)
{
    int recvlen = 0;
    int writelen = 0;
    int flen = 0;   //finish len
    int remain = 0; //remaining length
    int flag = 1;
    int wsum = 0;   //write sum
    int rsum = 0;   //read sum
    int i = 0;
    //int times = 0;

    header.flag = '0';

    printf("in writein\n");
    while(1)
    {
        flag = 1;
        flen = 0;
        remain = BUFLEN;
        memset(&header, 0, HEADLEN);

        /*don't put it into next loop it will clear the data it receive before*/
        memset(buf, 0, BUFLEN); 

        while(1)
        {
            if((recvlen = recv(sockfd, buf+flen, remain, 0)) < 0)
                perror("recv");

            /*must enssure we receive fully header or we might be obtain wrong header.len*/
            if(flag && recvlen > HEADLEN)
            {
                flag = 0;
                header.flag = buf[1];

                for(i=0; i<4; i++)
                    header.len[i] = buf[i+2];

                header.len[4] = '\0';

                if(atoi(header.len) != BUFLEN-HEADLEN)
                {
                    if(atoi(header.len) == 0)
                    {
                        printf("NULL doc\n");
                        return;
                    }
                    /*printf("times=%d\theader.len=%d\twritelen=%d\n",times,\
                        atoi(header.len), writelen); */  //log
                }
            }

            flen = recvlen + flen;  //finish length
            remain = BUFLEN - flen; //remaining length

            if(remain == 0)
                break;
        }
        //printf("times=%d\t",times++); //log

        rsum += atoi(header.len);
        if((writelen = fwrite(buf+HEADLEN, sizeof(char),\
                        atoi(header.len), fp)) != atoi(header.len))
        {
            perror("fwrite");
            exit(1);
        }
        wsum = wsum + writelen;
        //printf("header.len=%d\twritelen=%d\n",atoi(header.len), writelen); //log

        if(header.flag == '1')
        {
            printf("\nreceived \'%s\' succussful, quiting writein function\n", fname);
            break;
        }

    }
    printf("write length sum=%d\treceive len sum=%d\n", wsum, rsum);
    printf("out writein\n");
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

    while(1)
    {
        flen = 0;
        remain = BUFLEN;

        memset(buf, 0, BUFLEN);
        memset(fname, 0, PATH_LEN);
        memset(&header, 0, HEADLEN);

        /*Loop until all data is sent*/
        while(1)
        {
            if((recvlen = recv(sockfd, buf+flen, remain, 0)) > 0)
                buf[HEADLEN-1] = '\0';
            else
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

        header.flag = buf[1];
        header.type = buf[0];

        if(header.flag == '2')
        {
            printf("\nAll done ^_^\n");
            break;
        }

        header.len[END] = '\0';
        strcpy(fname, buf+HEADLEN);
        printf("\nfname = %s\n", fname);

        if(header.type == '0')
        {
            if(!oa)
                fstatus(fname); /*detect file status*/

            FILE *fp = fopen(fname, "wb");

            /*receive and write the data into disk*/
            writein(sockfd, fp, buf, fname); 
            fclose(fp);
            printf("=========================================================\n");
        }
        else if(header.type == '1')
        {
            mkdir(fname, 0755);
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
