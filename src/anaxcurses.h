#ifndef ANAXCURSES_H
#define ANAXCURSES_H

#include <stdlib.h>
#include <ncurses.h>
#include <string.h>
#include "globals.h"

#define LINESIZE    111

struct jobui {
    int index;
    char *name;
    int state;
    int percent;
    WINDOW *window;
    anaxjob_t *job;
};
typedef struct jobui jobui_t;

struct uilist {
    int num_jobs;
    jobui_t *jobuis;
};
typedef struct uilist uilist_t;

int initUIList(uilist_t **uilist, joblist_t *joblist);
int initWindows(uilist_t *uilist);
void endWindows();
int updateJobView(jobui_t *jobui);

#endif