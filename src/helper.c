#include "../includes/helper.h"
#include "../includes/queue.h"

struct client *find_client_by_sock(int sock, struct client *lst) {
    struct client *c;
    for(c = lst->next; c != lst; c = c->next) {
        if(c->clntsock == sock) {
            return c;
        }
    }
    return NULL;
}

struct client *find_client_by_name(const char *name, struct client *lst) {
    struct client *c;
    for(c = lst->next; c != lst; c = c->next) {
        if(!strcmp(name,c->name)) {
            return c;
        }
    }
    return NULL;
}

void numbyte_handler(ssize_t numBytes, unsigned int size, const char* fname1,
						const char* fname2)  {
	if (numBytes < 0) {
		printf("%s failed in %s.\n",fname1, fname2);
		perror(NULL);
		exit(1);
    }
    else if (numBytes == 0) {
		printf("%s returns 0 in %s.\n",fname1, fname2);
		perror(NULL);
		exit(1);
	}
	else if ((unsigned int)numBytes != size) {
		printf("%s returns unexpected return value in %s.\n",fname1, fname2);
		perror(NULL);
		exit(1);
	}
}

void set_magic(char *ptr, int mode){
    ptr[4] = 0x04; ptr[5] = 0x17; 
    if(mode == 1) {ptr[6] = 0x9a; ptr[7] = 0x00;}
    else if (mode == 2) {ptr[6] = 0x9a; ptr[7] = 0x01;}
    else if (mode == 3) {ptr[6] = 0x12;}
    else if (mode == 4) {ptr[6] = 0x15;}
}

int verify_magic(char *ptr, char *val, int size) {
    for(int i = 0; i < size; i++){
        if(ptr[i] != val[i]) {
            return 0;
        }
    }
    return 1;
}
char *magic_gen(char *ptr, int seed, int info1) {
    ptr[0] = 0x04; ptr[1] = 0x17;
    if(seed == 1) { // greet
        ptr[2] = 0x9b;
    }
    else if(seed == 2) { // stay alive
        ptr[2] = 0x13; ptr[3] = 0x1f;
    }
    else if(seed == 3) { // list users
        ptr[2] = 0x0c;
    }
    else if(seed == 4) { // nick
        ptr[2] = 0x0f; 
        uint8_t u = (uint8_t)info1;
        ptr[3] = u - 1;
    }
    else if(seed == 5) { // list rooms
        ptr[2] = 0x09;
    }
    else if(seed == 6) { // join room
        ptr[2] = 0x03;
    }
    else if(seed == 7) { // leave
        ptr[2] = 0x06;
    }
    else if(seed == 8) { // msg
        ptr[2] = 0x12;
    }
    else if(seed == 9) { // yell
        ptr[2] = 0x15;
    }
    return ptr;
}

struct client *has_name(const char *name, struct client *lst) {
    struct client *c;
    for(c = lst->next; c != lst; c = c->next) {
        if(!strcmp(name,c->name)){
            return c;
        }
    }
    return NULL;
}

struct rooms *get_room_by_name(const char *name, struct rooms *lst) {
    struct rooms *tmp;
    for(tmp = lst->next; tmp != lst; tmp = tmp->next) {
        if(!strcmp(name,tmp->name)){
            return tmp;
        }
    }
    return NULL;
}

/* assume r->head is initialized */
void insert_node(struct rooms *r, struct client *c) { 
    struct node *n = (struct node *)malloc(sizeof(struct node));
    n->c = c;
    n->next = r->head->next;
    r->head->next = n;
}

void del_node(struct rooms *r, struct client *c) {
    struct node *h = r->head, *t = h->next;
    for(; t != NULL; h = t, t = h->next) {
        if(t->c == c){
            h->next = t->next;
            free(t);
            break;
        }
    }
}

void send_packet_room(struct client *sender, const char *p, int size) {
    struct node *n = sender->room->head;
    for(n = n->next; n != NULL; n = n->next) {
        if(n->c != sender) {
            ssize_t numBytes = send(n->c->clntsock,p,size,0);
            numbyte_handler(numBytes,size,"send()","send_packet_room()");
        }
    }
}

int get_rand(int clnt_num, struct client *clst) {
    char name[10];
    for(int i = 0; i < clnt_num; i++) {
        struct client *c;
        sprintf(name,"rand%d",i);
        int flag = 1;
        for(c = clst->next; c != clst; c = c->next) {
            if(!strcmp(name,c->name)){
                flag = 0;
            }
        }
        if(flag) {return i;}
    }
    return clnt_num;
}

void update_time(struct client *c) {
    struct timespec d;
    clock_gettime(CLOCK_REALTIME,&d);
    c->last_time = d.tv_sec;
}
int check_time(struct client *clst) {
    struct client *c = clst->next, *t;
    struct timespec d;
    clock_gettime(CLOCK_REALTIME,&d);
    int sec = (int)d.tv_sec, deleted = 0;
    while(c != clst) {
        t = c;
        c = c->next;
        if(sec - t->last_time > 30) { 
            if(t->room) {
                del_node(t->room,t);
                if(!t->room->head->next) {
                    del_room(t->room);
                }
            }
            del_client(t);
            deleted++;
        }
    }
    return deleted;
}

void del_client(struct client *c) {
    DelDQ(c);
    close(c->clntsock);
    free(c);
}

void del_room(struct rooms *r) {
    DelDQ(r);
    free(r->head);
    free(r);
}

int re_init_fds(struct client *lst, struct pollfd *fds) {
    struct client *c;
    int rntval = 0;
    for(c = lst->next; c != lst; c = c->next) {
        fds[rntval].fd = c->clntsock;
        fds[rntval].events = POLLIN;
        rntval++;
    }
    return rntval;
}