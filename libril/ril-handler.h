#ifndef _RIL_HANDLER_H_
#define _RIL_HANDLER_H_

#include "at_tok.h"
#include "atchannel.h"
#include "ril-core.h"

//
//call handling
//
void requestHangupWaitingOrBackground(void *data, size_t datalen,
                                      RIL_Token t);
void requestHangupForegroundResumeBackground(void *data, size_t datalen,
                                             RIL_Token t);
void requestSwitchWaitingOrHoldingAndActive(void *data, size_t datalen,
                                            RIL_Token t);
void requestConference(void *data, size_t datalen, RIL_Token t);
void requestSeparateConnection(void *data, size_t datalen, RIL_Token t);
void requestExplicitCallTransfer(void *data, size_t datalen, RIL_Token t);
void requestUDUB(void *data, size_t datalen, RIL_Token t);
void requestSetMute(void *data, size_t datalen, RIL_Token t);
void requestGetMute(void *data, size_t datalen, RIL_Token t);
void requestLastCallFailCause(void *data, size_t datalen, RIL_Token t);
void requestGetCurrentCalls(void *data, size_t datalen, RIL_Token t);
void requestDial(void *data, size_t datalen, RIL_Token t);
void requestAnswer(void *data, size_t datalen, RIL_Token t);
void requestHangup(void *data, size_t datalen, RIL_Token t);
void requestDTMF(void *data, size_t datalen, RIL_Token t);
void requestDTMFStart(void *data, size_t datalen, RIL_Token t);
void requestDTMFStop(void *data, size_t datalen, RIL_Token t);
void requestCDMADTMF(void *data, size_t datalen, RIL_Token t);

//
//SMS
//
void requestSendSMS(void *data, size_t datalen, RIL_Token t);
void requestSendSMSExpectMore(void *data, size_t datalen, RIL_Token t);
void requestSMSAcknowledge(void *data, size_t datalen, RIL_Token t);
void requestWriteSmsToSim(void *data, size_t datalen, RIL_Token t);
void requestDeleteSmsOnSim(void *data, size_t datalen, RIL_Token t);
void requestGetSMSCAddress(void *data, size_t datalen, RIL_Token t);
void requestSetSMSCAddress(void *data, size_t datalen, RIL_Token t);
void requestGSMGetBroadcastSMSConfig(void *data, size_t datalen, RIL_Token t);
void requestGSMSetBroadcastSMSConfig(void *data, size_t datalen, RIL_Token t);
void requestGSMSMSBroadcastActivation(void *data, size_t datalen, RIL_Token t);

void requestCDMASendSMS(void *data, size_t datalen, RIL_Token t);
void requestCDMASMSAcknowledge(void *data, size_t datalen, RIL_Token t);
void CheckSMSStorage(void);
void checkMessageStorageReady();

void sms_delete(int index);

//
//network
//
extern RIL_RadioState sState;
extern pthread_mutex_t s_state_mutex;
extern pthread_cond_t s_state_cond;

void setRadioState(RIL_RadioState newState);
int isRadioOn();

void requestSetNetworkSelectionAutomatic(void *data, size_t datalen,
                                         RIL_Token t);
void requestSetNetworkSelectionManual(void *data, size_t datalen,
                                      RIL_Token t);
void requestQueryAvailableNetworks(void *data, size_t datalen,
                                   RIL_Token t);
void requestSetPreferredNetworkType(void *data, size_t datalen,
                                    RIL_Token t);
void requestGetPreferredNetworkType(void *data, size_t datalen,
                                    RIL_Token t);
void requestQueryNetworkSelectionMode(void *data, size_t datalen,
                                      RIL_Token t);
void requestSignalStrength(void *data, size_t datalen, RIL_Token t);
void requestRegistrationState(void *data,size_t datalen, RIL_Token t);

void requestGPRSRegistrationState(void *data, size_t datalen, RIL_Token t);

void requestOperator(void *data, size_t datalen, RIL_Token t);
void requestRadioPower(void *data, size_t datalen, RIL_Token t);
void requestSetLocationUpdates(void *data, size_t datalen, RIL_Token t);
void requestNeighboringCellIds(void * data, size_t datalen, RIL_Token t); 
void requestCDMASubScription(void *data, size_t datalen, RIL_Token t);
void requestQueryFacilityLock(void *data, size_t datalen, RIL_Token t);
void requestSetFacilityLock(void *data, size_t datalen, RIL_Token t);
int rssi2dbm(int rssi,int evdo);


//
//Device
//

void requestGetIMSI(void *data, size_t datalen, RIL_Token t);
void requestGetIMEI(void *data, size_t datalen, RIL_Token t);
void requestGetIMEISV(void *data, size_t datalen, RIL_Token t);
void requestDeviceIdentity(void *data, size_t datalen, RIL_Token t);
void requestBasebandVersion(void *data, size_t datalen, RIL_Token t);

//
//PDP
//
int  pdp_init(void);
void pdp_uninit(void);

void requestOrSendPDPContextList(RIL_Token *t);
void onDataCallListChanged(void *param);
void requestDataCallList(void *data, size_t datalen, RIL_Token t);
void requestSetupDataCall(void *data, size_t datalen, RIL_Token t);
void requestDeactivateDataCall(void *data, size_t datalen, RIL_Token t);
void requestLastDataCallFailCause(void *data, size_t datalen, RIL_Token t);
void requestScreenState(void *data, size_t datalen, RIL_Token t);



void pdp_check(void);

//
//Service
//
void requestQueryClip(void *data, size_t datalen, RIL_Token t);
void requestCancelUSSD(void *data, size_t datalen, RIL_Token t);
void requestSendUSSD(void *data, size_t datalen, RIL_Token t);
void requestGetCLIR(void *data, size_t datalen, RIL_Token t);
void requestSetCLIR(void *data, size_t datalen, RIL_Token t);
void requestQueryCallForwardStatus(void *data, size_t datalen,
                                   RIL_Token t);
void requestSetCallForward(void *data, size_t datalen, RIL_Token t);
void requestQueryCallWaiting(void *data, size_t datalen, RIL_Token t);
void requestSetCallWaiting(void *data, size_t datalen, RIL_Token t);
void requestSetSuppSvcNotification(void *data, size_t datalen,
                                   RIL_Token t);

void onSuppServiceNotification(const char *s, int type);
void onUSSDReceived(const char *s);

//
//SIM
//
#if 0
/*
 * The following SIM_Status list consist of indexes to combine the result
 * string of 3GPP AT command “AT+CPIN?” (ST-Ericsson version) with RIL API
 * "RIL_AppStatus" structure. To fill this structure the SIM_Status value is
 * matched to an entry in the static app_status_array[] below.
 */
typedef enum {
    SIM_ABSENT = 0,                     /* SIM card is not inserted */
    SIM_NOT_READY = 1,                  /* SIM card is not ready */
    SIM_READY = 2,                      /* radiostate = RADIO_STATE_SIM_READY */
    SIM_PIN = 3,                        /* SIM PIN code lock */
    SIM_PUK = 4,                        /* SIM PUK code lock */
    SIM_NETWORK_PERSO = 5,              /* Network Personalization lock */
    SIM_PIN2 = 6,                       /* SIM PIN2 lock */
    SIM_PUK2 = 7,                       /* SIM PUK2 lock */
    SIM_NETWORK_SUBSET_PERSO = 8,       /* Network Subset Personalization */
    SIM_SERVICE_PROVIDER_PERSO = 9,     /* Service Provider Personalization */
    SIM_CORPORATE_PERSO = 10,           /* Corporate Personalization */
    SIM_SIM_PERSO = 11,                 /* SIM/USIM Personalization */
    SIM_STERICSSON_LOCK = 12,           /* ST-Ericsson Extended SIM */
    SIM_BLOCKED = 13,                   /* SIM card is blocked */
    SIM_PERM_BLOCKED = 14,              /* SIM card is permanently blocked */
    SIM_NETWORK_PERSO_PUK = 15,         /* Network Personalization PUK */
    SIM_NETWORK_SUBSET_PERSO_PUK = 16,  /* Network Subset Perso. PUK */
    SIM_SERVICE_PROVIDER_PERSO_PUK = 17,/* Service Provider Perso. PUK */
    SIM_CORPORATE_PERSO_PUK = 18,       /* Corporate Personalization PUK */
    SIM_SIM_PERSO_PUK = 19,             /* SIM Personalization PUK (unused) */
    SIM_PUK2_PERM_BLOCKED = 20          /* PUK2 is permanently blocked */
} SIM_Status;
#endif
typedef enum {
    SIM_ABSENT = 0,
    SIM_NOT_READY = 1,
    SIM_READY = 2, /* SIM_READY means the radio state is RADIO_STATE_SIM_READY */
    SIM_PIN = 3,
    SIM_PUK = 4,
    SIM_NETWORK_PERSONALIZATION = 5,
    SIM_NETWORK_PERSO = 5,
    RUIM_ABSENT = 6,
    RUIM_NOT_READY = 7,
    RUIM_READY = 8,
    RUIM_PIN = 9,
    RUIM_PUK = 10,
    RUIM_NETWORK_PERSONALIZATION = 11,
    SIM_PIN2 = 12, /* SIM PIN2 lock */
    SIM_PUK2 = 13, /* SIM PUK2 lock */
    SIM_NETWORK_SUBSET_PERSO = 14, /* Network Subset Personalization */
    SIM_SERVICE_PROVIDER_PERSO = 15, /* Service Provider Personalization */
    SIM_CORPORATE_PERSO = 16, /* Corporate Personalization */
    SIM_SIM_PERSO = 17, /* SIM/USIM Personalization */
    SIM_STERICSSON_LOCK = 18, /* ST-Ericsson Extended SIM */
    SIM_BLOCKED = 19, /* SIM card is blocked */
    SIM_PERM_BLOCKED = 20, /* SIM card is permanently blocked */
    SIM_NETWORK_PERSO_PUK = 21, /* Network Personalization PUK */
    SIM_NETWORK_SUBSET_PERSO_PUK = 22, /* Network Subset Perso. PUK */
    SIM_SERVICE_PROVIDER_PERSO_PUK = 23,/* Service Provider Perso. PUK */
    SIM_CORPORATE_PERSO_PUK = 24, /* Corporate Personalization PUK */
    SIM_SIM_PERSO_PUK = 25, /* SIM Personalization PUK (unused) */
    SIM_PUK2_PERM_BLOCKED = 26 /* PUK2 is permanently blocked */
} SIM_Status;

typedef enum {
    UICC_TYPE_UNKNOWN,
    UICC_TYPE_SIM,
    UICC_TYPE_USIM,
} UICC_Type;

/* Huawei E770W subsys_mode spec. */
typedef enum{
	SUB_SYSMODE_NO_SERVICE = 0,
	SUB_SYSMODE_GSM = 1,
	SUB_SYSMODE_GPRS = 2,
	SUB_SYSMODE_EDGE = 3,
	SUB_SYSMODE_WCDMA = 4,
	SUB_SYSMODE_HSDPA = 5,
	SUB_SYSMODE_HSUPA = 6,
	SUB_SYSMODE_HSUPA_HSDPA = 7,
	SUB_SYSMODE_INVALID = 8,
}SUB_SYSMODE;



void onSimStateChanged(const char *s);
void onSIMReady();

void requestGetSimStatus(void *data, size_t datalen, RIL_Token t);
void requestSIM_IO(void *data, size_t datalen, RIL_Token t);
void requestEnterSimPin(void *data, size_t datalen, RIL_Token t);
void requestChangeSimPin(void *data, size_t datalen, RIL_Token t);
void requestChangeSimPin2(void *data, size_t datalen, RIL_Token t);
void requestSetFacilityLock(void *data, size_t datalen, RIL_Token t);
void requestQueryFacilityLock(void *data, size_t datalen, RIL_Token t);

void pollSIMState(void *param);
SIM_Status getSIMStatus(void);
void setPreferredMessageStorage();
RIL_RadioState getRadioState(void);


//
//OEM hook
//
void requestOEMHookRaw(void *data, size_t datalen, RIL_Token t);
void requestOEMHookStrings(void *data, size_t datalen, RIL_Token t);

//
//STK
//
void requestReportSTKServiceIsRunning(void *data, size_t datalen, RIL_Token t);
void requestSTKGetProfile(void *data, size_t datalen, RIL_Token t);
void requestSTKSetProfile(void *data, size_t datalen, RIL_Token t);
void requestSTKSendEnvelopeCommand(void *data, size_t datalen, RIL_Token t);
void requestSTKSendTerminalResponse(void *data, size_t datalen, RIL_Token t);



#endif
