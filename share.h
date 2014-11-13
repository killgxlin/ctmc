#ifndef _SHARE_H_
#define _SHARE_H_
struct package_st {
    struct workerdata_st* pwd;
    struct sessdata_st* sd;
    int size;
    void* p;
};

struct sessdata_st;
struct basedata_st;

extern struct sessdata_st* sessdata_new(struct basedata_st* bd, int nfd);
extern void sessdata_ref(struct sessdata_st* sd);
extern void sessdata_unref(struct sessdata_st* sd);
#endif
