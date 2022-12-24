#ifndef _RSERVER_H
#define _RSERVER_H

#define MAXCLIENT 1024
#define MAXMSGLEN 65536

struct client {
    struct client *prev;
    struct client *next;
    struct rooms *room;
    int clntsock;
    long double last_time;
    char name[256];
};
struct node {
    struct node *next;
    struct client *c;
};
struct rooms {
    struct rooms *prev;
    struct rooms *next;
    struct client *creator;
    char password[256];
    char name[256];
    struct node *head;
};
#endif
