#define _GNU_SOURCE 1
#include "share.h"
#include <string.h>
#include <stdlib.h>

struct package_st* 
myhandler(struct package_st* pkg) {
    struct package_st* npkg = malloc(sizeof(*npkg));
    npkg->pwd = pkg->pwd;
    int len = strlen(pkg->p);
    char* str = malloc(len+2);
    memcpy(str, pkg->p, len);
    str[len] = '\n';
    str[len+1] = 0;
    npkg->size = len+2;
    npkg->p = str;

    npkg->sd = pkg->sd;
    sessdata_ref(npkg->sd);

    sessdata_unref(pkg->sd);
    free(pkg->p);
    free(pkg);

    return npkg;
}
