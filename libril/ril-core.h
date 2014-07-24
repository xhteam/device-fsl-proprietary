#ifndef _RIL_CORE_H_
#define _RIL_CORE_H_


#include "ril-debug.h"
#include "ril-config.h"
#include "ril-hardware.h"
#include "ril-fake.h"

enum{
	RIL_SMS_MEM_BM=0,
	RIL_SMS_MEM_ME=1,
	RIL_SMS_MEM_MT=2,
	RIL_SMS_MEM_SM=3,
	RIL_SMS_MEM_TA=4,
	RIL_SMS_MEM_SR=5,
	RIL_SMS_MEM_MAX
};
typedef struct ril_config
{	
	unsigned sms_mem;
}ril_config_t;

typedef struct ril_status
{
	unsigned int network_state:3;
	unsigned int data_network_state:3;
	unsigned int hardware_available:1;
	unsigned int screen_state:1;
	unsigned int radio_state:4;
}ril_status_t;

typedef struct ril_context
{
	ril_config_t config;
	ril_status_t status;
}ril_context_t;

typedef struct cdma_network_context{
	int valid;
	int bsid;
	int sid;
	int nid;
	int pn;
}cdma_network_context;

extern ril_context_t ril_context;
extern cdma_network_context cdma_context;

//fast access method
#define ril_status(x) ril_context.status.x
#define ril_config(x) ril_context.config.x

extern const struct RIL_Env *s_rilenv;
extern int s_closed;

#define RIL_onRequestComplete(t, e, response, responselen) s_rilenv->OnRequestComplete(t,e, response, responselen)
#define RIL_onUnsolicitedResponse(a,b,c) s_rilenv->OnUnsolicitedResponse(a,b,c)
#define RIL_requestTimedCallback(a,b,c) s_rilenv->RequestTimedCallback(a,b,c)

extern char* s_service_port;
extern char* s_modem_port;

extern unsigned int   radio_state_not_ready;
extern unsigned int   radio_state_ready;
extern unsigned int   radio_state_locked_or_absent;
extern unsigned int   ril_apptype;
extern unsigned int   ril_persosubstate_network;

extern PST_RIL_HARDWARE rilhw;

int modem_init(void);

void enqueueRILEvent(void (*callback) (void *param),
                     void *param, const struct timespec *relativeTime);


#endif
