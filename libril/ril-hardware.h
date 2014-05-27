#ifndef _RIL_HARDWARE_H
#define _RIL_HARDWARE_H

enum
{
	kRIL_HW_UNKNOWN = 0,
	kRIL_HW_MF210,	
	kRIL_HW_MC2716,	
	kRIL_HW_AD3812,
	kRIL_HW_M305,
	kRIL_HW_MC8630,
	kRIL_HW_MU509,
	kRIL_HW_WM630,
	kRIL_HW_CWM930,	
	kRIL_HW_SEW290,
	kRIL_HW_SEV850,
	kRIL_HW_ANYDATA,
	kRIL_HW_EM350,
	kRIL_HW_MAX,	
	kRIL_HW_MG3732 = kRIL_HW_AD3812
};
enum
{
	kPREFER_NETWORK_TYPE_WCDMA = 1,
	kPREFER_NETWORK_TYPE_CDMA_EVDV = 2,
	kPREFER_NETWORK_TYPE_TD_SCDMA = 3 ,
	kPREFER_NETWORK_TYPE_DEFAULT = kPREFER_NETWORK_TYPE_WCDMA
};

struct ril_hardware;

typedef int (*rilhw_hook)(struct ril_hardware* rilhw,int event);
typedef struct ril_hardware
{
	unsigned int model;
	const char* model_name;
	const char* alias_name;
	int		    prefer_net;
	unsigned long vid;
	unsigned long pid;
	const char*   service_port;
	const char*   modem_port;
	const char*   voice_port;
	const char*   voice_format;
	const char*   voice_rw_period;

	unsigned int no_pinonoff:1;/*disable pin onoff function*/
	unsigned int no_suspend:1;/*disable runtime suspend feature*/
	unsigned int no_ipstack:1;/*prefer to using embedded tcpip stack.if no ipstack,using hosted ip stack*/
	unsigned int no_simio:1;

	rilhw_hook hook;
	//internal use
	void* private;
	
	
}ST_RIL_HARDWARE,*PST_RIL_HARDWARE;


int rilhw_init(void);

void rilhw_found(PST_RIL_HARDWARE *found);

//state 
//0:off
//1:on 
//2:reset
//3:invalid
enum{
 kRequestStateOff=0,
 kRequestStateOn,
 kRequestStateReset,
 kRequestStateInvalid,  /*Ellie*/
};
int rilhw_power(PST_RIL_HARDWARE hardware,int reqstate);
int rilhw_autosuspend(PST_RIL_HARDWARE hardware,int enable);
int rilhw_power_state(void);

//wakeup modem 
int rilhw_wakeup(PST_RIL_HARDWARE hardware,int wake);

//int rilhw_link_change(void);
int rilhw_notify_screen_state(int state);

//wake lock mainly for in call use
int rilhw_acquire_wakelock();
int rilhw_release_wakelock();

#ifdef ANDROID_PROPERTY_WATCH
#include <cutils/properties.h>
struct prop_watch;

typedef int (*notifier_call)(struct prop_watch *, char* , char*);
struct prop_watch{
	struct prop_watch *next;

	
	notifier_call notifier;

	char prop_key[PROPERTY_KEY_MAX];
	void *priv;
};

int init_prop_watch(struct prop_watch* pw,notifier_call __notifier,char* __prop_key,void* __priv);
int register_prop_watch(struct prop_watch* pw);



#endif

#endif 


