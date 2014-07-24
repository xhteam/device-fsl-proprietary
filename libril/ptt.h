#ifndef PTT_H
#define PTT_H

#define COMMON_ACTION_CAUSE \
  acOK=0, \
  acNetworkTimeout=31, \
  acNetworkError=1001, \
  acUnsignedNumberError=1002, \
  acUserAuthenticationFailed=1003, \
  acServiceOptionNoSupported=1004, \
  acRequestedServiceOptionNotSubscribed=1005,\
  acUnspecifiedError=1006,\
  acInvalidParameter=1007, \
  acNetworkDetach=1008, \
  acHighPriorityTaskInterrupt=1009,\
  acNASInnerError=1010, \
  acIMSIIsTemporarilyBlocked=1011,\
  acIMSIIsPermanentalyBlocked=1012,\
  acUserLowPriority=1013,\
  acNoPTTCapability=1014,

#define PTT_SPECIFIED_ACTION_CAUSE \
  acOperationSuccess=1100, \
  acGroupOwnerClose=1101, \
  acDispatcherClose=1102, \
  acTimeoutClose=1103,\
  acSpeakerTokenTimeout=1104,\
  acReleaseForHighPriorityUser=1105,\
  acQueueTimeout=1106,\
  acForceToCancelQueue=1107,\
  acRequestGroupNotSubscribed=1108,\
  acQueueBufferIsFull=1109,\
  acNotGroupOwner=1110,\
  acUserInterrupt=1111,\
  acSetFailure=1112,\
  acOutOfService=1113,\
  acNormalRelease=1114,

#define PTP_SPECIFIED_ACTION_CAUSE \
  acNormalCallClearing=1201, \
  acUserBusy=1202,\
  acNoUserResponding=1203,\
  acUserAlertingToUser=1204,\
  acCallRejected=1205,\
  acInvalidNumberFormat=1206,\
  acUserInHighPriorityTask=1207,\
  acCallerPermissionDenied=1208,\
  acCalleePermissionDenied=1209,

enum { 
 ePttCallInactive=0,
 ePttCallActive
};
enum {
  COMMON_ACTION_CAUSE
  PTT_SPECIFIED_ACTION_CAUSE
  PTP_SPECIFIED_ACTION_CAUSE
};

enum {
 ePttGroupStateUnknown,
 ePttGroupStateIdle,
 ePttGroupStateMaster,
 ePttGroupStateMonitor,
 ePttGroupStateP2PCall,
};

//Currently only 0,4 are supported
enum {
 eAirInterfaceServiceVoiceGroupCall=0,
 eAirInterfaceServiceVoiceMulticast=1,
 eAirInterfaceServiceVideoGroupCall=2,
 eAirInterfaceServiceVideoMulticast=3,
 eAirInterfaceServiceVoiceP2PCall=4,
 eAirInterfaceServiceVideoP2PCall=5,
};
enum {
 ePttCallGranted=0,
 ePttCallDenied=1,
 ePttCallQueued=2,
};
enum {
 ePttGroupOwnerIndOrignator=0,
 ePttGroupOwnerIndNonOrignator=1,
};

enum {
 ePttCapVoiceSupported=0x01,
 ePttCapSmsSupported=0x02,
 ePttCapPvSupported=0x04,
};

enum {
 ePttCallRefPriority0	= 0,
 ePttCallRefPriority1,
 ePttCallRefPriority2,
 ePttCallRefPriority3,
 ePttCallRefPriority4,
 ePttCallRefPriority5,
 ePttCallRefPriority6,
 ePttCallRefPriority7,
 ePttCallRefPriority8,
 ePttCallRefPriority9,
 ePttCallRefPriority10,
 ePttCallRefPriority11,
 ePttCallRefPriority12,
 ePttCallRefPriority13,
 ePttCallRefPriority14,
 ePttCallRefPriority15,
 ePttCallRefPriorityHighest = ePttCallRefPriority0,
 ePttCallRefPriorityNormal  = ePttCallRefPriority15,
};

enum {
 ePttCallInstanceDefault=0,
};

/*
enum {
 ePttCallStatusProgressing=0,
 ePttCallStatusQueued,
 ePttCallStatusCalledPartyPaged,
 ePttCallStatusContinue,
 ePttCallStatusHangTimeExpired,
 ePttCallStatusIncoming=99,
};*/


/*
struct PttEmergencyInfo{
  int type;
  int pid;//for group call ,it's group id,for p2p call,it's callee id
};
struct PttGroupInfo{
  int gid;
  int gpriority;
  int gstate;
  char* gname;
};
struct PttGroups{
  int groups_number;
  int dyn_groups_number;
  char* tun;
  struct PttGroupInfo* ginfo;
};*/

struct PttCall {
  int active;
  int inst;//android require index start from 1,
  int comm;
  int state;
  int isMT;
  int mode;//0:voice,1:data,2:fax
  int isMpty;
  char* number;
};

int parse_cgiu(char* line,PttGroups* pgs);
void free_ptt_groups(PttGroups* pgs);


int get_ptt_group_info(void);
int get_ptt_service_info(void);
int join_ptt_group(int gid,int priority);
int quit_ptt_group(int gid);
int monitor_ptt_group(int gid);
/*
 *pid is party id,
 * If aiservice is 4/5,it's p2p call and pid is telephone number ;
 * If aiservice is 0/1/2/3,it's group call and pid is group id 
*/
int request_ptt_group_master_call(int instance,int aiservice,int priority,int pid);
int release_ptt_group_master_call(int instance,int pid);
int request_ptt_group_p2p_call(int instance,int aiservice,int priority,int pid);
int request_ptt_group_p2p_call2(int instance,char* address);
int hook_ptt_group_p2p_call(void);
int hangup_ptt_group_p2p_call(void);

void requestGetCurrentCallsPTT(void *data, size_t datalen, RIL_Token t);

void pttcall_call_info_indicate(int active,int inst,int callstatus,int aiservice,int pid,int isMT);

#endif 

