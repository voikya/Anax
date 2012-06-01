#ifndef ANAXCURSES_H
#define ANAXCURSES_H

#include <stdlib.h>
#include <ncurses.h>
#include <string.h>
#include "globals.h"

#define LINESIZE    112

struct jobui {
    int index;
    char *name;
    int state;
    int percent;
    WINDOW *window;
    anaxjob_t *job;
};
typedef struct jobui jobui_t;

struct jobui_final {
    char *name;
    int percent;
    WINDOW *window;
};
typedef struct jobui_final finaljobui_t;

struct uilist {
    int num_jobs;
    jobui_t *jobuis;
    finaljobui_t final;
};
typedef struct uilist uilist_t;

int initUIList(uilist_t **uilist, joblist_t *joblist, char *outfile);
int initWindows(uilist_t *uilist);
void endWindows();
int updateJobView(jobui_t *jobui);
int updateFinalView(finaljobui_t *final);
int updateJobUIState(jobui_t *jobui, int state);
int updateFinalUIState(finaljobui_t *final, int percentage);

#endif