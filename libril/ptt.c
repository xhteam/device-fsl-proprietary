#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/select.h>
#include <telephony/ril.h>
#include <telephony/ril_ptt.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <linux/route.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <hardware_legacy/power.h>
#include <cutils/properties.h>
#include "eventset.h"
#include "ril-handler.h"
#include "ptt.h"

#define MAX_BLOCK_INDICATOR 8

static const char *BlockedIndicator[] = {
/* 0 */	"IMSI & IMEI both Remote ON",
/* 1 */	"IMSI temporarily Remote OFF, IMEI Remote ON",
/* 2 */	"IMSI permanently Remote OFF, IMEI Remote ON",
/* 3 */	"IMSI Remote ON, IMEI temporarily Remote OFF",
/* 4 */	"IMSI Remote ON, IMEI permanently Remote OFF",
/* 5 */	"IMSI temporarily Remote OFF, IMEI permanently Remote OFF",
/* 6 */ "IMSI permanently Remote OFF, IMEI temporarily Remote OFF",
/* 7 */ "IMSI & IMEI both temporarily Remote OFF",
/* 8 */ "IMSI &	IMEI both permanently Remote OFF",
};

static char* NextSplit(char **p_cur,char split)
{
    if (*p_cur == NULL)
        return NULL;

    while (**p_cur != '\0' && **p_cur != split)
        (*p_cur)++;

    if (**p_cur == split)
        return (*p_cur);
    return NULL;
}
//parse +CGIU: 2;0; ”12345”;1,2; 1,10,0,””; 2,0,0,”X-man”
int parse_cgiu(char* line,PttGroups* pgs){
  char *start,*p,*p1;
  int group_number,dyn_group_number;
  char* tun;
  int index=0;
  int emergency_type,emergency_number;
  int gid,gpriority,gstate;
  char* gname;
  DBG("CGIU line:%s",line);
  start = line;
  at_tok_start(&start);//skip prefix XXX: 
  emergency_type=emergency_number=0;
  p = start;
  p1 = NextSplit(&p,';');
  if(p1) *p1='\0';
  at_tok_nextint(&start,&group_number);
  DBG("groupnumber=%d",group_number);
  
  start = p = ++p1;
  p1 = NextSplit(&p,';');
  if(p1) *p1='\0';
  at_tok_nextint(&start,&dyn_group_number);
  DBG("dyn groupnumber=%d",dyn_group_number);

  if(pgs){
  	pgs->groups_number = group_number;
	pgs->dyn_groups_number = dyn_group_number;
	pgs->ginfo = calloc(group_number+dyn_group_number,sizeof(PttGroupInfo));
  }
  start = p = ++p1;
  p1 = NextSplit(&p,';');
  if(p1) *p1='\0';
  at_tok_nextstr(&start,&tun);
  DBG("tun=%s",tun);
  if(pgs)
  	pgs->tun = strdup(tun);

  

  start = p = ++p1;
  p1 = NextSplit(&p,';');
  if(p1) *p1='\0';  
  at_tok_nextint(&start,&emergency_type);
  at_tok_nextint(&start,&emergency_number);
  DBG("emergency_type=%d,emergency_number=%d",emergency_type,emergency_number);
  if(pgs){
     pgs->emerginfo.type = emergency_type;
     pgs->emerginfo.number = emergency_number;
  }
  do {
    start = p = ++p1;
    if(at_tok_hasmore(&start)){
    p1 = NextSplit(&p,';');if(p1) *p1='\0';
    gid=gpriority=gstate=0;
    gname=NULL;
    at_tok_nextint(&start,&gid);
    at_tok_nextint(&start,&gpriority);
    at_tok_nextint(&start,&gstate);
    at_tok_nextstr(&start,&gname);
    DBG("group->gid[%d],gpriority[%d],gstate[%d],gname[%s]",gid,gpriority,gstate,gname);
		if(pgs){
			pgs->ginfo[index].gid = gid;
			pgs->ginfo[index].gpriority= gpriority;
			pgs->ginfo[index].gstate = gstate;
			pgs->ginfo[index].gname = strdup(gname);
			index++;
		}
    }else {
      break;
    }
    if(!p1) break;
    
  }while(1);

  return 0;  
  
}
void free_ptt_groups(PttGroups* pgs){
	int index=0;
	if(pgs->tun) free(pgs->tun);
	for(;index<(pgs->dyn_groups_number+pgs->groups_number);index++)
		if(pgs->ginfo[index].gname) free(pgs->ginfo[index].gname);
	//free ginfo
	free(pgs->ginfo);
}
/*
 *case
*/
int get_ptt_group_info(void){
  ATResponse *atresponse = NULL;
  int err;
  char* line;

  err = at_send_command_singleline("AT+CGIU","+CGIU:", &atresponse);
  if(err != 0 || atresponse->success == 0){
    ERROR("cgiu query failed\n");
    goto ptt_get_failed;
  }
  line = strdup(atresponse->p_intermediates->line);
 //+CGIU: 2;0; ”12345”;1,2; 1,10,0,””; 2,0,0,”X-man”
  parse_cgiu(line,NULL);  
  free(line);
  
ptt_get_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;
}
int join_ptt_group(int gid,int priority){
  ATResponse *atresponse = NULL;
  int err;
  char* line;
  char* cmd;
  
  asprintf(&cmd,"AT+CTGS=%d,%d",gid,priority);
  err = at_send_command_singleline(cmd,"+CTGS:", &atresponse);
  free(cmd);
  if(err != 0 || atresponse->success == 0){
    int cme_error;
    at_get_cme_error(atresponse,(ATCmeError*)&cme_error);
    ERROR("ctgs query failed cme error=%d\n",cme_error);    
    goto join_ptt_group_failed;
  }
  DBG("Join GID[%d],Priority[%d] okay",gid,priority);
join_ptt_group_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;
}

int quit_ptt_group(int gid){
  return 0;
}
int request_ptt_group_master_call(int instance,int aiservice,int priority,int pid){
  ATResponse *atresponse = NULL;
  int err;
  char* line;
  char* cmd;
  
  asprintf(&cmd,"AT+CAPTTD=%d,%d,%d,\"%d\"",instance,aiservice,priority,pid);
  err = at_send_command(cmd,&atresponse);
  free(cmd);
  if(err != 0 || atresponse->success == 0){
    int cme_error;
    at_get_cme_error(atresponse,(ATCmeError*)&cme_error);
    ERROR("CAPTTD failed cme error=%d\n",cme_error);    
    goto capttd_failed;
  }
  
  DBG("call okay");

capttd_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;
}
int release_ptt_group_master_call(int instance,int pid){
  ATResponse *atresponse = NULL;
  int err;
  char* line;
  char* cmd;
  
  asprintf(&cmd,"AT+CAPTTR=%d,%d",instance,pid);
  err = at_send_command(cmd,&atresponse);
  free(cmd);
   if(err != 0 || atresponse->success == 0){
    int cme_error;
    at_get_cme_error(atresponse,(ATCmeError*)&cme_error);
    ERROR("CAPTTR failed cme error=%d\n",cme_error);    
    goto capttr_failed;
  }
  
  DBG("release call okay");

capttr_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;
}

static struct PttCall currcall;

int request_ptt_group_p2p_call(int instance,int aiservice,int priority,int pid){
  ATResponse *atresponse = NULL;
  int err;
  char* line;
  char* cmd;
  
  asprintf(&cmd,"AT+CAPTTD=%d,%d,%d,\"%d\"",instance,aiservice,priority,pid);
  err = at_send_command(cmd,&atresponse);
  free(cmd);
  if(err != 0 || atresponse->success == 0){
    int cme_error;
    at_get_cme_error(atresponse,(ATCmeError*)&cme_error);
    ERROR("CAPTTD failed cme error=%d\n",cme_error);    
    goto capttd_failed;
  }
  
  DBG("p2p call okay");

  pttcall_call_info_indicate(ePttCallActive,ePttCallInstanceDefault,ePttCallStatusProgressing,eAirInterfaceServiceVoiceP2PCall,pid,0);

capttd_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;
}

int request_ptt_group_p2p_call2(int instance,char* address){
  ATResponse *atresponse = NULL;
  int err;
  char* line;
  char* cmd;
  int pid=0;
  
  asprintf(&cmd,"AT+CAPTTD=%d,%d,%d,\"%s\"",instance,eAirInterfaceServiceVoiceP2PCall,ePttCallRefPriorityNormal,address);
  err = at_send_command(cmd,&atresponse);
  free(cmd);
  if(err != 0 || atresponse->success == 0){
    int cme_error;
    at_get_cme_error(atresponse,(ATCmeError*)&cme_error);
    ERROR("CAPTTD failed cme error=%d\n",cme_error);    
    goto capttd_failed;
  }
  pid = atoi(address);
  pttcall_call_info_indicate(ePttCallActive,ePttCallInstanceDefault,ePttCallStatusProgressing,eAirInterfaceServiceVoiceP2PCall,pid,0);
  DBG("p2p call okay");

capttd_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;
  
}

int hook_ptt_group_p2p_call(void){
  ATResponse *atresponse = NULL;
  int err;
     
  err = at_send_command("AT+CATA",&atresponse);
   if(err != 0 || atresponse->success == 0){
    int cme_error;
    at_get_cme_error(atresponse,(ATCmeError*)&cme_error);
    ERROR("CATA failed cme error=%d\n",cme_error);    
    goto cata_failed;
  }
  
  DBG("answer call okay");

cata_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;
}
int hangup_ptt_group_p2p_call(void){
  ATResponse *atresponse = NULL;
  int err;
     
  err = at_send_command("AT+CATH",&atresponse);
   if(err != 0 || atresponse->success == 0){
    int cme_error;
    at_get_cme_error(atresponse,(ATCmeError*)&cme_error);
    ERROR("CATA failed cme error=%d\n",cme_error);    
    goto cath_failed;
  }
  
  DBG("hangup call okay");

cath_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;

}



static int ctccStateToRILState(int state, RIL_CallState *p_state)
{
    switch(state) {
        case 0:
	case 2: *p_state = RIL_CALL_ACTIVE;   return 0;
        case 1: *p_state = RIL_CALL_HOLDING;  return 0;
       // case 2: *p_state = RIL_CALL_DIALING;  return 0;
        case 3: *p_state = RIL_CALL_ALERTING; return 0;
        case 4: case 99: *p_state = RIL_CALL_INCOMING; return 0;
        case 5: *p_state = RIL_CALL_WAITING;  return 0;
        default: return -1;
    }
}
static void convert_pttcall_to_rilcall(struct PttCall* ptt,RIL_Call* ril){
  ril->index = ptt->inst+1;//inst seems always start from 0;
  ctccStateToRILState(ptt->state,&ril->state);
  ril->number = ptt->number;
  #if (RIL_VERSION>2)
  ril->uusInfo = NULL;
  #endif
}
//There is no command to query current call 
void requestGetCurrentCallsPTT(void *data, size_t datalen, RIL_Token t){
    int err;
    ATResponse *p_response;
    ATLine *p_cur;
    int countCalls;
    int countValidCalls;
    RIL_Call *p_calls=NULL;
    RIL_Call **pp_calls=NULL;
    int i;

    //count the calls
    countCalls = currcall.active?1:0;
    countValidCalls = 0; 
    if(countCalls){
    //yes, there's an array of pointers and then an array of structures 
    pp_calls = (RIL_Call **)alloca(countCalls * sizeof(RIL_Call *));
    p_calls = (RIL_Call *)alloca(countCalls * sizeof(RIL_Call));
    memset (p_calls, 0, countCalls * sizeof(RIL_Call));

    // init the pointer array 
    for(i = 0; i < countCalls ; i++) {
        pp_calls[i] = &(p_calls[i]);
    }

     convert_pttcall_to_rilcall(&currcall,p_calls+countValidCalls);
     countValidCalls++;
   }
    
    RIL_onRequestComplete(t, RIL_E_SUCCESS, pp_calls,
            countValidCalls * sizeof (RIL_Call *));

    return;
error:
	//
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);    
}

void pttcall_call_info_indicate(int active,int inst,int callstatus,int aiservice,int pid,int isMT){
  currcall.active = active;
  if(active){
     currcall.inst = inst;
     currcall.state = callstatus;
     if(pid){
     	if(currcall.number) free(currcall.number);
     	asprintf(&currcall.number,"%d",pid);
     }
     currcall.isMT = isMT;
  }
}

static struct ActionCauses action_cause[] = { 
	/* common cuases */
	{0   , "ok"},
	{31  , "network timeout"},
	{1001, "network error"},
	{1002, "unsigned number error"},
	{1003, "user authentication failed"},
	{1004, "service option not supported"},
	{1005, "requested service option not subscribed"},
	{1006, "unspecified error"},
	{1007, "invalid parameter"},
	{1008, "network detach"},
	{1009, "high priority task interrupt"},
	{1010, "NAS Inner Error"},
	{1011, "IMSI is temporarily blocked"},
	{1012, "IMSI is permanently blocked"},
	{1013, "User Low Priority"},
	{1014, "No PTT Capability"},
	/* ptt causes */
	{1100, "Operation Success"},
	{1101, "group owner close"},
	{1102, "dispatcher close"},
	{1103, "timeout close"},
	{1104, "speaker token timeout"},
	{1105, "release for high priority user"},
	{1106, "queue timeout"},
	{1107, "force to cancel queue"},
	{1108, "requested group not subscribed"},
	{1109, "queue buffer is full"},
	{1110, "not group owner"},
	{1111, "user interrupt"},
	{1112, "Set Failure"},
	{1113, "Out of Service"},
	{1114, "Normal Release"},
	/* ptp causes */
	{1201, "Normal Call Clearing"},
	{1202, "User Busy"},
	{1203, "No User Responding"},
	{1204, "User Alerting No Answer"},
	{1205, "Call Rejected"},
	{1206, "Invalid Number Format"},
	{1207, "User In High Priority Task"},
	{1208, "Caller Permission Denied"},
	{1209, "Callee Permission Denied"},
};

static const char* ptt_getActionCause(int ac){
	int length=sizeof(action_cause)/sizeof(action_cause[0]);
	int i;
	for(i=0;i<length;i++){
		if(action_cause[i].err == ac)
			return action_cause[i].cause;
	}
	return action_cause[0].cause;
}

void requestPttQueryAvailableGroups(void *data, size_t datalen, RIL_Token t){
  ATResponse *atresponse = NULL;
  int err;
  char* line;
  PttGroups pgs;

  err = at_send_command_singleline("AT+CGIU","+CGIU:", &atresponse);
  if(err != 0 || atresponse->success == 0){
	ERROR("cgiu query failed\n");
	goto ptt_get_failed;
  }
  line = strdup(atresponse->p_intermediates->line);
 //+CGIU: 2;0; ”12345”;1,2; 1,10,0,””; 2,0,0,”X-man”
  parse_cgiu(line,&pgs);  
  free(line);
  

  if(atresponse)
	at_response_free(atresponse);  
  
  RIL_onRequestComplete(t, RIL_E_SUCCESS, &pgs,sizeof(pgs));
  free_ptt_groups(&pgs);
  return;
  
ptt_get_failed:
  if(atresponse)
	at_response_free(atresponse);  
  RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL,0);	  
	
}
void requestPttGroupSetup(void *data, size_t datalen, RIL_Token t){
	ATResponse *p_response = NULL;
	int gid,priority,indicator;
	char cmd[256];
	int err;
	int response=acOK;
	priority=indicator=-1;
	gid = ((int*)data)[0];
	datalen--;
	if(datalen>0)
		priority = ((int*)data)[1];
	datalen--;
	if(datalen>0)
		indicator = ((int*)data)[2];
	if(indicator>=0)
		sprintf(cmd,"AT+CTGS=%d,%d,%d",gid,priority,indicator);	
	else if(priority>=0)
		sprintf(cmd,"AT+CTGS=%d,%d",gid,priority);
	else
		sprintf(cmd,"AT+CTGS=%d",gid);
	err = at_send_command(cmd,&p_response);	

	if (err < 0 || p_response->success == 0) {
		// assume radio is off
		goto error;
	}

	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));
	return;
error:
	at_get_cme_error(p_response,(ATCmeError *)&response);
	DBG("ActionCause:%s\n",ptt_getActionCause(response));
	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, &response, sizeof(int));	
}
void requestPttGroupRelease(void *data, size_t datalen, RIL_Token t){
	ATResponse *p_response = NULL;
	int ccinstance,gid;
	char cmd[256];
	int err;
	int response=acOK;
	ccinstance = ((int*)data)[0];
	gid = ((int*)data)[1];
	sprintf(cmd,"AT+CTGR=%d,%d",ccinstance,gid);	
	err = at_send_command(cmd,&p_response);

	if (err < 0 || p_response->success == 0) {
		// assume radio is off
		goto error;
	}

	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));
	return;
error:
	at_get_cme_error(p_response,(ATCmeError *)&response);
	DBG("ActionCause:%s\n",ptt_getActionCause(response));
	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, &response, sizeof(int));	
}
void requestPttCallDial(void *data, size_t datalen, RIL_Token t){
	ATResponse *p_response = NULL;
	int ccinstance,ai,priority,partyid;
	char cmd[256];
	int err;
	ccinstance = ((int*)data)[0];
	ai = ((int*)data)[1];
	priority = ((int*)data)[2];
	partyid = ((int*)data)[3];
	sprintf(cmd,"AT+CAPTTD=%d,%d,%d,\"%d\"",ccinstance,ai,priority,partyid);	
	err = at_send_command(cmd,&p_response);

	if (err < 0 || p_response->success == 0) {
		// assume radio is off
		goto error;
	}

	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
	return;
error:
	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}
void requestPttCallHangup(void *data, size_t datalen, RIL_Token t){
	ATResponse *p_response = NULL;
	int ccinstance,gid;
	char cmd[256];
	int err;
	ccinstance = ((int*)data)[0];
	gid = ((int*)data)[1];
	sprintf(cmd,"AT+CAPTTR=%d,%d",ccinstance,gid);	
	err = at_send_command(cmd,&p_response);

	if (err < 0 || p_response->success == 0) {
		// assume radio is off
		goto error;
	}

	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
	return;
error:
	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);	

}
void requestPttCurrentGroupScanlistUpdate(void *data, size_t datalen, RIL_Token t){
	ATResponse *p_response = NULL;
	int i,len;
	char cmd[256];
	int err;
	char ret;
	int groupnumber=datalen/4-1;
	if(groupnumber>0){
	    len=sprintf(cmd,"AT+CGSU=%d,\"",((int*)data)[0]);	
	    i=1;
	    while(groupnumber-->0){
		len+=sprintf(cmd+len,"%d ",((int*)data)[i++]);
	    }
	    len+=sprintf(cmd+len,"\"");
	}else {
	    sprintf(cmd,"AT+CGSU=%d",((int*)data)[0]);
	}
	
	err = at_send_command(cmd,&p_response);

	if (err < 0 || p_response->success == 0) {
		// assume radio is off
		goto error;
	}

	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
	return;
error:
	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);	

}
void requestPttQueryBlockedIndicator(void *data, size_t datalen, RIL_Token t){
    ATResponse *p_response = NULL;
	int response;
    int err;
    char *line;
    char ret;

    err = at_send_command_singleline("AT+CTBI?", "+CTBI:", &p_response);

    if (err < 0 || p_response->success == 0) {
        // assume radio is off
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &response);
    if (err < 0) goto error;

    at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, &response,sizeof(int));
	return;
error:
    at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);	
}
void requestPttDeviceInfo(void *data, size_t datalen, RIL_Token t){	
    ATResponse *p_response = NULL;
	int responses[3];
    int err;
    char *line;
    char ret;

    err = at_send_command_singleline("AT+CDINFO?", "+CDINFO:", &p_response);

    if (err < 0 || p_response->success == 0) {
        // assume radio is off
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &responses[0]);
    if (err < 0) goto error;
    err = at_tok_nextint(&line, &responses[1]);
    if (err < 0) goto error;
    err = at_tok_nextint(&line, &responses[2]);
    if (err < 0) goto error;

    at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, responses,3*sizeof(int));
	return;
error:
    at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

}

//parse following lines
//^CPTTINFO:3;6001,15,0,1,"123456","X-man",0
//^CPTTINFO:7;;5,"3002"
//^CPTTINFO:2;8003

int parse_pttinfo(char* line,PttInfo* pi){
  char *start,*p,*p1;
  int givalid,civalid;
  char* mystring;
  char* myline = strdup(line);
  givalid=civalid=0;
  
  DBG("parsing line:\n%s\n",myline);
  start = myline;
  at_tok_start(&start);//skip prefix XXX:   
  p = start;  
  p1 = NextSplit(&p,';');
  
  if(p1) {   
    *p1='\0'; 
  
  at_tok_nextint(&start,&pi->pttstate);
  //DBG("pttstate=%d\n",pi->pttstate); 
  
  //get group info
  start = p = ++p1;  
  p1 = NextSplit(&p,';');
  if(p1) {
	*p1='\0';
  }
  
  if(at_tok_hasmore(&start)){
    at_tok_nextint(&start,&pi->gid);
    givalid|=PTT_BIZSTATE_GID_VALID;
  }
  if(at_tok_hasmore(&start)){
    at_tok_nextint(&start,&pi->gpriority);
    givalid|=PTT_BIZSTATE_GPRIORITY_VALID;
  }
  if(at_tok_hasmore(&start)){
    at_tok_nextint(&start,&pi->gdemandindicator);
    givalid|=PTT_BIZSTATE_GDEMANDIND_VALID;
  }
  if(at_tok_hasmore(&start)){
    at_tok_nextint(&start,&pi->ggrantstatus);
    givalid|=PTT_BIZSTATE_GGRANTSTATUS_VALID;
  }
  if(at_tok_hasmore(&start)){
    at_tok_nextstr(&start,&mystring);
    pi->gspeakernum = strdup(mystring);
    givalid|=PTT_BIZSTATE_GSPKNUM_VALID;
  }
  if(at_tok_hasmore(&start)){
    at_tok_nextstr(&start,&mystring);
    pi->gspeakername = strdup(mystring);
    givalid|=PTT_BIZSTATE_GSPKNAME_VALID;
  }
  if(at_tok_hasmore(&start)){
    at_tok_nextint(&start,&pi->gownerindicator);
    givalid|=PTT_BIZSTATE_GOWNERIND_VALID;
  }
  }
  pi->givalid=givalid;
  
  //get personal call info
  if(p1){
  start = p = ++p1;
  if(at_tok_hasmore(&start)){
    at_tok_nextint(&start,&pi->cpriority);
    civalid|=PTT_BIZSTATE_CIPRIORITY_VALID;
  }
  if(at_tok_hasmore(&start)){
    at_tok_nextstr(&start,&mystring);
    pi->ccalleeid = strdup(mystring);
    civalid|=PTT_BIZSTATE_CICALLEEID_VALID;
  }
  if(at_tok_hasmore(&start)){
    at_tok_nextstr(&start,&mystring);
    pi->ccallerid = strdup(mystring);
    civalid|=PTT_BIZSTATE_CICALLERID_VALID;
  }
  }
  pi->civalid=civalid;
  free(myline);
  return 0;  
  
}

void release_pttinfo(PttInfo* pi){
  if(pi->gspeakernum) free(pi->gspeakernum);
  if(pi->gspeakername) free(pi->gspeakername);
  if(pi->ccalleeid) free(pi->ccalleeid);
  if(pi->ccallerid) free(pi->ccallerid);
}
void requestPttBizState(void *data, size_t datalen, RIL_Token t){
	ATResponse *p_response = NULL;
	PttInfo pi;
	int err;
	char *line;
	char ret;
        memset(&pi,0,sizeof(PttInfo));
	err = at_send_command_singleline("AT^CPTTINFO?", "^CPTTINFO:", &p_response);

	if (err < 0 || p_response->success == 0) {
		// assume radio is off
		goto error;
	}

	line = p_response->p_intermediates->line;
	parse_pttinfo(line,&pi); 
        

	at_response_free(p_response);
	//only report biz state
	RIL_onRequestComplete(t, RIL_E_SUCCESS, &pi,sizeof(PttInfo));
 	release_pttinfo(&pi);
	return;
error:
	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);	
}

//ptt log management
#define EVENT_TIMEOUT  ETIMEDOUT
enum {
 eventLogStart=1,
 eventLogStop,
 eventLogTrigger,
};
static EventSet ptt_es;
static pthread_t thread;
static int pttlog_init=0;

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

static int pipelog2file(int in,int out) 
{
  static unsigned char buf[4096];
  int rlt = safe_read( in, buf, sizeof(buf) );
		
  if( rlt > 0 ){
    write(out,buf,rlt);	
  }
	
  return rlt;
}
static void* ptt_log_reader(void *arg){
  int stop=0;
  int trigger=0;
  int event;
  int fdIn,fdOut;
  int timeout_ms=10;
  char inPort[PROPERTY_VALUE_MAX];
  char OutPort[PROPERTY_VALUE_MAX]; 
  pthread_detach(pthread_self());	
  fdIn=fdOut=-1;
  property_get("persist.ril.login", inPort, "/dev/ttyUSB1");
  property_get("persist.ril.logout", OutPort, "/data/ppp/ptt.log");
  while(!stop){
    event = eventset_wait_timeout(ptt_es,timeout_ms);
    switch(event){
      case eventLogStart:{
	  DBG("PTT log start,inPort[%s]OutPort[%s]\n",inPort,OutPort);
	  fdIn = open(inPort, O_RDONLY );
	  fdOut = open(OutPort, O_RDWR | O_CREAT, 0666);
	  if(fdIn<0){
	  	ERROR("failed to open inPort\n");
	  }
	  if(fdOut<0){
	  	ERROR("failed to open OutPort\n");
	  }
	}
	break;
      case eventLogStop: {
	  close(fdOut);
          close(fdIn);
  	  stop++;
	}break;	
      case eventLogTrigger: trigger++; break;
      default:case EVENT_TIMEOUT: {
	if(trigger){
	  pipelog2file(fdIn,fdOut);
        }	
	}break;
    }
  }
  return NULL;
}

int ptt_log_start(void){
  ATResponse *p_response = NULL;
  int err=0;
  char* line;
  char* validtoken=NULL;
  if(!pttlog_init){
    pttlog_init++;
    eventset_create(&ptt_es);
    pthread_create(&thread, NULL,ptt_log_reader,NULL);
  }
  eventset_set(ptt_es,eventLogStart);
  //

  err = at_send_command_singleline("AT^GLD=255", "^GLD:", &p_response);

  if (err < 0 || p_response->success == 0) {
	// assume radio is off
	goto error;
  }
  line = p_response->p_intermediates->line;
  err = at_tok_start(&line);
  if (err < 0) goto error;
  at_tok_nextstr(&line,&validtoken);
  if(!strcmp(validtoken,"DATA")){
    DBG("Found PTT DATA LOG\n");
    ptt_log_trigger(); 
  }else{
    DBG("PTT DATA LOG Not Found\n");
    ptt_log_stop();
  }
  at_response_free(p_response);
  return 0;
error:
  at_response_free(p_response);
  ptt_log_stop();
  return err;
}
int ptt_log_stop(void){
  eventset_set(ptt_es,eventLogStop);
  sleep(1);
  if(pttlog_init){
    pttlog_init--;
    eventset_destroy(ptt_es);
  }
  return 0;
}
int ptt_log_trigger(void){
  if(!pttlog_init){
    pttlog_init++;
    eventset_create(&ptt_es);
    pthread_create(&thread, NULL,ptt_log_reader,NULL);
  }  
  eventset_set(ptt_es,eventLogTrigger);
  return 0;
}
