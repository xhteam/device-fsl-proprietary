#include <telephony/ril.h>
#include <telephony/ril_cdma_sms.h>
#include <telephony/ril_ptt.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <alloca.h>
#include <getopt.h>
#include <sys/socket.h>
#include <termios.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>
#include <cutils/sockets.h>
#include <stdbool.h>

#include "atchannel.h"
#include "at_tok.h"
#include "misc.h"
#include "ril-handler.h"
#include "sms.h"
#include "ril-requestdatahandler.h"
#include "ptt.h"

#define MODEM_POWER_PATCH
#define TIMEOUT_SEARCH_FOR_TTY 5 /* Poll every Xs for the port*/
#define TIMEOUT_EMRDY 10 /* Module should respond at least within 10s */
#define TIMEOUT_MODEM_POWER_PATCH 6
static void requestDebug(void *data, size_t datalen, RIL_Token t);

static void pttDebug(void *data, size_t datalen, RIL_Token t)
{
    const char* cmd = ((const char **)data)[0];
    char* line = strdup(cmd);
    int action;
    int err;
    DBG("%s[%s]",__func__,line);
    err = at_tok_start(&line);    
    err = at_tok_nextint(&line, &action);
    if (err < 0) goto action_failed;
    if(1==action){
      get_ptt_group_info();
    }else if(2==action){
      int gid,priority;
      priority=15;
      err = at_tok_nextint(&line, &gid);
      if (err < 0) goto action_failed;
      if(at_tok_hasmore(&line)){
	at_tok_nextint(&line, &priority);
      }
      join_ptt_group(gid,priority);
    }else if(3==action){
      int inst,priority,pid;       
      err = at_tok_nextint(&line, &inst);
      if (err < 0) goto action_failed;
      err = at_tok_nextint(&line, &priority);
      if (err < 0) goto action_failed;
      err = at_tok_nextint(&line, &pid);
      if (err < 0) goto action_failed;
      priority=15;     
      request_ptt_group_master_call(inst,eAirInterfaceServiceVoiceGroupCall,priority,pid);
    }else if(4==action){
      int inst,pid;
      err = at_tok_nextint(&line, &inst);
      if (err < 0) goto action_failed;
      err = at_tok_nextint(&line, &pid);
      if (err < 0) goto action_failed;
      release_ptt_group_master_call(inst,pid);
    }else if(5==action){
      int inst,pid;
      err = at_tok_nextint(&line, &inst);
      if (err < 0) goto action_failed;
      err = at_tok_nextint(&line, &pid);
      if (err < 0) goto action_failed;
      request_ptt_group_p2p_call(inst,eAirInterfaceServiceVoiceP2PCall,15,pid);
    }else if(6==action){
      hangup_ptt_group_p2p_call();
    }else if(7==action){
      hook_ptt_group_p2p_call();
    }

    free(line);
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
    return;
action_failed:
    DBG("ptt debug failed");
    free(line);
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
#define RIL_REQUEST_ENTRY(req,dispatch) {req,dispatch}

typedef void (*request_dispatch)(void *data, size_t datalen, RIL_Token t);
typedef struct _ril_request
{
    int request;
    request_dispatch disp;
}RIL_REQUEST,*PRIL_REQUEST;


static RIL_REQUEST RIL_DISP_TABLE[]=
{

	//Basic Voice Call
	RIL_REQUEST_ENTRY(RIL_REQUEST_LAST_CALL_FAIL_CAUSE,requestLastCallFailCause),
	RIL_REQUEST_ENTRY(RIL_REQUEST_GET_CURRENT_CALLS,requestGetCurrentCalls),
	RIL_REQUEST_ENTRY(RIL_REQUEST_DIAL,requestDial),
	RIL_REQUEST_ENTRY(RIL_REQUEST_HANGUP,requestHangup),
	RIL_REQUEST_ENTRY(RIL_REQUEST_ANSWER,requestAnswer),

	/* Advanced Voice Call */
	RIL_REQUEST_ENTRY(RIL_REQUEST_GET_CLIR,requestGetCLIR),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SET_CLIR,requestSetCLIR),
	RIL_REQUEST_ENTRY(RIL_REQUEST_QUERY_CALL_FORWARD_STATUS,requestQueryCallForwardStatus),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SET_CALL_FORWARD,requestSetCallForward),
	RIL_REQUEST_ENTRY(RIL_REQUEST_QUERY_CALL_WAITING,requestQueryCallWaiting),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SET_CALL_WAITING,requestSetCallWaiting),
	RIL_REQUEST_ENTRY(RIL_REQUEST_UDUB,requestUDUB),

	RIL_REQUEST_ENTRY(RIL_REQUEST_SCREEN_STATE,requestScreenState),
	RIL_REQUEST_ENTRY(RIL_REQUEST_QUERY_CLIP,requestQueryClip),
	RIL_REQUEST_ENTRY(RIL_REQUEST_DTMF,requestDTMF),
	RIL_REQUEST_ENTRY(RIL_REQUEST_DTMF_START,requestDTMFStart),
	RIL_REQUEST_ENTRY(RIL_REQUEST_DTMF_STOP,requestDTMFStop),
	RIL_REQUEST_ENTRY(RIL_REQUEST_CDMA_BURST_DTMF,requestCDMADTMF),
	
	
	RIL_REQUEST_ENTRY(RIL_REQUEST_GET_MUTE,requestGetMute),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SET_MUTE,requestSetMute),

	/* Multiparty Voice Call */
	RIL_REQUEST_ENTRY(RIL_REQUEST_HANGUP_WAITING_OR_BACKGROUND,requestHangupWaitingOrBackground),
	RIL_REQUEST_ENTRY(RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND,requestHangupForegroundResumeBackground),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE,requestSwitchWaitingOrHoldingAndActive),
	RIL_REQUEST_ENTRY(RIL_REQUEST_CONFERENCE,requestConference),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SEPARATE_CONNECTION,requestSeparateConnection),
	RIL_REQUEST_ENTRY(RIL_REQUEST_EXPLICIT_CALL_TRANSFER,requestExplicitCallTransfer),

	/* Data Call Requests */
	RIL_REQUEST_ENTRY(RIL_REQUEST_SETUP_DATA_CALL,requestSetupDataCall),
	RIL_REQUEST_ENTRY(RIL_REQUEST_DEACTIVATE_DATA_CALL,requestDeactivateDataCall),
	RIL_REQUEST_ENTRY(RIL_REQUEST_LAST_DATA_CALL_FAIL_CAUSE,requestLastDataCallFailCause),
	RIL_REQUEST_ENTRY(RIL_REQUEST_DATA_CALL_LIST,requestDataCallList),

	 /* SMS Requests */
	RIL_REQUEST_ENTRY(RIL_REQUEST_SEND_SMS,requestSendSMS),
	RIL_REQUEST_ENTRY(RIL_REQUEST_WRITE_SMS_TO_SIM,requestWriteSmsToSim),
	RIL_REQUEST_ENTRY(RIL_REQUEST_DELETE_SMS_ON_SIM,requestDeleteSmsOnSim),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SMS_ACKNOWLEDGE,requestSMSAcknowledge),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SEND_SMS_EXPECT_MORE,requestSendSMSExpectMore),
	RIL_REQUEST_ENTRY(RIL_REQUEST_GET_SMSC_ADDRESS,requestGetSMSCAddress),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SET_SMSC_ADDRESS,requestSetSMSCAddress),
	RIL_REQUEST_ENTRY(RIL_REQUEST_GSM_GET_BROADCAST_SMS_CONFIG,requestGSMGetBroadcastSMSConfig),
	RIL_REQUEST_ENTRY(RIL_REQUEST_GSM_SET_BROADCAST_SMS_CONFIG,requestGSMSetBroadcastSMSConfig),
	RIL_REQUEST_ENTRY(RIL_REQUEST_GSM_SMS_BROADCAST_ACTIVATION,requestGSMSMSBroadcastActivation),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SET_SUPP_SVC_NOTIFICATION,requestSetSuppSvcNotification),
	//CDMA sms	
	RIL_REQUEST_ENTRY(RIL_REQUEST_CDMA_SEND_SMS,requestCDMASendSMS),
	RIL_REQUEST_ENTRY(RIL_REQUEST_CDMA_SMS_ACKNOWLEDGE,requestCDMASMSAcknowledge),


	
	/* SIM Handling Requests */
	RIL_REQUEST_ENTRY(RIL_REQUEST_GET_SIM_STATUS,requestGetSimStatus),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SIM_IO,requestSIM_IO),
	RIL_REQUEST_ENTRY(RIL_REQUEST_ENTER_SIM_PIN,requestEnterSimPin),
	RIL_REQUEST_ENTRY(RIL_REQUEST_ENTER_SIM_PUK,requestEnterSimPin),
	RIL_REQUEST_ENTRY(RIL_REQUEST_ENTER_SIM_PIN2,requestEnterSimPin),
	RIL_REQUEST_ENTRY(RIL_REQUEST_ENTER_SIM_PUK2,requestEnterSimPin),
	RIL_REQUEST_ENTRY(RIL_REQUEST_CHANGE_SIM_PIN,requestChangeSimPin),
	RIL_REQUEST_ENTRY(RIL_REQUEST_CHANGE_SIM_PIN2,requestEnterSimPin),


	/* USSD Requests */
	RIL_REQUEST_ENTRY(RIL_REQUEST_SEND_USSD,requestSendUSSD),
	RIL_REQUEST_ENTRY(RIL_REQUEST_CANCEL_USSD,requestCancelUSSD),

	/* Network */
	RIL_REQUEST_ENTRY(RIL_REQUEST_REGISTRATION_STATE,requestRegistrationState),
	RIL_REQUEST_ENTRY(RIL_REQUEST_GPRS_REGISTRATION_STATE,requestGPRSRegistrationState),
	RIL_REQUEST_ENTRY(RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE,requestQueryNetworkSelectionMode),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC,requestSetNetworkSelectionAutomatic),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL,requestSetNetworkSelectionManual),
	RIL_REQUEST_ENTRY(RIL_REQUEST_QUERY_AVAILABLE_NETWORKS,requestQueryAvailableNetworks),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE,requestSetPreferredNetworkType),
	RIL_REQUEST_ENTRY(RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE,requestGetPreferredNetworkType),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SIGNAL_STRENGTH,requestSignalStrength),
	RIL_REQUEST_ENTRY(RIL_REQUEST_OPERATOR,requestOperator),
	RIL_REQUEST_ENTRY(RIL_REQUEST_RADIO_POWER,requestRadioPower),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SET_LOCATION_UPDATES,requestSetLocationUpdates),
	RIL_REQUEST_ENTRY(RIL_REQUEST_GET_NEIGHBORING_CELL_IDS,requestNeighboringCellIds),

	RIL_REQUEST_ENTRY(RIL_REQUEST_CDMA_SUBSCRIPTION,requestCDMASubScription),

	RIL_REQUEST_ENTRY(RIL_REQUEST_QUERY_FACILITY_LOCK,requestQueryFacilityLock),
	RIL_REQUEST_ENTRY(RIL_REQUEST_SET_FACILITY_LOCK,requestSetFacilityLock),

	/* OEM */
	RIL_REQUEST_ENTRY(RIL_REQUEST_OEM_HOOK_RAW,requestOEMHookRaw),
	RIL_REQUEST_ENTRY(RIL_REQUEST_OEM_HOOK_STRINGS,requestOEMHookStrings),
	RIL_REQUEST_ENTRY(RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING,requestReportSTKServiceIsRunning),

	/* Device */
	RIL_REQUEST_ENTRY(RIL_REQUEST_GET_IMSI,requestGetIMSI),
	RIL_REQUEST_ENTRY(RIL_REQUEST_GET_IMEI,requestGetIMEI),
	RIL_REQUEST_ENTRY(RIL_REQUEST_GET_IMEISV,requestGetIMEISV),
	RIL_REQUEST_ENTRY(RIL_REQUEST_DEVICE_IDENTITY,requestDeviceIdentity),
	RIL_REQUEST_ENTRY(RIL_REQUEST_BASEBAND_VERSION,requestBasebandVersion),

	/*STK*/
	/* not ready
	RIL_REQUEST_ENTRY(RIL_REQUEST_STK_GET_PROFILE,requestSTKGetProfile),
	RIL_REQUEST_ENTRY(RIL_REQUEST_STK_SET_PROFILE,requestSTKSetProfile),
	RIL_REQUEST_ENTRY(RIL_REQUEST_STK_SEND_ENVELOPE_COMMAND,requestSTKSendEnvelopeCommand),
	RIL_REQUEST_ENTRY(RIL_REQUEST_STK_SEND_TERMINAL_RESPONSE,requestSTKSendTerminalResponse),
	*/
	/*PTT*/	
	RIL_REQUEST_ENTRY(RIL_REQUEST_PTT_QUERY_AVAILABLE_GROUPS,requestPttQueryAvailableGroups),
	RIL_REQUEST_ENTRY(RIL_REQUEST_PTT_GROUP_SETUP,requestPttGroupSetup),
	RIL_REQUEST_ENTRY(RIL_REQUEST_PTT_GROUP_RELEASE,requestPttGroupRelease),
	RIL_REQUEST_ENTRY(RIL_REQUEST_PTT_CALL_DIAL,requestPttCallDial),
	RIL_REQUEST_ENTRY(RIL_REQUEST_PTT_CALL_HANGUP,requestPttCallHangup),
	RIL_REQUEST_ENTRY(RIL_REQUEST_PTT_CURRENT_GROUP_SCANLIST_UPDATE,requestPttCurrentGroupScanlistUpdate),
	RIL_REQUEST_ENTRY(RIL_REQUEST_PTT_QUERY_BLOCKED_INDICATOR,requestPttQueryBlockedIndicator),
	RIL_REQUEST_ENTRY(RIL_REQUEST_PTT_DEVICE_INFO,requestPttDeviceInfo),
	RIL_REQUEST_ENTRY(RIL_REQUEST_PTT_QUERY_BIZ_STATE,requestPttBizState),
	
	RIL_REQUEST_ENTRY(RIL_REQUEST_PTT_DEBUG,pttDebug),
	RIL_REQUEST_ENTRY(RIL_REQUEST_RIL_DEBUG,requestDebug),
	RIL_REQUEST_ENTRY(-1,requestDebug),

	//terminate
	RIL_REQUEST_ENTRY(0,NULL),
};

extern const char * requestToString(int request);


const struct RIL_Env *s_rilenv;


//local flags
unsigned int	s_flags = (RIL_FLAG_ERROR|RIL_FLAG_WARN
							|RIL_FLAG_DEBUG);

//service port
static int 				s_port = -1;
static int          	s_device_socket = 0;
//static 
		char * 			s_service_port = NULL;
static char* 			s_def_service_port = "/dev/ttyUSB1";

//modem port
//static 
		char * 			s_modem_port = NULL;
static char* 			s_def_modem_port="/dev/ttyUSB0";

		int   s_cdma_device = 0;

unsigned int   radio_state_not_ready;
unsigned int   radio_state_ready;
unsigned int   radio_state_locked_or_absent;
unsigned int   ril_apptype;
unsigned int   ril_persosubstate_network;

PST_RIL_HARDWARE rilhw;		

ril_context_t ril_context;
cdma_network_context cdma_context;



/* trigger change to this with s_state_cond */
int s_closed = 0;

#define timespec_cmp(a, b, op)   \
    ((a).tv_sec == (b).tv_sec    \
     ? (a).tv_nsec op(b).tv_nsec \
     : (a).tv_sec op(b).tv_sec)

enum RequestGroups {
    CMD_QUEUE_DEFAULT = 0,
    CMD_QUEUE_AUXILIARY = 1,
    CMD_QUEUE_CHANNELS
};

typedef struct RILRequest {
    int request;
    void *data;
    size_t datalen;
    RIL_Token token;
    struct RILRequest *next;
} RILRequest;

typedef struct RILEvent {
    void (*eventCallback) (void *param);
    void *param;
    struct timespec abstime;
    struct RILEvent *next;
    struct RILEvent *prev;
} RILEvent;

typedef struct RequestQueue {
    pthread_mutex_t queueMutex;
    pthread_cond_t cond;
    RILRequest *requestList;
    RILEvent *eventList;
    char enabled;
    char closed;
} RequestQueue;

typedef struct RILRequestGroup {
    int group;
    char *name;
    int *requests;
    RequestQueue *requestQueue;
} RILRequestGroup;

static RequestQueue s_requestQueueDefault = {
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    NULL,
    NULL,
    1,
    1
};

static RequestQueue *s_requestQueues[] = {
    &s_requestQueueDefault
};

static RILRequestGroup RILRequestGroups[] = {
    {CMD_QUEUE_DEFAULT, "default", NULL, &s_requestQueueDefault},
};



static void requestDebug(void *data, size_t datalen, RIL_Token t)
{
    const char* cmd = ((const char **)data)[0];
    if(cmd){	
        ATResponse *atresponse = NULL;
	int err = at_send_command_raw(cmd, &atresponse);
	if(!err){
	    RIL_onRequestComplete(t, RIL_E_SUCCESS, 0,0);
	}
	else
	    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	at_response_free(atresponse);
    }else {
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    }    
}
/**
 * Enqueue a RILEvent to the request queue.
 */
void enqueueRILEvent(void (*callback) (void *param),
                     void *param, const struct timespec *relativeTime)
{
    int err;
    struct timespec ts;
    char done = 0;
    RequestQueue *q = NULL;

    RILEvent *e = (RILEvent *) malloc(sizeof(RILEvent));
    memset(e, 0, sizeof(RILEvent));

    e->eventCallback = callback;
    e->param = param;

    if (relativeTime == NULL) {
        relativeTime = (const struct timespec *) alloca(sizeof(struct timespec));
        memset((struct timespec *) relativeTime, 0, sizeof(struct timespec));
    }

    clock_gettime(CLOCK_MONOTONIC, &ts);

    e->abstime.tv_sec = ts.tv_sec + relativeTime->tv_sec;
    e->abstime.tv_nsec = ts.tv_nsec + relativeTime->tv_nsec;

    if (e->abstime.tv_nsec > 1000000000) {
        e->abstime.tv_sec++;
        e->abstime.tv_nsec -= 1000000000;
    }

    q = &s_requestQueueDefault;

again:
    if ((err = pthread_mutex_lock(&q->queueMutex)) != 0)
        ERROR("%s() failed to take queue mutex: %s!", __func__, strerror(err));

    if (q->eventList == NULL) {
        q->eventList = e;
    } else {
        if (timespec_cmp(q->eventList->abstime, e->abstime, > )) {
            e->next = q->eventList;
            q->eventList->prev = e;
            q->eventList = e;
        } else {
            RILEvent *tmp = q->eventList;
            do {
                if (timespec_cmp(tmp->abstime, e->abstime, > )) {
                    tmp->prev->next = e;
                    e->prev = tmp->prev;
                    tmp->prev = e;
                    e->next = tmp;
                    break;
                } else if (tmp->next == NULL) {
                    tmp->next = e;
                    e->prev = tmp;
                    break;
                }
                tmp = tmp->next;
            } while (tmp);
        }
    }

    if ((err = pthread_cond_broadcast(&q->cond)) != 0)
        ERROR("%s() failed to take broadcast queue update: %s!",
            __func__, strerror(err));

    if ((err = pthread_mutex_unlock(&q->queueMutex)) != 0)
        ERROR("%s() failed to release queue mutex: %s!",
            __func__, strerror(err));

}


static RequestQueue *getRequestQueue(int request)
{
    return RILRequestGroups[CMD_QUEUE_DEFAULT].requestQueue;
}


/*** Callback methods from the RIL library to us ***/
static const RIL_CardStatus staticSimStatus = {
    .card_state = RIL_CARDSTATE_ABSENT,
    .universal_pin_state = RIL_PINSTATE_UNKNOWN,
    .gsm_umts_subscription_app_index = 0,
    .cdma_subscription_app_index = 0,
    .num_applications = 0
};

static bool requestStateFilter(int request, RIL_Token t)
{
	int s_state=sState;
    /*
     * These commands will not accept RADIO_NOT_AVAILABLE and cannot be executed
     * before we are in SIM_STATE_READY so we just return GENERIC_FAILURE if
     * not in SIM_STATE_READY.
     */
    if (s_state != RADIO_STATE_SIM_READY
        && (request == RIL_REQUEST_WRITE_SMS_TO_SIM ||
            request == RIL_REQUEST_DELETE_SMS_ON_SIM)) {
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        return true;
    }

    /* Ignore all requsts while is radio_state_unavailable */
    if (s_state == RADIO_STATE_UNAVAILABLE) {
        /*
         * The following command(s) must never fail. Return static state for
         * these command(s) while in RADIO_STATE_UNAVAILABLE.
         */
        if (request == RIL_REQUEST_GET_SIM_STATUS) {
            RIL_onRequestComplete(t, RIL_REQUEST_GET_SIM_STATUS,
                                  (char *) &staticSimStatus,
                                  sizeof(staticSimStatus));
        }
        /*
         * The following command must never fail. Return static state for this
         * command while in RADIO_STATE_UNAVAILABLE.
         */
        else if (request == RIL_REQUEST_SCREEN_STATE) {
            RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        }
        /* Ignore all other requests when RADIO_STATE_UNAVAILABLE */
        else {
            RIL_onRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
        }
        return true;
    }

    /*
     * Ignore all non-power requests when RADIO_STATE_OFF
     * (except RIL_REQUEST_RADIO_POWER and
     * RIL_REQUEST_GET_SIM_STATUS and a few more).
     * This is according to reference RIL implementation.
     * Note that returning RIL_E_RADIO_NOT_AVAILABLE for all ignored requests
     * causes Android Telephony to enter state RADIO_NOT_AVAILABLE and block
     * all communication with the RIL.
     */
    if (s_state == RADIO_STATE_OFF
        && !(request == RIL_REQUEST_RADIO_POWER ||
             request == RIL_REQUEST_STK_GET_PROFILE ||
             request == RIL_REQUEST_STK_SET_PROFILE ||
             request == RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING ||
             request == RIL_REQUEST_GET_SIM_STATUS ||
             request == RIL_REQUEST_GET_IMEISV ||
             request == RIL_REQUEST_GET_IMEI ||
             request == RIL_REQUEST_DEVICE_IDENTITY ||
             request == RIL_REQUEST_BASEBAND_VERSION ||
             request == RIL_REQUEST_SCREEN_STATE)) {
        RIL_onRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
        return true;
    }

    /*
     * Ignore all non-power requests when RADIO_STATE_OFF
     * and RADIO_STATE_SIM_NOT_READY (except RIL_REQUEST_RADIO_POWER
     * and a few more).
     */
    if ((s_state == RADIO_STATE_OFF || s_state == RADIO_STATE_SIM_NOT_READY)
        && !(request == RIL_REQUEST_RADIO_POWER ||
             request == RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING ||
             request == RIL_REQUEST_GET_SIM_STATUS ||
             request == RIL_REQUEST_GET_IMEISV ||
             request == RIL_REQUEST_GET_IMEI ||
             request == RIL_REQUEST_DEVICE_IDENTITY ||
             request == RIL_REQUEST_BASEBAND_VERSION ||
             request == RIL_REQUEST_SCREEN_STATE)) {
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        return true;
    }

    /*
     * Don't allow radio operations when sim is absent or locked!
     * DIAL, GET_CURRENT_CALLS, HANGUP and LAST_CALL_FAIL_CAUSE are
     * required to handle emergency calls.
     */
    if (s_state == RADIO_STATE_SIM_LOCKED_OR_ABSENT
        && !(request == RIL_REQUEST_ENTER_SIM_PIN ||
             request == RIL_REQUEST_ENTER_SIM_PUK ||
             request == RIL_REQUEST_ENTER_SIM_PIN2 ||
             request == RIL_REQUEST_ENTER_SIM_PUK2 ||
             request == RIL_REQUEST_ENTER_NETWORK_DEPERSONALIZATION ||
             request == RIL_REQUEST_GET_SIM_STATUS ||
             request == RIL_REQUEST_RADIO_POWER ||
             request == RIL_REQUEST_GET_IMEISV ||
             request == RIL_REQUEST_GET_IMEI ||
             request == RIL_REQUEST_BASEBAND_VERSION ||
             request == RIL_REQUEST_DIAL ||
             request == RIL_REQUEST_GET_CURRENT_CALLS ||
             request == RIL_REQUEST_HANGUP ||
             request == RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND ||
             request == RIL_REQUEST_SET_TTY_MODE ||
             request == RIL_REQUEST_QUERY_TTY_MODE ||
             request == RIL_REQUEST_DTMF ||
             request == RIL_REQUEST_DTMF_START ||
             request == RIL_REQUEST_DTMF_STOP ||
             request == RIL_REQUEST_LAST_CALL_FAIL_CAUSE ||
             request == RIL_REQUEST_SCREEN_STATE)) {
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        return true;
    }

    return false;
}


/*** Callback methods from the RIL library to us ***/

/**
 * Call from RIL to us to make a RIL_REQUEST
 *
 * Must be completed with a call to RIL_onRequestComplete()
 *
 * RIL_onRequestComplete() may be called from any thread, before or after
 * this function returns.
 *
 * Will always be called from the same thread, so returning here implies
 * that the radio is ready to process another command (whether or not
 * the previous command has completed).
 */
static void
processRequest (int request, void *data, size_t datalen, RIL_Token t)
{
	int err = RIL_E_REQUEST_NOT_SUPPORTED;

   if (requestStateFilter(request, t))
		return;

	PRIL_REQUEST pRequest=RIL_DISP_TABLE;
	while(pRequest)
	{
		if((pRequest->request == request)||
			!pRequest->request)
			break;
		pRequest++;
	}

	if(pRequest->disp)
	{
	    
		pRequest->disp(data,datalen,t);		
		err = RIL_E_SUCCESS;
	}
   
ERROR_HANDLER:
	if(err)
	{
		WARN("request[%s] can't be supported currently",requestToString(request));
		RIL_onRequestComplete(t, err, NULL, 0);
	}
		
}

/**
 * Synchronous call from the RIL to us to return current radio state.
 * RADIO_STATE_UNAVAILABLE should be the initial state.
 */
static RIL_RadioState
onStateRequest()
{
	if(!ril_status(hardware_available))
		return fake_ril_getState();
    return sState;
}
/**
 * Call from RIL to us to find out whether a specific request code
 * is supported by this implementation.
 *
 * Return 1 for "supported" and 0 for "unsupported"
 */

static int
onSupports (int requestCode)
{
    //@@@todo
    return 1;
}

static void onCancel (RIL_Token t)
{
    //@@@todo

}

static const char * getVersion(void)
{
    return RIL_DRIVER_VERSION;
}

/**%
 * Call from RIL to us to make a RIL_REQUEST.
 *
 * Must be completed with a call to RIL_onRequestComplete().
 */
static void onRequest(int request, void *data, size_t datalen, RIL_Token t)
{
	int s_state = sState;
    RILRequest *r;
    RequestQueue *q;
    int err;

    /* In radio state unavailable no requests are to enter the queues */
    if (s_state == RADIO_STATE_UNAVAILABLE) {
        (void)requestStateFilter(request, t);
        goto finally;
    }

    q = getRequestQueue(request);

    r = calloc(1, sizeof(RILRequest));
    assert(r != NULL);

    /* Formulate a RILRequest and put it in the queue. */
    r->request = request;
    r->data = dupRequestData(request, data, datalen);
    r->datalen = datalen;
    r->token = t;
    r->next = NULL;

    
    if ((err = pthread_mutex_lock(&q->queueMutex)) != 0) {
        ERROR("%s() failed to take queue mutex: %s!", __func__, strerror(err));
        assert(0);
    }

    /* Queue empty, just throw r on top. */
    if (q->requestList == NULL)
        q->requestList = r;
    else {
        RILRequest *l = q->requestList;
        while (l->next != NULL)
            l = l->next;

        l->next = r;
    }

    if ((err = pthread_cond_broadcast(&q->cond)) != 0)
        ERROR("%s() failed to broadcast queue update: %s!",
            __func__, strerror(err));

    if ((err = pthread_mutex_unlock(&q->queueMutex)) != 0)
        ERROR("%s() failed to release queue mutex: %s!",
            __func__, strerror(err));

finally:
    return;
}

void setPreferredMessageStorage()
{
    ATResponse *atresponse = NULL;
    char *tok = NULL;
    int used1, total1;
    int err = -1;

    err = at_send_command_singleline("AT+CPMS=\"SM\",\"SM\"","+CPMS: ",
                                     &atresponse);
    if (err < 0 || atresponse->success == 0)
        goto error;

    /*
     * Depending on the host boot time the indication that message storage
     * on SIM is full (+CIEV: 10,1) may be sent before the RIL is started.
     * The RIL will explicitly check status of SIM messages storage using
     * +CPMS intermediate response and inform Android if storage is full.
     * +CPMS: <used1>,<total1>,<used2>,<total2>,<used3>,<total3>
     */
    tok = atresponse->p_intermediates->line;

    err = at_tok_start(&tok);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&tok, &used1);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&tok, &total1);
    if (err < 0)
        goto error;

    if (used1&&used1 >= total1)
        RIL_onUnsolicitedResponse(RIL_UNSOL_SIM_SMS_STORAGE_FULL,NULL, 0);

    goto exit;

error:
    ERROR("%s() failed during AT+CPMS sending/handling!", __func__);

exit:
    at_response_free(atresponse);
    return;
}


#if 0
static int SIMReady(int wait_s)
{
	int ret=0;
    ATResponse *atresponse = NULL;
    int err;	
    ATCmeError cme_error_code = -1;
	//wait card init 10s
	do{
		err = at_send_command_singleline("AT+CPIN?","+CPIN:",&atresponse);
		if (err < 0 || atresponse == NULL) 
		{
			ret = SIM_NOT_READY;
		}
		else
		{
			at_get_cme_error(atresponse, &cme_error_code);
			switch (cme_error_code) 
			{        
				case CME_PHONE_FAILURE:  
					ret = SIM_READY;
					break;        
				case CME_SIM_NOT_INSERTED:  
				case CME_SIM_FAILURE:
					ret = SIM_ABSENT;     
					break;
				default: 
					ret = SIM_NOT_READY;      
					break;
			}    
		}
		if(SIM_NOT_READY == ret)
		{			
			WARN("sim not ready\n");
			sleep(1);
		}
		else if(SIM_ABSENT == ret)
		{
			WARN("sim not found\n");
			break;
		}
		else
			break;
	
	}while(--wait_s);

	at_response_free(atresponse);

	return ret;

}
#endif

int modem_init(void)
{
    ATResponse *p_response = NULL;
    int err;

    /* note: we don't check errors here. Everything important will
       be handled in onATTimeout and onATReaderClosed */

    /*  atchannel is tolerant of echo but it must */
    /*  have verbose result codes */
    at_send_command("ATE0", NULL);


    /*  Extended errors */
    at_send_command("AT+CMEE=1", NULL);

	
	//wait for SIM init
	//err = SIMReady(20);
	
    
	if(kRIL_HW_MC2716 ==rilhw->model||kRIL_HW_MC8630 ==rilhw->model)
	{
		//full function at initial		
		at_send_command("AT+CFUN=1", NULL); 
	
	    /*  No auto-answer */
	    at_send_command("ATS0=0", NULL);
		

	    /*  SMS TEXT mode */
	    at_send_command("AT+CMGF=1", NULL);
		//hang up previous call
	    //at_send_command("AT+CHV", NULL);
		//deactivate previous PDP
		//at_send_command("ATH",NULL);

		/*  new sms storage */
		if(ril_config(sms_mem)==RIL_SMS_MEM_SM)
	    {
	    	at_send_command("AT+CPMS=\"SM\",\"SM\",\"SM\"", NULL);
		}
		else
		{
	    	at_send_command("AT+CPMS=\"ME\",\"ME\",\"ME\"", NULL);			
		}

		/* CDMA/HDR hybrid mode*/
	    at_send_command("AT^PREFMODE=8", NULL);
	

	    at_send_command("AT+CTA=5", NULL);

		//at_send_command("AT^CVOICE=0", NULL);
		//enable ZTE voice function
		at_send_command("AT+ZCVF=2", NULL);

		//Ellie: adjust the volume to the maximum
		if(kRIL_HW_MC8630 ==rilhw->model)
			at_send_command("AT+CLVL=4", NULL);
		//report cdma network info
		at_send_command("AT+ZCED=1,1", NULL);

	}else if(kRIL_HW_EM350 ==rilhw->model){
	  /*  No auto-answer */
	    at_send_command("ATS0=0", NULL);


		//full function at initial
	    at_send_command("AT+CFUN=1", NULL);	

	    /*  Network registration events */
	    err = at_send_command("AT+CEREG=2", &p_response);

	    /* some handsets -- in tethered mode -- don't support CREG=2 */
	    if (err < 0 || p_response->success == 0) {
	        at_send_command("AT+CEREG=1", NULL);
	    }
	    at_response_free(p_response);

	    at_send_command("AT+CGREG=2", NULL);
	    at_send_command_singleline("AT^TVER?","^TVER:",&p_response);
	    at_response_free(p_response);
	    at_send_command_singleline("AT+CTBI?","+CTBI:", &p_response);
	    at_response_free(p_response);
	}
	else //GSM
	{
		
	    /*  No auto-answer */
	    at_send_command("ATS0=0", NULL);


		//full function at initial
	    at_send_command("AT+CFUN=1", NULL);	

	    /*  Network registration events */
	    err = at_send_command("AT+CREG=2", &p_response);

	    /* some handsets -- in tethered mode -- don't support CREG=2 */
	    if (err < 0 || p_response->success == 0) {
	        at_send_command("AT+CREG=1", NULL);
	   }
	    at_response_free(p_response);
	    /*  GPRS registration events */
	    at_send_command("AT+CGREG=2", NULL);

	    /*  auto zone update and report*/
	    at_send_command("AT+CTZU=1", NULL);
	   
	    /*  Call Waiting notifications */
	   at_send_command("AT+CCWA=1", NULL);

	 /*  Call Waiting notifications */
	at_send_command("AT+ZMDS=4", NULL);

	    /*  Alternating voice/data off */
	    at_send_command("AT+CMOD=0", NULL);

	    /*  +CSSU unsolicited supp service notifications */
	   at_send_command("AT+CSSN=0,1", NULL);

		/*  no connected line identification */
		at_send_command("AT+COLP=0", NULL);

		/* voice hangup mode 0*/
		at_send_command("AT+CVHU=0", NULL);
		
		/*  caller id = yes */
		at_send_command("AT+CLIP=1", NULL);

		/*	don't hide outgoing callerID */
		at_send_command("AT+CLIR=0", NULL);

	    /*  character set [GSM|UCS2] */
	   at_send_command("AT+CSCS=\"GSM\"", NULL);

	    /*  USSD unsolicited */
	    at_send_command("AT+CUSD=1", NULL);

	    at_send_command("AT+CTA=2", NULL);

	
	    /*  SMS in PDU mode */
	    at_send_command("AT+CMGF=0", NULL);

		if(kRIL_HW_M305 ==rilhw->model)
		{
			at_send_command("AT^ZVOUSB=1", NULL);			
		}
		else if(kRIL_HW_AD3812==rilhw->model)
		{		
			char value[PROPERTY_VALUE_MAX];						
 			memset(value,0,PROPERTY_VALUE_MAX);
			/*new command for module software release of BCD094*/
			at_send_command("AT+ZVCF=1", NULL);

			//set unsolicited command port
			at_send_command("AT^PORTSET=1", NULL);

			property_get("ril.vmodem.route",value,"analog");
			if(!strcmp(value,"analog")){
				at_send_command("AT+SPEAKER=1", NULL);
				at_send_command("AT+CLVL=4", NULL);
			}else {
				//digital usb port
				at_send_command("AT+SPEAKER=2", NULL);
				at_send_command("AT+ZHWPCM=0", NULL);
				
			}
		}
		else if(kRIL_HW_CWM930==rilhw->model)
		{		
			at_send_command("AT+CLVL=3", NULL);
		}
	}	

	return 0;
}



/**
 * Initialize everything that can be configured while we're still in
 * AT+CFUN=0
 */
static int initializeCommon(void)
{
	int ret;


	//ril context init
	ril_config(sms_mem) = RIL_SMS_MEM_SM;
	ril_status(network_state)=0;
	ril_status(data_network_state)=0;
	ril_status(hardware_available)=1;
	ril_status(screen_state)=1;//assume screen is on
	ril_status(hardware_available)=1;
	ril_status(radio_state)=RADIO_STATE_OFF;

	//to trigger android radio state changed
	sState = RADIO_STATE_UNAVAILABLE;

    setRadioState (RADIO_STATE_OFF);

    ret = at_handshake();if(ret<0)	goto failed;
	ret = modem_init();if(ret<0)	goto failed;

	//start rilhw 
	rilhw_init();

	return 0;

failed:
	setRadioState(RADIO_STATE_UNAVAILABLE);
	//power off ril hardware now
	//Ellie comments out, just return error and we'll handle it in queueRunner
	//rilhw_power(rilhw,kRequestStateReset);
	return ret;
	

}

static void waitForClose()
{
    pthread_mutex_lock(&s_state_mutex);

    while (s_closed == 0) {
        pthread_cond_wait(&s_state_cond, &s_state_mutex);
    }

    pthread_mutex_unlock(&s_state_mutex);
}

/**
 * Called by atchannel when an unsolicited line appears
 * This is called on atchannel's reader thread. AT commands may
 * not be issued here
 */
static void onUnsolicited (const char *s, const char *sms_pdu)
{
    char *line = NULL;
    int err;

    /* Ignore unsolicited responses until we're initialized.
     * This is OK because the RIL library will poll for initial state
     */
    if (sState == RADIO_STATE_UNAVAILABLE) {
		DBG("onUnsolicited radio not available now\n");
        return;
    }

    if (strStartsWith(s, "%CTZV:")) {
        char *response;

        line = strdup(s);
        at_tok_start(&line);

        err = at_tok_nextstr(&line, &response);

        if (err != 0) {
            ERROR("invalid NITZ line %s\n", s);
        } else {
            RIL_onUnsolicitedResponse (
                RIL_UNSOL_NITZ_TIME_RECEIVED,
                response, strlen(response));
        }
         free(line);
    }
	else if(strStartsWith(s, "^SIMST:"))
    {
       RIL_onUnsolicitedResponse (
            RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED,
            NULL, 0);     
    
    }
	else if(strStartsWith(s,"+CLIP:"))
	{
		;//TODO
	}
	else if (strStartsWith(s,"+CRING:")
                || strStartsWith(s,"RING")
				|| strStartsWith(s,"HANGUP:")
                || strStartsWith(s,"+CCWA")
				|| strStartsWith(s,"VOICE")                
				|| strStartsWith(s,"ANSWER")                
                || strStartsWith(s,"^ORIG")
                || strStartsWith(s,"^CONN")
                || strStartsWith(s,"^CEND")
				|| strStartsWith(s,"CONNECT")                
				|| strStartsWith(s,"+ZCORG")
				|| strStartsWith(s,"+ZCCNT")
				|| strStartsWith(s,"+ZCEND")) 
    {
		DBG("call state changed\n");
        RIL_onUnsolicitedResponse (
            RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
            NULL, 0);
    } 
	else if (strStartsWith(s,"+CREG:")
                || strStartsWith(s,"+CGREG:")
                || strStartsWith(s,"^MODE:")
		|| strStartsWith(s,"^NDISSTAT:") ) 
    {
		enqueueRILEvent(onDataCallListChanged, NULL, NULL);
		RIL_onUnsolicitedResponse ( //@@
		RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED,
		NULL, 0);		
    }
	else if (strStartsWith(s, "+CMT:")) 
	{
        RIL_onUnsolicitedResponse (
            RIL_UNSOL_RESPONSE_NEW_SMS,
            sms_pdu, strlen(sms_pdu));
    } 
	else if (strStartsWith(s, "+CDS:")) 
	{
        RIL_onUnsolicitedResponse (
            RIL_UNSOL_RESPONSE_NEW_SMS_STATUS_REPORT,
            sms_pdu, strlen(sms_pdu));
    } 
	else if (strStartsWith(s, "^SMMEMFULL:")) 
	{
        RIL_onUnsolicitedResponse (
            RIL_UNSOL_CDMA_RUIM_SMS_STORAGE_FULL,
            NULL, 0);
	
	}
	else if (strStartsWith(s, "+CMTI:")) 
	{
		const struct timespec TIMEVAL_CMGR = { 1, 0 };
		
		switch(rilhw->model)
		{
			case kRIL_HW_MC2716:
			case kRIL_HW_MC8630:
			{
				char* mem;
				int index;
				line = strdup(s);
				at_tok_start(&line);

				err = at_tok_nextstr(&line, &mem);
				if(!err)
				{
					err = at_tok_nextint(&line,&index);
					if(!err)
					{
						PST_SMS_INDICATION smi=malloc(sizeof(ST_SMS_INDICATION));
						if(smi){
							if(!strcmp(mem,"BM"))
								smi->sms_mem = 0;
							else if(!strcmp(mem,"ME"))
								smi->sms_mem = 1;
							else if(!strcmp(mem,"MT"))
								smi->sms_mem = 2;
							else if(!strcmp(mem,"SM"))
								smi->sms_mem = 3;
							else if(!strcmp(mem,"TA"))
								smi->sms_mem = 4;
							else if(!strcmp(mem,"SR"))
								smi->sms_mem = 5;
							
							smi->sms_index = index;
							DBG("sms mem=%s,%d\n",mem,index);
							
							enqueueRILEvent(on_new_cdma_sms, smi, &TIMEVAL_CMGR);
						}
					}
				}

				free(line);
			}
			break;
			default://GSM based hardware
			{
				/* can't issue AT commands here -- call on main thread */  
				int location;     
				char *response = NULL; 
				char *tmp;        
				line = strdup(s);        
				tmp = line;        
				at_tok_start(&tmp);
				err = at_tok_nextstr(&tmp, &response);    
				if (err < 0) 
				{            
					ERROR("sms request fail");   					       
				}	
				else if (!strncmp(response, "SM",2)) 
				{
					/* Read the memory location of the sms */  
					err = at_tok_nextint(&tmp, &location);    
					if (err < 0) 
					{            
						ERROR("error parse location");   
					}       
					else
					{
						enqueueRILEvent(on_new_gsm_sms, (void *)location, &TIMEVAL_CMGR);
					}
				}
				free(line);
			}
			break;
		}
        
		
        
		
	}		
	else if (strStartsWith(s, "^HCMGR:")||strStartsWith(s, "^HCMT:")) // cdma directly report
	{
        int ret;
		#if CDMA_MASQUERADING
        char* pdu;
        ret = encode_gsm_sms_pdu(s, sms_pdu, &pdu);
        if (ret == 0)
        {
            /* report cdma message in gsm pdu format */
            RIL_onUnsolicitedResponse (
                RIL_UNSOL_RESPONSE_NEW_SMS,
                pdu, strlen(pdu));
        }
		#else		
		RIL_CDMA_SMS_Message sms;
		ret = encode_cdma_sms(s,sms_pdu,&sms);
		if(!ret){
            RIL_onUnsolicitedResponse (
                RIL_UNSOL_RESPONSE_CDMA_NEW_SMS,
                &sms, sizeof(RIL_CDMA_SMS_Message));
			
		}
		#endif
	}
	else if (strStartsWith(s, "+CMGR:")) //read return
	{            
        int ret;
        char* pdu;
        ret = encode_gsm_mgr_pdu(s, (char*)sms_pdu, &pdu);
        if (ret == 0)
        {
            /* report cdma message in gsm pdu format */
            RIL_onUnsolicitedResponse (
                RIL_UNSOL_RESPONSE_NEW_SMS,
                pdu, strlen(pdu));
        }
	
	}
	else if (strStartsWith(s, "+CMGL:"))
	{            
        int ret;
        char* pdu;
        ret = encode_gsm_mgl_pdu(s, (char*)sms_pdu, &pdu);
        if (ret == 0)
        {
            /* report cdma message in gsm pdu format */
            RIL_onUnsolicitedResponse (
                RIL_UNSOL_RESPONSE_NEW_SMS,
                pdu, strlen(pdu));
        }
	
	}
	
	
	else if (strStartsWith(s, "^HCMGSS:")) 
	{
		DBG("CDMA sms send successfully\n");
	}	
	else if (strStartsWith(s, "^HCMGSF:")) 
	{
		DBG("CDMA sms send failed\n");
	}		
	else if(strStartsWith(s, "^HCMT:"))
	{
		DBG("new CDMA sms report");
	}
	else if (strStartsWith(s, "+CGEV:")) 
	{
		enqueueRILEvent(onDataCallListChanged, NULL, NULL);
    }else if (strStartsWith(s, "^RESUME:")) 
	{
        ;
    }
	/*
	else if ( strStartsWith(s, "^HRSSILVL:"))
	{
        int dbm=0;
        RIL_SignalStrength response={{0,0},{0,0},{0,0,0}};
         line = strdup(s);
        at_tok_start(&line);
        err = at_tok_nextint(&line, &dbm);

        if (err != 0) {
            ERROR("invalid HRSSILVL  line %s\n",s);
        } else {
		    response.EVDO_SignalStrength.dbm = rssi2dbm(dbm,1);
			response.EVDO_SignalStrength.ecio = 0;//ecio;
				 response.EVDO_SignalStrength.signalNoiseRatio =8;
           	 RIL_onUnsolicitedResponse (
                	RIL_UNSOL_SIGNAL_STRENGTH,
               		& response, sizeof(response));
       	 }
        free(line);
    }
    */
    else if(strStartsWith(s,"^RSSI:")){	
	int rssi=0;
        RIL_SignalStrength response;
	memset(&response,99,sizeof(RIL_SignalStrength));
        char* tmp =line = strdup(s);
        at_tok_start(&line);
        err = at_tok_nextint(&line, &rssi);

        if (err != 0) {
            ERROR("invalid ^RSSI  line %s\n",s);
        } else {
 	  response.GW_SignalStrength.signalStrength = rssi;
	  response.LTE_SignalStrength.signalStrength = rssi;
	  if(99==rssi)
	  	response.LTE_SignalStrength.rsrp=0x7FFFFFFF;
	  else
		response.LTE_SignalStrength.rsrp=113-2*rssi;
	  response.LTE_SignalStrength.rsrq=0x7FFFFFFF;
	  response.LTE_SignalStrength.rssnr=0x7FFFFFFF;
	  response.LTE_SignalStrength.cqi=0x7FFFFFFF;	
           	 RIL_onUnsolicitedResponse (
                	RIL_UNSOL_SIGNAL_STRENGTH,
               		& response, sizeof(response));
       	 }
        free(tmp);
   }else if(strStartsWith(s,"+CGAL:")){
	   	int groups[320];
		int total;
		int index=1;
		char* tmp =line = strdup(s);

        at_tok_start(&line);
        at_tok_nextint(&line, &groups[0]);
		total = groups[0];
		while((total-->0)&&at_tok_hasmore(&line)){
			 at_tok_nextint(&line, &groups[index++]);
		}
		free(tmp);
		
		RIL_onUnsolicitedResponse (
				   RIL_UNSOL_PTT_CURRENT_GROUP_ACTIVE_LIST,
				   groups, sizeof(int)*(groups[0]+1));
		
   }else if(strStartsWith(s,"+CAPTTG:")){
	int inst,gid,pttgrant,actioncause,ownerind;
	char* tmp =line = strdup(s);
  	ownerind=ePttGroupOwnerIndNonOrignator;
        at_tok_start(&line);
        at_tok_nextint(&line, &inst);
        at_tok_nextint(&line, &gid);
        at_tok_nextint(&line, &pttgrant);
        at_tok_nextint(&line, &actioncause);
	if(at_tok_hasmore(&line)){
	  at_tok_nextint(&line, &ownerind);
	}
        free(tmp);
	DBG("PTTCall Grant:inst:%d,gid:%d,%s,ac:%d,owner:%s",inst,gid,(ePttCallGranted==pttgrant)?"granted":
		(ePttCallDenied==pttgrant)?"denied":"queued",actioncause,
		(ePttGroupOwnerIndOrignator==ownerind)?"Originator":"NonOriginator");
			{
				int reponses[5];
				reponses[0] = inst;
				reponses[1] = gid;
				reponses[2] = pttgrant;
				reponses[3] = actioncause;
				reponses[4] = ownerind;
				RIL_onUnsolicitedResponse (
                	RIL_UNSOL_PTT_CALL_INDICATOR,
               		reponses, sizeof(int)*5);
			}
		
   }else if(strStartsWith(s,"+CSIND:")){
   	int pttgrant;
	char* speakerno,*speakername;
	char* tmp =line = strdup(s);
	speakerno=speakername=NULL;
        at_tok_start(&line);
        at_tok_nextint(&line, &pttgrant);
	if(at_tok_hasmore(&line)){
	  at_tok_nextstr(&line, &speakerno);
	}	
	if(at_tok_hasmore(&line)){
	  at_tok_nextstr(&line, &speakername);
	}

	DBG("PTTCall Speak Granted:%s %s %s",(ePttCallGranted==pttgrant)?"Yes":"No",
		speakerno?speakerno:"",
		speakername?speakername:"");
	{
		char* reponses[3]={0};
		int num=1;
		asprintf(&reponses[0],"%d",pttgrant);
		if(speakerno){
			asprintf(&reponses[1],speakerno);
			num++;
		}
		if(speakername){
			asprintf(&reponses[2],speakername);
			num++;
		}
		RIL_onUnsolicitedResponse(RIL_UNSOL_PTT_NOTIFICATION_DIAL,reponses,sizeof(char*)*num);
		for(num=0;num<3;num++)
			if(reponses[num]) free(reponses[num]);
	}
        free(tmp);
   }else if(strStartsWith(s,"+CTICN:")){
   	int inst,callstatus,aiservice,simplex,callpartyid,demandind,priority,pttambientlsn,ptttempgrp;
	char* tmp =line = strdup(s);
        at_tok_start(&line);
        at_tok_nextint(&line, &inst);
        at_tok_nextint(&line, &callstatus);
        at_tok_nextint(&line, &aiservice);
        at_tok_nextint(&line, &simplex);
        at_tok_nextint(&line, &callpartyid);
        at_tok_nextint(&line, &demandind);
        at_tok_nextint(&line, &priority);
        at_tok_nextint(&line, &pttambientlsn);
        at_tok_nextint(&line, &ptttempgrp);
        free(tmp);
	DBG("PTTCall incoming call:inst:%d callstatus:%d,ai:%d,simplex:%d,callid:%d,demandind:%d,priority:%d,amblsn:%d,tempgrp:%d",
		inst,callstatus,aiservice,simplex,callpartyid,demandind,priority,
		pttambientlsn,ptttempgrp);
       pttcall_call_info_indicate(ePttCallActive,inst,ePttCallStatusIncoming,aiservice,callpartyid,1);
       RIL_onUnsolicitedResponse (
            RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
            NULL, 0);
	   {
	   	
			int reponses[9];
			reponses[0] = inst;
			reponses[1] = callstatus;
			reponses[2] = aiservice;
			reponses[3] = simplex;
			reponses[4] = callpartyid;
			reponses[5] = demandind;
			reponses[6] = priority;
			reponses[7] = pttambientlsn;
			reponses[8] = ptttempgrp;
				
		RIL_onUnsolicitedResponse (
	            RIL_UNSOL_PTT_NOTIFICATION_CALL,
	            reponses, sizeof(int)*9);
       }
   }else if(strStartsWith(s,"+CTCC:")){
   	int inst,commtype;
	char* tmp =line = strdup(s);
        at_tok_start(&line);
        at_tok_nextint(&line, &inst);
        at_tok_nextint(&line, &commtype);
        free(tmp);
	DBG("PTTCall Connect:inst:%d comm:%d",inst,commtype);
		{
				int reponses[2];
				reponses[0] = inst;
				reponses[1] = commtype;
				RIL_onUnsolicitedResponse (
                	RIL_UNSOL_PTT_CALL_CONNECT,
               		reponses, sizeof(int)*2);
		}
   }else if(strStartsWith(s,"+CTGR:")){
   	int inst,gid,pttcause;
	char* tmp =line = strdup(s);
        at_tok_start(&line);
        at_tok_nextint(&line, &inst);
        at_tok_nextint(&line, &gid);
        at_tok_nextint(&line, &pttcause);

        free(tmp);
	DBG("Group Release:inst:%d gid:%d ac:%d",inst,gid,pttcause);

		{
			int reponses[3];
				reponses[0] = inst;
				reponses[1] = gid;
				reponses[2] = pttcause;
		RIL_onUnsolicitedResponse (
	            RIL_UNSOL_PTT_GROUP_RELEASE,
	            reponses, sizeof(int)*3);
		}

   }else if(strStartsWith(s,"+CTCR:")){
   	int inst,gid,actioncause;
	char* tmp =line = strdup(s);
        at_tok_start(&line);
        at_tok_nextint(&line, &inst);
        at_tok_nextint(&line, &gid);
        at_tok_nextint(&line, &actioncause);

        free(tmp);
	DBG("PTTCall Release:inst:%d gid:%d ac:%d",inst,gid,actioncause);
	pttcall_call_info_indicate(ePttCallInactive,0,0,0,0,0);
	RIL_onUnsolicitedResponse (
            RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
            NULL, 0);
		{
			int reponses[3];
				reponses[0] = inst;
				reponses[1] = gid;
				reponses[2] = actioncause;
		RIL_onUnsolicitedResponse (
	            RIL_UNSOL_PTT_CALL_HANGUP,
	            reponses, sizeof(int)*3);
		}

   }else if(strStartsWith(s,"^CPTTINFO:")){
 	static const char* pttstate_names[]={
          "unregistered",
           "group not joined",
           "group idle","group monitor","group master idle","group master monitor","group paused?","p2p call MO","p2p call MT","MO paused?","MT paused?" 
	};
   	int pttstate;
	char* tmp = line = strdup(s);
        at_tok_start(&line);
        at_tok_nextint(&line, &pttstate);        
	free(tmp);
	DBG("PTTCall State:%s",(pttstate<11)?pttstate_names[pttstate]:"unknown");
   }else if(strStartsWith(s,"+CTBI:")){
	   int ctbi;
	   char* tmp = line = strdup(s);
		   at_tok_start(&line);
		   at_tok_nextint(&line, &ctbi);		
	   free(tmp);
	   {
			int reponses[3];
				reponses[0] = ctbi;
		RIL_onUnsolicitedResponse (
	            RIL_UNSOL_PTT_BLOCKED_INDICATOR,
	            reponses, sizeof(int)*1);
		}
   	
   }else if(strStartsWith(s,"+CGIU:")){
	   int ctbi;
	   PttGroups pgs;
	   char* tmp = line = strdup(s);
	   parse_cgiu(line,&pgs);
	   RIL_onUnsolicitedResponse(
			   RIL_UNSOL_PTT_AVAILABLE_GROUP_CHANGED,
			   &pgs, sizeof(pgs));
	   free_ptt_groups(&pgs);
	   free(tmp);   	
   }else if(strStartsWith(s,"+CTOCP:")){
 	static const char* callstate_names[]={
	  "call progressing",//0
	  "call queued",//1
	  "call party paged",//2
 	  "call continue",//3
	  "hang time expired",	  //4
	};
   	int inst,callstatus,aiservice,simplex,encryption;
	char* tmp = line = strdup(s);
        at_tok_start(&line);
        at_tok_nextint(&line, &inst);
        at_tok_nextint(&line, &callstatus);
        at_tok_nextint(&line, &aiservice);
	simplex = 0;
	if(at_tok_hasmore(&line)){
	  at_tok_nextint(&line, &simplex);
	}	
	encryption=0;
	if(at_tok_hasmore(&line)){
	  at_tok_nextint(&line, &encryption);
	}

	free(tmp);
	DBG("PTT P2PCall inst:%d,status:%s,aiservice:%d,%s %s",inst,
		(callstatus<11)?callstate_names[callstatus]:"unknown",
		aiservice,simplex?"Simplex":"Duplex",encryption?"Encryption":"");
	pttcall_call_info_indicate(ePttCallActive,inst,callstatus,aiservice,0,0);
	RIL_onUnsolicitedResponse (
            RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
            NULL, 0);
		{
			int reponses[5];
				reponses[0] = inst;
				reponses[1] = callstatus;
				reponses[2] = aiservice;				
				reponses[1] = simplex;
				reponses[2] = encryption;
		RIL_onUnsolicitedResponse (
	            RIL_UNSOL_PTT_OUTGOING_CALL_PROGRESS,
	            reponses, sizeof(int)*5);
		}
       
   }else if(strStartsWith(s,"^DSDORMANT"))
    {
		int dormanted;
        line = strdup(s);
        at_tok_start(&line);

        err = at_tok_nextint(&line, &dormanted);

        if (!err ) {

			DBG("MT enter %s state\n",dormanted?"dormanted":"normal");				
        }
		
         free(line);
    }else if(strStartsWith(s,"+CCED:"))  {
		int band,channel,sid,nid,bsprev,pilot_pn_offset,bsid;
		int notused;
        char* tmp = line = strdup(s);
        at_tok_start(&line);

        at_tok_nextint(&line, &band);
		at_tok_nextint(&line, &channel);
		at_tok_nextint(&line, &sid);
		at_tok_nextint(&line, &nid);
		at_tok_nextint(&line, &bsprev);
		at_tok_nextint(&line, &pilot_pn_offset);
		at_tok_nextint(&line, &bsid);
		if(bsid!=cdma_context.bsid||
			cdma_context.sid != sid||
			cdma_context.nid != nid||
			cdma_context.pn != pilot_pn_offset){
			cdma_context.bsid = bsid;
			cdma_context.sid = sid;
			cdma_context.nid = nid;
			cdma_context.pn = pilot_pn_offset;
			cdma_context.valid=1;
			// RIL_onUnsolicitedResponse ( //@@
			// RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED,
			 //NULL, 0);		 
		}
		free(tmp);		
    }else{
    	DBG("unhandled unsolicited message [%s]\n",s);
    }
	
	
}

static void signalCloseQueues(char close_flag)
{
    unsigned int i;
    for (i = 0; i < (sizeof(s_requestQueues) / sizeof(RequestQueue *)); i++) {
        int err;
        RequestQueue *q = s_requestQueues[i];
        if ((err = pthread_mutex_lock(&q->queueMutex)) != 0)
            ERROR("%s() failed to take queue mutex: %s",
                __func__, strerror(err));

        q->closed = close_flag;
        if ((err = pthread_cond_signal(&q->cond)) != 0)
            ERROR("%s() failed to broadcast queue update: %s",
                __func__, strerror(err));

        if ((err = pthread_mutex_unlock(&q->queueMutex)) != 0)
            ERROR("%s() failed to take queue mutex: %s", __func__,
                 strerror(err));
    }
}

/* Called on command or reader thread */
static void onATReaderClosed()
{
    WARN("AT channel closed\n");

    setRadioState (RADIO_STATE_UNAVAILABLE);
    signalCloseQueues(1);
    at_close();
}

/* Callback from AT Channel. Called on command thread. */
static void onATTimeout()
{
    WARN("AT channel timeout; restarting..");
    /* Last resort, throw escape on the line, close the channel
       and hope for the best. */
    at_send_escape();

    setRadioState(RADIO_STATE_UNAVAILABLE);

    signalCloseQueues(2);
}
static void onATAccessNotify(const char* at,int wake)
{
	//wakeup modem before access
	rilhw_wakeup(rilhw,wake);
}

static void usage(char *s)
{
#ifdef RIL_SHLIB
    fprintf(stderr, "requires: -p <tcp port> or [-d /dev/tty_device] [-u /dev/tty_device] [-f flag]\n");
	fprintf(stderr, "[-d /dev/tty_device] specifiy service port \n");
	fprintf(stderr, "[-u /dev/tty_device] specifiy modem port \n");
#else
    fprintf(stderr, "usage: %s [-p <tcp port>] [-d /dev/tty_device] [-u /dev/tty_device] [-f flag]\n", s);
	fprintf(stderr, "[-d /dev/tty_device] specifiy service port \n");
	fprintf(stderr, "[-u /dev/tty_device] specifiy modem port \n");

    exit(-1);
#endif
}

struct queueArgs {
    int port;
    char * loophost;
    const char *device_path;
};

static int safe_read(int fd, char *buf, int count)
{
    int n;
    int i = 0;

    while (i < count) {
        n = read(fd, buf + i, count - i);
        if (n > 0)
            i += n;
        else if (!(n < 0 && errno == EINTR))
            return -1;
    }

    return count;
}

static void *queueRunner(void *param)
{
    int fd = -1;
    int ret = 0;
    int n;
    struct timespec timeout;
    struct queueArgs *queueArgs = (struct queueArgs *) param;
    struct RequestQueue *q = NULL;
	int failcount=0,fatalerr=0;

    DBG("%s() starting!", __func__);

    for (;;) {
runer_loop:
		if(rilhw_power_state()==kRequestStateOff)
			fatalerr=1;
		if(failcount>0||fatalerr)
		{
			pdp_uninit();	
		}
#ifdef MODEM_POWER_PATCH
		if(failcount>TIMEOUT_MODEM_POWER_PATCH||fatalerr)
		{
			rilhw_power(0,kRequestStateReset);
			failcount=fatalerr=0;
			DBG("modem power once again");
			sleep(TIMEOUT_SEARCH_FOR_TTY);
		}
#endif
		if(failcount>0)
		{
			WARN("error encountered,retry after %ds\n",TIMEOUT_SEARCH_FOR_TTY);		
			sleep(TIMEOUT_SEARCH_FOR_TTY);
		}

        fd = -1;      		
		rilhw_found(&rilhw);		
		if(rilhw||queueArgs->port > 0)
		{		
			DBG("new ril hardware %s found\n",rilhw->model_name);
			s_service_port = rilhw->service_port;
			s_modem_port = rilhw->modem_port;
			DBG("RIL Prepare init ,ServicePort[%s],ModemPort[%s]",s_service_port,s_modem_port);	
			
			pdp_init();
			#if (CDMA_MASQUERADING==0)
			if(rilhw->prefer_net == kPREFER_NETWORK_TYPE_CDMA_EVDV){
				radio_state_not_ready = RADIO_STATE_RUIM_NOT_READY;
				radio_state_ready = RADIO_STATE_RUIM_READY;
				radio_state_locked_or_absent = RADIO_STATE_RUIM_LOCKED_OR_ABSENT;
				ril_apptype = RIL_APPTYPE_RUIM;
				ril_persosubstate_network = RIL_PERSOSUBSTATE_RUIM_NETWORK1;
			}
			else 
			#endif
			{
				radio_state_not_ready = RADIO_STATE_SIM_NOT_READY;
				radio_state_ready = RADIO_STATE_SIM_READY;
				radio_state_locked_or_absent = RADIO_STATE_SIM_LOCKED_OR_ABSENT;
				ril_apptype = RIL_APPTYPE_SIM;
				ril_persosubstate_network = RIL_PERSOSUBSTATE_SIM_NETWORK;		
			}
        	while (fd < 0) {
//            if (queueArgs->port > 0) {
  //              if (queueArgs->loophost)
    //                fd = socket_network_client(queueArgs->loophost, queueArgs->port, SOCK_STREAM);
      //          else
        //            fd = socket_loopback_client(queueArgs->port, SOCK_STREAM);
          //  } else
            	if (s_service_port != NULL) {

                fd = open (s_service_port, O_RDWR);
                if ( fd >= 0 && !memcmp( s_service_port, "/dev/tty", 8 ) ) {

					/* disable echo on serial ports */
					struct termios  ios;
					tcgetattr( fd, &ios );
					
					 #if 0
					 ios.c_iflag = IGNPAR|IGNBRK;
					 ios.c_iflag &=~(ICRNL);
					 
					ios.c_lflag = 0;  /* disable ECHO, ICANON, etc... */
					 ios.c_oflag = 0;
					 #else											 
					 cfmakeraw(&ios);
					 cfsetospeed(&ios, B115200);
					 cfsetispeed(&ios, B115200);
					 ios.c_cflag |= CREAD | CLOCAL;
					 #endif
					 tcflush(fd, TCIOFLUSH);
					 tcsetattr( fd, TCSANOW, &ios );
                }
            }

			if (fd < 0) {				
				failcount++;
				goto runer_loop;		
				/* never returns */
			}
        }

        ret = at_open(fd, onUnsolicited);

        if (ret < 0) {
            ERROR("%s() AT error %d on at_open", __func__, ret);
            at_close();
            failcount++;
            continue;
        }

        at_set_on_reader_closed(onATReaderClosed);
        at_set_on_timeout(onATTimeout);
        at_set_access_notify(onATAccessNotify);

        q = &s_requestQueueDefault;

        if (initializeCommon()) {
            ERROR("%s() Failed to initialize common", __func__);
            at_close();
            failcount++;
            continue;
        }

	        failcount=0;
	        q->closed = 0;
	        at_make_default_channel();

	        ERROR("%s() Looping the requestQueue!", __func__);
	        for (;;) {
	            RILRequest *r;
	            RILEvent *e;
	            struct timespec ts;
	            int err;

	            memset(&ts, 0, sizeof(ts));

	            if ((err = pthread_mutex_lock(&q->queueMutex)) != 0)
	                ERROR("%s() failed to take queue mutex: %s!",
	                __func__, strerror(err));

	            if (q->closed != 0) {
	                WARN("%s() AT Channel error, attempting to recover..", __func__);
	                if ((err = pthread_mutex_unlock(&q->queueMutex)) != 0)
	                    ERROR("Failed to release queue mutex: %s!", strerror(err));
	                if(q->closed==2)
	                    fatalerr=1;
	                else
                        failcount++;
	                break; /* Catch the closed bit at the top of the loop. */
	            }

	            while (q->closed == 0 && q->requestList == NULL &&
	                q->eventList == NULL) {
	                if ((err = pthread_cond_wait(&q->cond, &q->queueMutex)) != 0)
	                    ERROR("%s() failed broadcast queue cond: %s!",
	                        __func__, strerror(err));
	            }

	            /* eventList is prioritized, smallest abstime first. */
	            if (q->closed == 0 && q->requestList == NULL && q->eventList) {
	                int err = 0;
	                err = pthread_cond_timedwait(&q->cond, &q->queueMutex, &q->eventList->abstime);
	                if (err && err != ETIMEDOUT)
	                    ERROR("%s() timedwait returned unexpected error: %s",
	                __func__, strerror(err));
	            }

	            if (q->closed != 0) {
	                if ((err = pthread_mutex_unlock(&q->queueMutex)) != 0)
	                    ERROR("%s(): Failed to release queue mutex: %s!",
	                        __func__, strerror(err));
	                continue; /* Catch the closed bit at the top of the loop. */
	            }

	            e = NULL;
	            r = NULL;

	            clock_gettime(CLOCK_MONOTONIC, &ts);

	            if (q->eventList != NULL &&
	                timespec_cmp(q->eventList->abstime, ts, < )) {
	                e = q->eventList;
	                q->eventList = e->next;
	            }

	            if (q->requestList != NULL) {
	                r = q->requestList;
	                q->requestList = r->next;
	            }

	            if ((err = pthread_mutex_unlock(&q->queueMutex)) != 0)
	                ERROR("%s(): Failed to release queue mutex: %s!",
	                    __func__, strerror(err));
		    
	            if (e) {
	                e->eventCallback(e->param);
	                free(e);
	            }

	            if (r) {
	                processRequest(r->request, r->data, r->datalen, r->token);
	                freeRequestData(r->request, r->data, r->datalen);
	                free(r);
	            }
	        }

        	at_close();
        	ERROR("%s() Re-opening after close", __func__);
    	}else {
			failcount++;
		}

	}
	
    return NULL;
}

void ril_dump_array(char* prefix,const char* a,int length)
{
	int i;
	char* ptr;
    char* ptr2;
	char digits[2048] = {0};
    char c[512] = {'0'};
	
	ptr = &digits[0];
    ptr2 = &c[0];
	for(i=0;i<length;i++)
	{
		ptr+=sprintf(ptr,"%02x,",a[i]);
        ptr2+=sprintf(ptr2, "%c", a[i]);
	}
	DBG("%s array = [%s]",prefix,digits);
    DBG("%s char = [%s]", prefix, c);
}

static pthread_t s_tid_queueRunner;
/*** Static Variables ***/
static const RIL_RadioFunctions s_callbacks = {
    RIL_VERSION,
    onRequest,
    onStateRequest,
    onSupports,
    onCancel,
    getVersion
};



#ifdef RIL_SHLIB

const RIL_RadioFunctions *RIL_Init(const struct RIL_Env *env, int argc, char **argv)
{
    int ret;
    int fd = -1;
    int opt;
    pthread_attr_t attr;
    struct queueArgs *queueArgs;
	char *loophost = NULL;

    s_rilenv = env;

    while ( -1 != (opt = getopt(argc, argv, "z:p:d:s:u:f:"))) {
        switch (opt) {			
            case 'z':
                loophost = optarg;
                DBG("%s() Using loopback host %s..", __func__, loophost);
                break;
            case 'p':
                s_port = atoi(optarg);
                if (s_port == 0) {
                    usage(argv[0]);
                    return NULL;
                }
                DBG("Opening loopback port %d\n", s_port);
            break;

            case 'd':
                s_service_port = optarg;
                DBG("Opening tty device %s\n", s_service_port);
            break;

            case 's':
                s_service_port   = optarg;
                s_device_socket = 1;
                DBG("Opening socket %s\n", s_service_port);
            break;

			case 'u':
				s_modem_port = optarg;
				DBG("Opening modem device %s\n",s_modem_port);
            break;

			case 'f':
				s_flags = atoi(optarg);
				//always turn on error
				s_flags |= RIL_FLAG_ERROR;
				DBG("Debug flag change to x%08x\n",s_flags);
            break;
            
            default:
                usage(argv[0]);
                return NULL;
        }
    }

	if(!s_service_port)
	{
		s_service_port = s_def_service_port;
		WARN("use default AT port [%s]\n",s_service_port);
	}
	if(!s_modem_port)
	{
		s_modem_port = s_def_modem_port;
		WARN("use default MODEM port [%s]\n",s_modem_port);
	}
    

    
    if (s_port < 0 && s_service_port == NULL) {
        usage(argv[0]);
        return NULL;
    }

	
    queueArgs = (struct queueArgs*) malloc(sizeof(struct queueArgs));
    memset(queueArgs, 0, sizeof(struct queueArgs));

    queueArgs->device_path = s_service_port;
    queueArgs->port = s_port;
    queueArgs->loophost = loophost;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    pthread_create(&s_tid_queueRunner, &attr, queueRunner, queueArgs);

    return &s_callbacks;
}

#else
int main (int argc, char **argv)
{
    int opt;
    int port = -1;
    struct queueArgs *queueArgs;
	char *loophost = NULL;


    DBG("%s() entering...", __func__);

    while ( -1 != (opt = getopt(argc, argv, "z:p:d:s:u:f:"))) {
        switch (opt) {
            case 'z':
                loophost = optarg;
                DBG("%s() Using loopback host %s..", __func__, loophost);
                break;			
            case 'p':
                s_port = atoi(optarg);
                if (s_port == 0) {
                    usage(argv[0]);
                    return NULL;
                }
                DBG("Opening loopback port %d\n", s_port);
            break;

            case 'd':
                s_service_port = optarg;
                DBG("Opening tty device %s\n", s_service_port);
            break;

            case 's':
                s_service_port   = optarg;
                s_device_socket = 1;
                DBG("Opening socket %s\n", s_service_port);
            break;

			case 'u':
				s_modem_port = optarg;
				DBG("Opening modem device %s\n",s_modem_port);
            break;

			case 'f':
				s_flags = atoi(optarg);
				//always turn on error
				s_flags |= RIL_FLAG_ERROR;
				DBG("Debug flag change to x%08x\n",s_flags);
            break;
            
            default:
                usage(argv[0]);
                return NULL;
        }
    }

	if(!s_service_port)
	{
		s_service_port = s_def_service_port;
		WARN("use default AT port [%s]\n",s_service_port);
	}
	if(!s_modem_port)
	{
		s_modem_port = s_def_modem_port;
		WARN("use default MODEM port [%s]\n",s_modem_port);
	}
    
    if (s_port < 0 && s_service_port == NULL) {
        usage(argv[0]);
        return NULL;
    }


    queueArgs = (struct queueArgs*) malloc(sizeof(struct queueArgs));
    memset(queueArgs, 0, sizeof(struct queueArgs));

    queueArgs->device_path = s_service_port;
    queueArgs->port = s_port;
    queueArgs->loophost = loophost;

    RIL_register(&s_callbacks);

    queueRunner(queueArgs);

    return 0;
}

#endif

