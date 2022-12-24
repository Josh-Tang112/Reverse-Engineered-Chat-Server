#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <endian.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <math.h>
#include <time.h>

#include "../includes/optparser.h"
#include "../includes/rserver.h"
#include "../includes/helper.h"
#include "../includes/queue.h"

int main(int argc, char *argv[]) {
    /* ./rserver -p port# */
    if (argc != 3){
        printf("wrong # of arguments: %d\n", argc);
        return 1;
    }

    struct server_arguments args = server_parseopt(argc,argv);

    /* Create server socket */
    int servsock = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (servsock < 0) {perror("sock() failed.\n");return 1;}

    /* construct server address structure */
    struct sockaddr_in servaddr;
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(args.port);

    /* usual stuffs */
    if(bind(servsock, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
        perror("bind() failed.\n");
        return 1;
    }
    if(listen(servsock,MAXCLIENT) < 0) {
        perror("listen() failed.\n");
        return 1;
    }

    /* creating client list */
    struct client *client_lst, *tmp;
    InitDQ(client_lst, struct client);
    if(!client_lst) {return 1;}
    int clnt_num = 0;

    /* create rooms list */
    struct rooms *roomlst, *r;
    InitDQ(roomlst,struct rooms);

    /* set up fds for poll*/
    struct pollfd listening[1]; // for checking pending connections
    listening[0].fd = servsock;
    listening[0].events = POLLIN;
    int listen_wait = 100; // 0.1 sec
    
    struct pollfd clntfds[MAXCLIENT];
    int input_wait = 900; // 0.9 sec

    /* support variables */
    char init_name[8], buffer[1024];
    const char *greet = "Greetings from the land of milk and honig"; 
    const char *alive = "staying alive, staying alive..."; 
    const char *nickused = "This nick has been nicked by someone else.";
    const char *invalidpass = "Invalid password. You shall not pass.";
    const char *nonick = "Nick not present";
    const char *intovoid = "You shout into the void and hear nothing.";
    const char *lengthexceed = "Length limit exceeded.";
    ssize_t numBytes;
    int rc, size = 0;
    struct timespec anchor, now;
    clock_gettime(CLOCK_REALTIME,&anchor);
    while(1) {
        /* find expire client every 5 or so sec */
        clock_gettime(CLOCK_REALTIME,&now);
        if(now.tv_sec - anchor.tv_sec > 5) {
            int rntval = check_time(client_lst);
            if(rntval) {
                re_init_fds(client_lst,clntfds);
                clnt_num -= rntval;
            }
            clock_gettime(CLOCK_REALTIME,&anchor);
        }
        /* polling for pending connections */
        rc = poll(listening, 1, listen_wait); // wait for 0.3 secs
        if (rc < 0) {perror("poll() failed.\n"); return 1;}
        else if (rc) {
            int clntsock = accept(servsock,NULL,NULL);
            if(clntsock < 0){perror("accept() failed.\n"); return 1;}
            /* create and setup temporary struct */
            tmp = (struct client *)malloc(sizeof(struct client));
            tmp->clntsock = clntsock;
            sprintf(init_name,"rand%d",get_rand(clnt_num,client_lst));
            strcpy(tmp->name,init_name);
            tmp->last_time = 0;
            tmp->room = NULL;
            InsertDQ(client_lst,tmp); // insert temorary struct into list
            clntfds[clnt_num].fd = tmp->clntsock; // put in fds 
            clntfds[clnt_num].events = POLLIN;
            clnt_num++;
        }
        rc = poll(clntfds,clnt_num,input_wait); // poll for incoming traffic
        if (rc < 0) {perror("poll() failed.\n"); return 1;}
        else if (!rc) {continue;}
        for(int i = 0; i < clnt_num; i++) {
            if(clntfds[i].revents != POLLIN) {continue;}
            else if(recv(clntfds[i].fd, buffer, 1, MSG_PEEK | MSG_DONTWAIT) == 0) {
                struct client *sender = find_client_by_sock(clntfds[i].fd,client_lst);
                if(sender->room) {
                    del_node(sender->room,sender);
                    if(!sender->room->head->next) {
                        del_room(sender->room);
                    }
                }
                del_client(sender);
                clnt_num--;
                re_init_fds(client_lst,clntfds);
                continue;
            }
            /* get the size of packet */
            numBytes = recv(clntfds[i].fd,&size,4,0);
            size = htonl(size);
            struct client *sender = find_client_by_sock(clntfds[i].fd,client_lst);
            /* receive the packet */
            if(size > 1021) {
                if(sender->room) {
                    del_node(sender->room,sender);
                    if(!sender->room->head->next) {
                        del_room(sender->room);
                    }
                }
                del_client(sender);
                clnt_num--;
                re_init_fds(client_lst,clntfds);
            }
            int rcvd = 0;
            while(rcvd < size+3) {
                numBytes = recv(clntfds[i].fd,buffer+rcvd,size+3-rcvd,0);
                numbyte_handler(numBytes,numBytes,"recv()","while loop");
                rcvd += numBytes;
            }
            buffer[rcvd] = '\0'; // null terminate it
            /* calculate the offset */
            int offset = 0;
            while((buffer[offset] <= 0x1f || buffer[offset] >= 0x7f)
                    && size > 0){offset++;}
            /* construct the reply packet */
            char *reply, magic[4];
            int reply_size = 0, *iptr;
            if(!strcmp(buffer+offset,greet) 
                && verify_magic(buffer,magic_gen(magic,1,0),3)){ 
                reply_size = strlen(sender->name) + 8;
                reply = (char *)malloc(reply_size + 1);
                iptr = (int *)reply;
                iptr[0] = htonl(reply_size - 7); // size of msg is len + 1
                set_magic(reply,1);
                strcpy(reply+8,sender->name);
                numBytes = send(clntfds[i].fd,reply,reply_size,0);
                numbyte_handler(numBytes,reply_size,"send()","first if");
                free(reply);
                update_time(sender);
            } /* receives greeting packet*/
            else if(!strcmp(buffer+offset,alive) 
                    && verify_magic(buffer,magic_gen(magic,2,0),4)){ 
                update_time(sender);
            } /* receives keep alive packet*/
            else if(size == 0 && verify_magic(buffer,magic_gen(magic,3,0),3)){ 
                int copied = 0; struct node *ntmp;
                if(!sender->room) {
                    for(tmp = client_lst->next; tmp != client_lst; tmp = tmp->next){
                        reply_size += strlen(tmp->name) + 1;
                    }
                }
                else {
                    ntmp = sender->room->head;
                    for(ntmp = ntmp->next; ntmp; ntmp = ntmp->next) {
                        reply_size += strlen(ntmp->c->name) + 1;
                    }
                }
                reply = (char *)malloc(reply_size + 8 + 1);
                iptr = (int *)reply;
                iptr[0] = htonl(reply_size + 1);
                set_magic(reply,1);
                copied += 8;
                if(!sender->room) {
                    for(tmp = client_lst->next; tmp != client_lst; tmp = tmp->next){
                        uint8_t len = (uint8_t)strlen(tmp->name);
                        reply[copied] = len;
                        copied++;
                        strcpy(reply+copied,tmp->name);
                        copied += len;
                    }
                }
                else {
                    ntmp = sender->room->head;
                    for(ntmp = ntmp->next; ntmp; ntmp = ntmp->next) {
                        uint8_t len = (uint8_t)strlen(ntmp->c->name);
                        reply[copied] = len;
                        copied++;
                        strcpy(reply+copied,ntmp->c->name);
                        copied += len;
                    }
                }
                numBytes = send(clntfds[i].fd,reply,reply_size + 8,0);
                numbyte_handler(numBytes,reply_size + 8,"send()","second if");
                free(reply);
                update_time(sender);
            } /* receives list users packet */
            else if ((size > 0 && size < 256) && 
                        verify_magic(buffer,magic_gen(magic,4,size),4)) {
                struct client *c = has_name(buffer+offset,client_lst);
                if(c && strcmp(buffer+offset,sender->name)) { // if name exist
                    reply_size = 50;
                    reply = (char *)malloc(reply_size + 1);
                    iptr = (int *)reply;
                    iptr[0] = htonl(43);
                    set_magic(reply,2);
                    strcpy(reply+8,nickused);
                    numBytes = send(clntfds[i].fd,reply,reply_size,0);
                    numbyte_handler(numBytes,50,"send()","sending nick used packet");
                    free(reply);
                    update_time(sender);
                }
                else {
                    strcpy(sender->name,buffer+offset);
                    reply = (char *)malloc(8);
                    iptr = (int *)reply;
                    iptr[0] = htonl(1);
                    set_magic(reply,1);
                    numBytes = send(clntfds[i].fd,reply,8,0);
                    numbyte_handler(numBytes,8,"send()","sending nick success packet");
                    free(reply);
                    update_time(sender);
                }
            } /* receives nick packet */
            else if (size == 0 && verify_magic(buffer,magic_gen(magic,5,0),3)){
                int copied = 0;
                for(r = roomlst->next; r != roomlst; r = r->next){
                    reply_size += strlen(r->name) + 1;
                }
                reply = (char *)malloc(reply_size + 8 + 1);
                iptr = (int *)reply;
                iptr[0] = htonl(reply_size + 1);
                set_magic(reply,1);
                copied += 8;
                for(r = roomlst->next; r != roomlst; r = r->next){
                    uint8_t len = (uint8_t)strlen(r->name);
                    reply[copied] = len;
                    copied++;
                    strcpy(reply+copied,r->name);
                    copied += len;
                }
                numBytes = send(clntfds[i].fd,reply,reply_size + 8,0);
                numbyte_handler(numBytes,reply_size + 8,"send()","list rooms");
                free(reply);
                update_time(sender);
            } /* receives list rooms packet */
            else if (verify_magic(buffer,magic_gen(magic,6,0),3)) {
                uint8_t roomlen = buffer[3], passlen = size - roomlen - 2, u = 0;
                char roomname[256], roompassword[256];
                for(uint8_t j = 0; j < roomlen; j++) {
                    roomname[j] = buffer[4 + j];
                }
                for(uint8_t j = 0; j < passlen; j++) {
                    roompassword[j] = buffer[4 + roomlen + 1 + j];
                }
                roomname[roomlen] = '\0';
                roompassword[passlen] = '\0';
                struct rooms *target = get_room_by_name(roomname,roomlst);
                if(!target) { // target room does not exist
                    /* construct room */
                    target = (struct rooms *)malloc(sizeof(struct rooms));
                    target->creator = sender;
                    strcpy(target->name,roomname);
                    strcpy(target->password,roompassword);
                    InitQ(target->head,struct node);
                    insert_node(target,sender);
                    sender->room = target; // set users room as the created room
                    InsertDQ(roomlst,target); // insert room to the list
                    u = 1;
                }
                else if(strlen(target->password) == 0 || 
                        sender == target->creator ||
                        !strcmp(target->password,roompassword)){
                    if(sender->room) { // sender is in no room
                        del_node(sender->room,sender);
                    }
                    sender->room = target;
                    insert_node(target,sender);
                    u = 1;
                } /* room entrance packet accepted */
                if(u) {
                    /* construct reply packet */
                    reply = (char *)malloc(8);
                    iptr = (int *)reply;
                    iptr[0] = htonl(1);
                    set_magic(reply,1);
                    numBytes = send(clntfds[i].fd,reply,8,0);
                    numbyte_handler(numBytes,8,"send()","sending nick success packet");
                    free(reply);
                    update_time(sender);
                }
                else {
                    reply = (char *)malloc(45);
                    iptr = (int *)reply;
                    iptr[0] = htonl(38);
                    set_magic(reply,2);
                    strcpy(reply+8,invalidpass);
                    numBytes = send(clntfds[i].fd,reply,45,0);
                    numbyte_handler(numBytes,45,"send()","invalid password packet");
                    free(reply);
                    update_time(sender);
                }
            }/* receives join room */
            else if (size == 0 && verify_magic(buffer,magic_gen(magic,7,0),3)) {
                reply = (char *)malloc(8);
                iptr = (int *)reply;
                iptr[0] = htonl(1);
                set_magic(reply,1);
                numBytes = send(clntfds[i].fd,reply,8,0);
                numbyte_handler(numBytes,8,"send()","sending leave success packet");
                free(reply);
                if(!sender->room) { // sender not in any room
                    del_client(sender);
                    clnt_num--;
                    re_init_fds(client_lst,clntfds);
                }
                else {
                    del_node(sender->room,sender);
                    if(!sender->room->head->next) {
                        del_room(sender->room);
                    }
                    sender->room = NULL;
                    update_time(sender);
                }
            } /* receives leave packet */
            else if(verify_magic(buffer,magic_gen(magic,8,0),3)) {
                uint8_t recverlen = buffer[3], msglen;
                uint8_t u = 1, senderlen = strlen(sender->name);
                char name[256];
                struct client *target;
                strncpy(name,buffer+4,recverlen); 
                name[recverlen] = '\0';
                if(buffer[4 + recverlen] != 0x00) { /* msg len overflow */
                    if(sender->room) {
                        del_node(sender->room,sender);
                        if(!sender->room->head->next) {
                            del_room(sender->room);
                        }
                    }
                    del_client(sender);
                    clnt_num--;
                    re_init_fds(client_lst,clntfds);
                    u = 0;
                } 
                if(u) {target = find_client_by_name(name,client_lst);}
                if(u && !target){ // nick not present
                    reply = (char *)malloc(24);
                    iptr = (int *)reply;
                    iptr[0] = htonl(17);
                    set_magic(reply,2);
                    strncpy(reply+8,nonick,strlen(nonick));
                    numBytes = send(clntfds[i].fd,reply,24,0);
                    numbyte_handler(numBytes,24,"send()","sending no nick packet");
                    free(reply);
                    update_time(sender);
                    u = 0;
                }
                if(u) {
                    /* send packet to receiver */
                    msglen = buffer[4 + recverlen + 1];
                    reply = (char *)malloc(4 + 4 + 2 + msglen + senderlen);
                    iptr = (int *)reply;
                    iptr[0] = htonl(size);
                    set_magic(reply,3);
                    reply[7] = senderlen;
                    strncpy(reply+8,sender->name,senderlen+1);
                    reply[8 + senderlen + 1] = msglen;
                    strncpy(reply+8+senderlen+2,buffer+4+recverlen+2,msglen);
                    numBytes = send(target->clntsock,reply,10+msglen+senderlen,0);
                    numbyte_handler(numBytes,10+msglen+senderlen,"send()","msg");
                    free(reply);
                    /* send msg success to sender */
                    reply = (char *)malloc(8);
                    iptr = (int *)reply;
                    iptr[0] = htonl(1);
                    set_magic(reply,1);
                    numBytes = send(clntfds[i].fd,reply,8,0);
                    numbyte_handler(numBytes,8,"send()","sending nick success packet");
                    free(reply);
                    update_time(sender);
                }
            } /* receives msg packet */
            else if(verify_magic(buffer,magic_gen(magic,9,0),3)) {
                uint8_t u = 1, roomlen;
                if(buffer[3]==0x00 || !sender->room) { // yell in the lobby
                    reply = (char *)malloc(49);
                    iptr = (int *)reply;
                    iptr[0] = htonl(42);
                    set_magic(reply,2);
                    strncpy(reply+8,intovoid,strlen(intovoid));
                    numBytes = send(clntfds[i].fd,reply,49,0);
                    numbyte_handler(numBytes,49,"send()","sending shout to void packet");
                    free(reply);
                    update_time(sender);
                    u = 0;
                }
                if (u) {roomlen = buffer[3];}
                if (u && 
                    (buffer[4 + roomlen] != 0x00 || 
                    (uint8_t)buffer[4 + roomlen + 1] == 0xff)) {
                    reply = (char *)malloc(30);
                    iptr = (int *)reply;
                    iptr[0] = htonl(23);
                    set_magic(reply,2);
                    strncpy(reply+8,lengthexceed,strlen(lengthexceed));
                    numBytes = send(clntfds[i].fd,reply,30,0);
                    numbyte_handler(numBytes,30,"send()","sending shout too much packet");
                    free(reply);
                    if(sender->room) {
                        del_node(sender->room,sender);
                        if(!sender->room->head->next) {
                            del_room(sender->room);
                        }
                    }
                    del_client(sender);
                    clnt_num--;
                    re_init_fds(client_lst,clntfds);
                } /* msg length is too long (more than 255 byte)*/
                else if (u) {
                    uint8_t roomlen = buffer[3], sednerlen = strlen(sender->name);
                    char room[256];
                    strncpy(room,buffer+4,roomlen);
                    room[roomlen] = '\0';
                    if(strcmp(room,sender->room->name)){ //sender room is not in packet
                        if(sender->room) {
                            del_node(sender->room,sender);
                            if(!sender->room->head->next) {
                                del_room(sender->room);
                            }
                        }
                        del_client(sender);
                        clnt_num--;
                        re_init_fds(client_lst,clntfds);
                    }
                    else {
                        /* send msg success to sender */
                        reply = (char *)malloc(8);
                        iptr = (int *)reply;
                        iptr[0] = htonl(1);
                        set_magic(reply,1);
                        numBytes = send(clntfds[i].fd,reply,8,0);
                        numbyte_handler(numBytes,8,"send()","sending yell success packet");
                        free(reply);
                        /* send yell packet to peer in room */
                        reply = (char *)malloc(size + 7 + sednerlen + 1);
                        iptr = (int *) reply;
                        iptr[0] = htonl(size + sednerlen + 1);
                        set_magic(reply,4);
                        reply[7] = roomlen;
                        strncpy(reply+8,room,roomlen);
                        reply[8 + roomlen] = sednerlen;
                        strncpy(reply+8+roomlen+1,sender->name,sednerlen+1);
                        reply[8 + roomlen + 1 + sednerlen + 1] = buffer[4+roomlen+1];
                        strncpy(reply+8+roomlen+1+sednerlen+2,buffer+4+roomlen+2,(uint8_t)buffer[4+roomlen+1]);
                        send_packet_room(sender,reply,size + 7 + sednerlen + 1);
                        free(reply);
                        update_time(sender);
                    }
                }
            } /* receives yell packet */
            else {
                if(sender->room) {
                    del_node(sender->room,sender);
                    if(!sender->room->head->next) {
                        del_room(sender->room);
                    }
                }
                del_client(sender);
                clnt_num--;
                re_init_fds(client_lst,clntfds);
            } /* default case */
        } /* for loop looping over readeble fds*/
    }
    close(servsock);
}