/*
Author:libing.
Date:2010-01-14
*/
#include <errno.h>
#include <assert.h>
#include<semaphore.h>
#include<pthread.h> 
#include "eventset.h"

#define SET_BIT(nr,p) do { \
	*p |= (1<<nr); \
	}while(0)

#define CLR_BIT(nr,p) do{ \
	*p &= ~(1<<nr); \
	}while(0)

#define TEST_BIT(nr,p) (*p&(1<<nr))

static inline int  find_first_bit(unsigned long  * num)
{
	 int i = 0 ;
	 for(i=0;i<MAX_EVENTS;i++){
	 	if(TEST_BIT(i, num)){
	 		break;
	 	}
	 }
	 if(i>=MAX_EVENTS) i=-1;
	 return i;
}


/*
static inline  unsigned char  test_bit(unsigned char bit,unsigned long * num)
{
	if( (((*num)>>bit)&0x01) ==1){
		return 1;
	}
	else{
		return 0;
	}
}

static inline unsigned char clear_bit(unsigned char bit,unsigned long  * num)
{
	unsigned char bit_value[]={1,2,4,8,16,32,64,128};
	return *num = (*num)&(~bit_value[bit]);
}

static inline unsigned char set_bit(unsigned char bit,unsigned long  * num)
{
         unsigned char  bit_value[]={1,2,4,8,16,32,64,128};
	 return   (*num) = (*num)|bit_value[bit];
}

static inline unsigned char  find_first_bit(unsigned char  * num,unsigned char max)
{
	 unsigned char i = 0 ;
	 for(i=0;i<sizeof(unsigned char) * 8 && i < max;i++){
	 	if(test_bit(i, num)){
	 		break;
	 	}
	 }
	 return i;
}

void mulevent_init(pmulevent_t pmulevent)
{
	assert(pmulevent); 
	pmulevent->sig = 0;
	sem_init(&pmulevent->sem,0,0);
	pthread_mutex_init(&pmulevent->lock,NULL);
}

int mulevent_set_event(pmulevent_t pmulevent,unsigned char nr)
{ 
	assert(nr >=0 && nr < 8);
	pthread_mutex_lock(&pmulevent->lock); 
	if(test_bit(nr,&pmulevent->sig)){
		pthread_mutex_unlock(&pmulevent->lock);
		return -1;
	}
	set_bit(nr,&pmulevent->sig);
	sem_post(&pmulevent->sem);
	pthread_mutex_unlock(&pmulevent->lock);

	return 0;
}

void mulevent_clear_event(pmulevent_t pmulevent,unsigned char nr)
{ 
	int ret = 0;
	assert(nr >=0 && nr < 8);
	pthread_mutex_lock(&pmulevent->lock);
	if(test_bit(nr,&pmulevent->sig)){
		 clear_bit(nr,&pmulevent->sig);
		 ret =sem_trywait(&pmulevent->sem);
	}
	pthread_mutex_unlock(&pmulevent->lock);
}

unsigned int mulevent_wait(pmulevent_t pmulevent)
{
	unsigned int nr=0;
	sem_wait(&pmulevent->sem);
	pthread_mutex_lock(&pmulevent->lock);
	nr = find_first_bit(&pmulevent->sig,8);
	assert(nr< 8);
	if(nr< 8){
		clear_bit(nr,&pmulevent->sig);
	}
	pthread_mutex_unlock(&pmulevent->lock);
	return nr;
}

long mulevent_wait_timeout(pmulevent_t pmulevent,const struct timespec *abs_timeout)
{
	long nr=0;
	while( sem_timedwait(&pmulevent->sem,abs_timeout)){	
		if( errno==ETIMEDOUT) return ETIMEDOUT;
	}
	pthread_mutex_lock(&pmulevent->lock);
	nr = find_first_bit(&pmulevent->sig,8);
	assert(nr< 8);
	if(nr< 8){
		clear_bit(nr,&pmulevent->sig);
	}
	pthread_mutex_unlock(&pmulevent->lock);
	return nr;
}

void mulevent_destroy(pmulevent_t pmulevent)
{
	sem_destroy(&pmulevent->sem);
}


typedef struct _MULEVENT{
 unsigned char sig;
 pthread_mutex_t   lock;
 sem_t sem;
}mulevent_t,*pmulevent_t;

*/

//a better conditional variable impl TODO
struct event_set{	
	unsigned long sig;
	pthread_mutex_t  lock;
	pthread_cond_t condition;
};
int eventset_create(EventSet* inst)
{
	struct event_set* new_eventset;

	if(!inst) return -1;
	new_eventset = (struct event_set*)malloc(sizeof(struct event_set));
	if(!new_eventset)
		return -1;
	
	new_eventset->sig = 0;
	pthread_cond_init(&new_eventset->condition,NULL);
	pthread_mutex_init(&new_eventset->lock,NULL);

	*inst = new_eventset;

	return 0;
	
}
void eventset_destroy(EventSet inst)
{
	struct event_set* set = inst;
	if(set)
	{
		pthread_cond_destroy(&set->condition);
		pthread_mutex_destroy(&set->lock);
		free(set);		
	}
	
}
void eventset_set(EventSet inst,unsigned char event)
{
	struct event_set* set = inst;
	if(set)
	{	
		pthread_mutex_lock(&set->lock); 
		SET_BIT(event,&set->sig);
		pthread_cond_signal(&set->condition);		
		pthread_mutex_unlock(&set->lock);		
	}
	
}
void eventset_clr(EventSet inst,unsigned char event)
{
	struct event_set* set = inst;
	if(set)
	{
		pthread_mutex_lock(&set->lock);		
		CLR_BIT(event,&set->sig);
		pthread_cond_signal(&set->condition);		
		pthread_mutex_unlock(&set->lock);		
	}

}
int eventset_wait(EventSet inst)
{
	struct event_set* set = inst;
	int nr=0;	
	if(set)
	{
		pthread_mutex_lock(&set->lock);
		pthread_cond_wait(&set->condition,&set->lock);		
		nr = find_first_bit(&set->sig);
		if(nr< MAX_EVENTS){
			CLR_BIT(nr,&set->sig);
		}
		pthread_mutex_unlock(&set->lock);
	}

	return nr;
	
}
int eventset_wait_timeout(EventSet inst,const int ms)
{
	struct event_set* set = inst;
	int nr=0;	
	if(set)
	{
		struct timespec to; 
		int err;
		if(ms>0){
			to.tv_sec=time(NULL)+ms/1000;
			to.tv_nsec=(ms%1000)*1000;
			pthread_mutex_lock(&set->lock);
			err = pthread_cond_timedwait(&set->condition,&set->lock,&to);
		}else {
			err= pthread_cond_wait(&set->condition,&set->lock);
		}
		nr = find_first_bit(&set->sig);
		if(nr< MAX_EVENTS){
			CLR_BIT(nr,&set->sig);
		}
		if(err ==ETIMEDOUT&&nr<0)
		{
			//even timeout occurs,we must check if the event bit set,
			//because pthread conditional variable is auto-reset
			pthread_mutex_unlock(&set->lock);
			return ETIMEDOUT;
		}
		
		pthread_mutex_unlock(&set->lock);

		return nr;
		
	}
	
	return nr;
}

