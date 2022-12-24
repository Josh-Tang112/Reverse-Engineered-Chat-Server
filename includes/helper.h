#ifndef _HELP_H
#define _HELP_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include "../includes/rserver.h"

struct client *find_client_by_sock(int sock, 
						struct client *lst);

struct client *find_client_by_name(const char *name, 
						struct client *lst);

void numbyte_handler(ssize_t numBytes, unsigned int size, 
						const char* fname1,
						const char* fname2);

void set_magic(char *ptr, int mode);

int verify_magic(char *ptr, char *val, int size);
char *magic_gen(char *ptr, int seed, int info1);

struct client *has_name(const char *name, struct client *lst);

struct rooms *get_room_by_name(const char *name, struct rooms *lst);
void insert_node(struct rooms *r, struct client *c);
void del_node(struct rooms *r, struct client *c);
void send_packet_room(struct client *sender, const char *p, int size);

int get_rand(int clnt_num, struct client *clst);

void update_time(struct client *c);
int check_time(struct client *clst);

void del_client(struct client *c);

void del_room(struct rooms *r);

int re_init_fds(struct client *lst, struct pollfd *fds);
#endif
