#define _GNU_SOURCE 1

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <event2/event.h>
#include <pthread.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/listener.h>

#include "queue.h"
#include "share.h"

// ----------------------------------------------------------------------------
struct basedata_st {
    struct event_base* b;
    struct evconnlistener* l;

    int thrnum;
    struct workerdata_st** wds;
    struct package_st* (*handler)(struct package_st* pkg);
};

struct basedata_st* basedata_new(const char* addr, struct package_st* (*handler)(struct package_st* pkg));
void basedata_free(struct basedata_st* bd);
void basedata_dispatch(struct basedata_st* bd);

// ----------------------------------------------------------------------------
struct workerdata_st {
    struct basedata_st* bd;
    int i;
    int rfd;
    int wfd;
    struct queue_st* recvq;
    struct queue_st* sendq;
    struct event* revt;

    int w_stop;
    pthread_t thr;
};

void* _worker(void* ctx);

// ----------------------------------------------------------------------------
struct sessdata_st {
    struct basedata_st* bd;
    struct bufferevent* bev;
    int fd;
    int ref;
};

extern struct sessdata_st* sessdata_new(struct basedata_st* bd, int nfd);
void sessdata_ref(struct sessdata_st* sd);
void sessdata_unref(struct sessdata_st* sd);

// ----------------------------------------------------------------------------
void _conn_data_cb(struct bufferevent* bev, void* ctx);
void _conn_event_cb(struct bufferevent* bev, short what, void* ctx);
void _acceptcb(struct evconnlistener* l, evutil_socket_t nfd, struct sockaddr* addr, int socklen, void* ctx);
void _errorcb(struct evconnlistener* l, void* ctx);
void _pipe_data_cb(int rfd, short what, void* ctx);

// sessdata ---------------------------------------------------------------------
struct sessdata_st*
sessdata_new(struct basedata_st* bd, int nfd) {
    struct sessdata_st* sd = malloc(sizeof(*sd));
    sd->fd = nfd;
    sd->bd = bd;
    sd->bev = bufferevent_socket_new(
                                    bd->b,
                                    nfd, 
                                    BEV_OPT_CLOSE_ON_FREE);
    assert(sd->bev != NULL);

    sd->ref = 1;
    bufferevent_setcb(sd->bev, _conn_data_cb, NULL, _conn_event_cb, sd);
    bufferevent_enable(sd->bev, EV_READ|EV_WRITE);

    printf("fd:%d sd:%ld created\n", sd->fd, (int64_t)sd);

    return sd;
}

void
sessdata_ref(struct sessdata_st* sd) {
    sd->ref++;
    printf("fd:%d sd:%ld count:%d ref \n", sd->fd, (int64_t)sd, sd->ref);
}

void
sessdata_unref(struct sessdata_st* sd) {
    if (--sd->ref == 0) {
        printf("fd:%d sd:%ld count:%d unref \n", sd->fd, (int64_t)sd, sd->ref);
        free(sd);
        printf("fd:%d sd:%ld freed\n", sd->fd, (int64_t)sd);
        return;
    }
    printf("fd:%d sd:%ld count:%d unref \n", sd->fd, (int64_t)sd, sd->ref);
}

// worker ---------------------------------------------------------------------
void*
_worker(void* ctx) {
    struct workerdata_st* pwd = ctx;
    struct basedata_st* bd = pwd->bd;

    printf("worker:%d begin\n", pwd->i);

    while (!pwd->w_stop) {
        struct package_st* pkg = queue_dequeue(pwd->recvq);

        struct package_st* npkg = bd->handler(pkg);

        int fd = npkg->sd->fd;
        int i = 0;
        int wi = -1;
        for (; i<bd->thrnum; ++i) {
            wi = (fd+i)%bd->thrnum;
            if (0 == queue_tryenqueue(bd->wds[wi]->sendq, npkg)) {
                break;
            }
        }
        if (i == bd->thrnum) {
            wi = fd % bd->thrnum;
            queue_enqueue(bd->wds[wi]->sendq, npkg);
        }
        if (wi >= 0) {
            while (write(pwd->wfd, "s", 1) != 1)
                ;
        }
    }
    printf("worker:%d finish\n", pwd->i);
}

// callbacks ----------------------------------------------------------------
void 
_conn_data_cb(
        struct bufferevent* bev, 
        void* ctx) {

    struct sessdata_st* sd = ctx;
    struct basedata_st* bd = sd->bd;

    struct evbuffer* input = bufferevent_get_input(bev);
    struct evbuffer* output = bufferevent_get_output(bev);

    size_t size = 0;
    char* line = evbuffer_readln(input, &size, EVBUFFER_EOL_ANY);
    if (line) {
        struct package_st* pkg = malloc(sizeof(*pkg));
        pkg->sd = sd;
        pkg->p = strdup(line);
        pkg->size = size;
        sessdata_ref(sd);

        int fd = bufferevent_getfd(sd->bev);
        int i = 0;
        for (; i<bd->thrnum; ++i) {
            if (0 == queue_tryenqueue(bd->wds[(fd+i) % bd->thrnum]->recvq, pkg)) {
                printf("fd:%d enqueue wi:%d\n", fd, (fd+i)%bd->thrnum);
                break;
            }
        }
        if (i == bd->thrnum) {
            queue_enqueue(bd->wds[fd % bd->thrnum]->recvq, pkg);
            printf("fd:%d enqueue wi:%d\n", fd, fd%bd->thrnum);
        }

        free(line);
    }

    int fd = bufferevent_getfd(bev);
    printf("fd:%d data\n", fd);
}

void 
_conn_event_cb(
        struct bufferevent* bev, 
        short what, 
        void* ctx) {

    struct sessdata_st* sd = ctx;

    int fd = bufferevent_getfd(bev);

    printf("fd:%d event:%d\n", fd, what);
    if (what & (BEV_EVENT_EOF|BEV_EVENT_ERROR)) {
        if (sd->bev) {
            bufferevent_free(sd->bev);
            sd->bev = NULL;
            printf("fd:%d closed\n", fd);
        }
        sessdata_unref(sd);
    }
}


void 
_acceptcb(
        struct evconnlistener* l, 
        evutil_socket_t nfd, 
        struct sockaddr* addr, 
        int socklen, 
        void* ctx) {

    struct basedata_st* bd = ctx;

    struct sessdata_st* sd = sessdata_new(bd, nfd);

    printf("fd:%d accepted\n", nfd);
}

void
_errorcb(
        struct evconnlistener* l,
        void* ctx) {
    struct basedata_st* bd = ctx;

    printf("_errorcb error:%d\n", EVUTIL_SOCKET_ERROR());

    event_base_loopexit(bd->b, NULL);
}

void
_pipe_data_cb(int rfd, short what, void* ctx) {
    printf("pipe event:%d\n", what);
    struct workerdata_st* pwd = ctx;
    if (what & EV_READ) {
        char buff[4096];
        while (recv(rfd, buff, 4096, MSG_DONTWAIT) == 4096)
            ;

        while (1) {
            struct package_st* pkg = queue_trydequeue(pwd->sendq);
            if (pkg == NULL)
                break;

            printf("fd:%d msg to sent\n", pkg->sd->fd);

            if (pkg->sd->bev) {
                bufferevent_write(pkg->sd->bev, pkg->p, pkg->size);
                printf("fd:%d msg sent\n", pkg->sd->fd);
            } else {
                printf("oldfd:%d msg drop\n", pkg->sd->fd);
            }

            sessdata_unref(pkg->sd);
            free(pkg->p);
            free(pkg);
        }
    }
}
// ----------------------------------------------------------------------------
struct basedata_st* 
basedata_new(const char* addr, struct package_st* (*handler)(struct package_st* pkg)) {
    struct basedata_st* bd = malloc(sizeof(*bd));
    bd->b = event_base_new();
    bd->thrnum = 4;
    bd->handler = handler;
    bd->wds = malloc(sizeof(struct workerdata_st*) * bd->thrnum);
    for (int i=0; i<bd->thrnum; i++) {
        bd->wds[i] = malloc(sizeof(struct workerdata_st));
        struct workerdata_st* pwd = bd->wds[i];

        int fds[2];
        int ret = evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
        if (ret != 0) {
            perror("socketpair");
            exit(1);
        }
        struct event* revt = event_new(bd->b, fds[0], EV_READ|EV_PERSIST, _pipe_data_cb, pwd);
        ret = event_add(revt, NULL);
        assert(ret == 0);

        pwd->bd = bd;
        pwd->i = i;
        pwd->revt = revt;
        pwd->rfd = fds[0];
        pwd->wfd = fds[1];
        pwd->recvq = queue_new();
        pwd->sendq = queue_new();

        pwd->w_stop = 0;
        pthread_create(&pwd->thr, NULL, _worker, pwd);
    }


    struct sockaddr saddr;
    int socklen = sizeof(saddr);
    int ret = evutil_parse_sockaddr_port(addr, &saddr, &socklen);
    if (ret != 0) {
        printf("evutil_parse_sockaddr_port error\n");
        return NULL;
    }

    bd->l = evconnlistener_new_bind(
                        bd->b, 
                        _acceptcb, 
                        bd, 
                        LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, 
                        100, 
                        &saddr, 
                        socklen);
    evconnlistener_set_error_cb(bd->l, _errorcb);

    return bd;
}
void 
basedata_free(struct basedata_st* bd) {
    evconnlistener_free(bd->l);
    event_base_free(bd->b);

    for (int i=0; i<bd->thrnum; i++) {
        struct workerdata_st* pwd = bd->wds[i];

        pwd->w_stop = 1;
        pthread_join(pwd->thr, NULL);

        event_del(pwd->revt);
        event_free(pwd->revt);
        close(pwd->wfd);
        close(pwd->rfd);
        queue_free(pwd->recvq);
        queue_free(pwd->sendq);

        free(pwd);
    }
    free(bd->wds);
    free(bd);
}
void 
basedata_dispatch(struct basedata_st* bd) {
    event_base_dispatch(bd->b);
}

// main -----------------------------------------------------------------------
extern struct package_st* myhandler(struct package_st*);

int
main() {
    struct basedata_st* bd = basedata_new("0.0.0.0:9876", myhandler);
    assert(bd);

    basedata_dispatch(bd);
    basedata_free(bd);
}


