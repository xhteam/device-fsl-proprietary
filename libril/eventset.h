#ifndef _EVENTSET_H
#define _EVENTSET_H

typedef void* EventSet;
#define MAX_EVENTS 32
int eventset_create(EventSet* inst);
void eventset_destroy(EventSet inst);
void eventset_set(EventSet inst,unsigned char event);
void eventset_clr(EventSet inst,unsigned char event);
int eventset_wait(EventSet inst);
int eventset_wait_timeout(EventSet inst,const int ms);


#endif
