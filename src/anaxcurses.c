#include "anaxcurses.h"

/*
Layout:
110c

4c         32c                           12c                             52c                              4c
    1c                               1c            3c                                                   1c
1.   ASTGTM2_N00_E00.tif              [PENDING]      [==================================================] 0%

Final: XXXXXXXXXXX


Stages:
PENDING
RECEIVING           10%
PROCESSING          5%
LOCAL CHK           25%
REMOTE CHK          25%
PREPARING           5%
RENDERING           20%
SENDING             10%
COMPLETE

*/

int initUIList(uilist_t **uilist, joblist_t *joblist, char *outfile) {
    *uilist = malloc(sizeof(uilist_t));
    (*uilist)->num_jobs = joblist->num_jobs;
    (*uilist)->jobuis = calloc(joblist->num_jobs, sizeof(jobui_t));
    
    for(int i = 0; i < joblist->num_jobs; i++) {
        (*uilist)->jobuis[i].index = i + 1;
        (*uilist)->jobuis[i].name = calloc(33, sizeof(char));
        char *name_ptr = strrchr(joblist->jobs[i].name, '/');
        if(!name_ptr)
            name_ptr = joblist->jobs[i].name;
        strncpy((*uilist)->jobuis[i].name, name_ptr, 32);
        (*uilist)->jobuis[i].state = UI_STATE_PENDING;
        (*uilist)->jobuis[i].percent = 0;
        (*uilist)->jobuis[i].job = &(joblist->jobs[i]);
    }
    
    (*uilist)->final.name = calloc(33, sizeof(char));
    char *name_ptr = strrchr(outfile, '/');
    if(!name_ptr)
        name_ptr = outfile;
    strncpy((*uilist)->final.name, name_ptr, 32);
    (*uilist)->final.percent = 0;
    
    return 0;
}

int initWindows(uilist_t *uilist) {
    pthread_mutex_init(&curses_lock, NULL);

    initscr();
    cbreak();
    
    for(int i = 0; i < uilist->num_jobs; i++) {
        uilist->jobuis[i].window = newwin(1, LINESIZE, i + 1, 0);
        werase(uilist->jobuis[i].window);
        updateJobView(&(uilist->jobuis[i]));
    }
    
    uilist->final.window = newwin(1, LINESIZE, uilist->num_jobs + 2, 0);
    werase(uilist->final.window);
    updateFinalView(&(uilist->final));
    
    return 0;
}

void endWindows() {
    endwin();
}

int updateJobView(jobui_t *jobui) {
    // Set up a string to hold the fully-formatted output
    char output[LINESIZE];
    memset(output, 0, LINESIZE);
    
    // Format the job index
    //    Length: 4 characters
    //    Description: Left-aligned, space-padded number with following '.'
    char index[5];
    memset(index, 0, 5);
    sprintf(index, "%i.", jobui->index);
    if(jobui->index < 10)
        strcat(index, "  ");
    else if(jobui->index < 100)
        strcat(index, " ");
    index[4] = 0;
    
    // Format the image name
    //    Length: 32 characters
    //    Description: Left-aligned, space-padded string, truncated if needed
    char name[33];
    memset(name, 0, 33);
    int namelen = strlen(jobui->name);
    strncpy(name, jobui->name, 32);
    for(int i = namelen; i < 32; i++)
        name[i] = ' ';
    name[32] = 0;
    
    // Format the current state
    //    Length: 12 characters
    //    Description: 10-character string inside brackets, right-padding outside end bracket
    char status[13];
    memset(status, 0, 13);
    switch(jobui->state) {
        case UI_STATE_PENDING:
            sprintf(status, "[PENDING]   ");
            break;
        case UI_STATE_RECEIVING:
            sprintf(status, "[RECEIVING] ");
            break;
        case UI_STATE_PROCESSING:
            sprintf(status, "[PROCESSING]");
            break;
        case UI_STATE_LOCALCHK:
            sprintf(status, "[LOCAL CHK] ");
            break;
        case UI_STATE_REMOTECHK:
            sprintf(status, "[REMOTE CHK]");
            break;
        case UI_STATE_PREPARING:
            sprintf(status, "[PREPARING] ");
            break;
        case UI_STATE_RENDERING:
            sprintf(status, "[RENDERING] ");
            break;
        case UI_STATE_SENDING:
            sprintf(status, "[SENDING]   ");
            break;
        case UI_STATE_COMPLETE:
            sprintf(status, "[COMPLETE]  ");
            break;
    }
    
    // Format the status bar
    //    Length: 52 characters
    //    Description: Two fixed brackets with 50 characters in between them, either '=' or ' '
    char statusbar[53];
    memset(statusbar, 0, 53);
    int num_blocks = jobui->percent / 2;
    statusbar[0] = '[';
    for(int i = 1; i <= 50; i++) {
        statusbar[i] = (i <= num_blocks) ? '=' : ' ';
    }
    statusbar[51] = ']';
    statusbar[52] = 0;
    
    // Format the percent complete indicator
    //    Length: 4 characters
    //    Description: Right-aligned percentage, space-padded
    char percentage[6];
    memset(percentage, 0, 6);
    sprintf(percentage, "%i%%%%", jobui->percent);
    
    // Form the combined output string
    sprintf(output, "%s %s %s   %s %s", index, name, status, statusbar, percentage);
    output[LINESIZE] = 0;
    
    // Print the string to the terminal
    pthread_mutex_lock(&curses_lock);
    wmove(jobui->window, 0, 0);
    wprintw(jobui->window, output);
    wrefresh(jobui->window);
    pthread_mutex_unlock(&curses_lock);
    
    return 0;
}

int updateFinalView(finaljobui_t *final) {
    // Set up a string to hold the fully-formatted output
    char output[LINESIZE];
    memset(output, 0, LINESIZE);
    
    // Format the image name
    char name[40];
    memset(name, 0, 40);
    int namelen = strlen(final->name);
    sprintf(name, "Final: %s", final->name);
    for(int i = namelen + 7; i < 39; i++)
        name[i] = ' ';
    name[39] = 0;

    // Format the status bar
    char statusbar[53];
    memset(statusbar, 0, 53);
    int num_blocks = final->percent / 2;
    statusbar[0] = '[';
    for(int i = 1; i <= 50; i++) {
        statusbar[i] = (i <= num_blocks) ? '=' : ' ';
    }
    statusbar[51] = ']';
    statusbar[52] = 0;
    
    // Format the percent complete indicator
    char percentage[6];
    memset(percentage, 0, 6);
    sprintf(percentage, "%i%%%%", final->percent);
    
    // Form the combined output string
    sprintf(output, "%s              %s %s", name, statusbar, percentage);
    output[LINESIZE] = 0;
    
    // Print the string to the terminal
    pthread_mutex_lock(&curses_lock);
    wmove(final->window, 0, 0);
    wprintw(final->window, output);
    wrefresh(final->window);
    pthread_mutex_unlock(&curses_lock);

    return 0;
}

int updateJobUIState(jobui_t *jobui, int state) {
    jobui->state = state;
    switch(state) {
        case UI_STATE_PENDING:
            jobui->percent = 0;
        case UI_STATE_RECEIVING:
            jobui->percent += 0;
            break;
        case UI_STATE_PROCESSING:
            jobui->percent += 10;
            break;
        case UI_STATE_LOCALCHK:
            jobui->percent += 5;
            break;
        case UI_STATE_REMOTECHK:
            jobui->percent += 25;
            break;
        case UI_STATE_PREPARING:
            jobui->percent += 25;
            break;
        case UI_STATE_RENDERING:
            jobui->percent += 5;
            break;
        case UI_STATE_SENDING:
            jobui->percent += 20;
            break;
        case UI_STATE_COMPLETE:
            jobui->percent += 10;
            break;
    }

    return 0;
}

int updateFinalUIState(finaljobui_t *final, int percentage) {
    final->percent = percentage;
    
    return 0;
}