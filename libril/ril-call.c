#include <stdio.h>
#include <telephony/ril.h>
#include <telephony/ril_ptt.h>
#include <string.h>
#include <sys/time.h>

#include "at_tok.h"
#include "ril-handler.h"
#include "ril-hardware.h"

/* Last call fail cause, obtained by *ECAV. */
static int s_lastCallFailCause = CALL_FAIL_ERROR_UNSPECIFIED;


static int clccStateToRILState(int state, RIL_CallState *p_state)
{
    switch(state) {
        case 0: *p_state = RIL_CALL_ACTIVE;   return 0;
        case 1: *p_state = RIL_CALL_HOLDING;  return 0;
        case 2: *p_state = RIL_CALL_DIALING;  return 0;
        case 3: *p_state = RIL_CALL_ALERTING; return 0;
        case 4: *p_state = RIL_CALL_INCOMING; return 0;
        case 5: *p_state = RIL_CALL_WAITING;  return 0;
        default: return -1;
    }
}

/**
 * Note: Directly modified line and has *p_call point directly into
 * modified line.
 */
static int callFromCLCCLine(char *line, RIL_Call *p_call,int valid_index)
{
	//+CLCC: 1,0,2,0,0,\"+18005551212\",145
	//index,isMT,state,mode,isMpty(,number,TOA)?

    int err;
    int state;
    int mode;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(p_call->index));
    if (err < 0) goto error;

	if ((kRIL_HW_MC2716 == rilhw->model)||(kRIL_HW_MC8630 == rilhw->model))
	{
		/* rilj requires call id >=1, but 2716 may return 0. -guang */
		if(!p_call->index)
			p_call->index++;//valid_index;
	}

	
    err = at_tok_nextbool(&line, &(p_call->isMT));
    if (err < 0) goto error;
    err = at_tok_nextint(&line, &state);
    if (err < 0) goto error;
    err = clccStateToRILState(state, &(p_call->state));
    if (err < 0) goto error;
    err = at_tok_nextint(&line, &mode);
    if (err < 0) goto error;
    p_call->isVoice = (mode == 0);//0:voice,1:data,2:fax

    //ignore non voice call
    if(!p_call->isVoice) return -1;
    err = at_tok_nextbool(&line, &(p_call->isMpty));//multi party call
    if (err < 0) goto error;

    if (at_tok_hasmore(&line)) {
        err = at_tok_nextstr(&line, &(p_call->number));

        // tolerate null here 
        if (err < 0) return 0;

        // Some lame implementations return strings
        // like "NOT AVAILABLE" in the CLCC line
        if (p_call->number != NULL
            && 0 == strspn(p_call->number, "+0123456789")
        ) {
            p_call->number = NULL;
        }

        err = at_tok_nextint(&line, &p_call->toa);
        if (err < 0) goto error;
    }

	
	#if (RIL_VERSION>2)
    p_call->uusInfo = NULL;
	#endif

	//
	//CallLevelControl(p_call);
	
    return 0;

error:
    ERROR("invalid CLCC line\n");
    return -1;
}


/**
 * RIL_REQUEST_HANGUP_WAITING_OR_BACKGROUND
 *
 * Hang up waiting or held (like AT+CHLD=0)
 ok
*/
void requestHangupWaitingOrBackground(void *data, size_t datalen,
                                      RIL_Token t)
{
	ATResponse *p_response=NULL;
	int err=0;
	switch(rilhw->model)
	{
		case kRIL_HW_MF210:at_send_command("AT+CHUP", NULL);break;
		case kRIL_HW_MC2716:
		case kRIL_HW_MC8630:
			at_send_command("AT+CHV", NULL);break;
		case kRIL_HW_EM350:
			hangup_ptt_group_p2p_call();
			break;
		case kRIL_HW_AD3812:			
		{	
			// 3GPP 22.030 6.5.5
			// "Releases all held calls or sets User Determined User Busy
			//	(UDUB) for a waiting call."
			err = at_send_command("AT+CHLD=0", &p_response);			
			if (err < 0 || p_response->success == 0)
			{
				err = -1;
			}
		}
		break;
		default:at_send_command("ATH", NULL);break;			
	
	}
	
	if(p_response)free(p_response);
	
    RIL_onRequestComplete(t, (!err)?RIL_E_SUCCESS:RIL_E_GENERIC_FAILURE, NULL, 0);
}

/**
 * RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND
 *
 * Hang up waiting or held (like AT+CHLD=1)
 ok
*/
void requestHangupForegroundResumeBackground(void *data, size_t datalen,
                                             RIL_Token t)
{
	ATResponse *p_response=NULL;
	int err=0;

	switch(rilhw->model)
	{
		case kRIL_HW_MF210:at_send_command("AT+CHUP", NULL);break;
		case kRIL_HW_MC2716:
		case kRIL_HW_MC8630:
			at_send_command("AT+CHV", NULL);break;
		case kRIL_HW_EM350:
			hangup_ptt_group_p2p_call();
			break;
		case kRIL_HW_AD3812:			
		{
			// 3GPP 22.030 6.5.5
			// "Releases all active calls (if any exist) and accepts
			//	the other (held or waiting) call."
			err = at_send_command("AT+CHLD=1", &p_response);			
			if (err < 0 || p_response->success == 0)
			{
				err = -1;
			}
		}break;
		default:at_send_command("ATH", NULL);break;			
	
	}
	
	if(p_response)free(p_response);
	
    RIL_onRequestComplete(t, (!err)?RIL_E_SUCCESS:RIL_E_GENERIC_FAILURE, NULL, 0);

}

/**
 * RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE
 *
 * Switch waiting or holding call and active call (like AT+CHLD=2)
 ok
*/
void requestSwitchWaitingOrHoldingAndActive(void *data, size_t datalen,
                                            RIL_Token t)
{
	ATResponse *p_response=NULL;
	int err=0;

	switch(rilhw->model)
	{
		case kRIL_HW_MF210:at_send_command("AT+CHUP", NULL);break;
		case kRIL_HW_MC2716:
		case kRIL_HW_MC8630:
			at_send_command("AT+CHV", NULL);break;
		case kRIL_HW_EM350:
			hangup_ptt_group_p2p_call();
			break;
		case kRIL_HW_AD3812:
		{
			err = at_send_command("AT+CHLD=2", &p_response);			
			if (err < 0 || p_response->success == 0)
			{
				err = -1;
			}
		}
		break;

		default:at_send_command("ATH", NULL);break;			
		
	
	}

	if(p_response)free(p_response);
	
    RIL_onRequestComplete(t, (!err)?RIL_E_SUCCESS:RIL_E_GENERIC_FAILURE, NULL, 0);
}

/**
 * RIL_REQUEST_CONFERENCE
 *
 * Conference holding and active (like AT+CHLD=3)
 ok
*/
void requestConference(void *data, size_t datalen, RIL_Token t)
{
	ATResponse *p_response=NULL;
	int err=0;

    // 3GPP 22.030 6.5.5
            // "Adds a held call to the conversation"
    err = at_send_command("AT+CHLD=3", &p_response);
	
	if (err < 0 || p_response->success == 0)
	{
		err = -1;
	}
	free(p_response);
    RIL_onRequestComplete(t, (!err)?RIL_E_SUCCESS:RIL_E_GENERIC_FAILURE, NULL, 0);
}

/**
 * RIL_REQUEST_SEPARATE_CONNECTION
 *
 * Separate a party from a multiparty call placing the multiparty call
 * (less the specified party) on hold and leaving the specified party 
 * as the only other member of the current (active) call
 *
 * Like AT+CHLD=2x
 *
 * See TS 22.084 1.3.8.2 (iii)
 * TS 22.030 6.5.5 "Entering "2X followed by send"
 * TS 27.007 "AT+CHLD=2x"
 ok
*/
void requestSeparateConnection(void *data, size_t datalen, RIL_Token t)
{
	char cmd[12];
	int party = ((int*)data)[0];

	// Make sure that party is in a valid range.
	// (Note: The Telephony middle layer imposes a range of 1 to 7.
	// It's sufficient for us to just make sure it's single digit.)
	if (party > 0 && party < 10){
		int err;
		sprintf(cmd, "AT+CHLD=2%d", party);
		err = at_send_command(cmd, NULL);
		RIL_onRequestComplete(t, (err<0)?RIL_E_GENERIC_FAILURE:RIL_E_SUCCESS, NULL, 0);
	}
	else{
		RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	}
}

/**
 * RIL_REQUEST_EXPLICIT_CALL_TRANSFER
 *
 * Connects the two calls and disconnects the subscriber from both calls.
 ok
*/
void requestExplicitCallTransfer(void *data, size_t datalen, RIL_Token t)
{
	int err = 0;
	err = at_send_command("AT+CHLD=4",NULL);
	if(err < 0)
		RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	else
		RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

/**
 * RIL_REQUEST_UDUB
 *
 * Send UDUB (user determined used busy) to ringing or 
 * waiting call answer (RIL_BasicRequest r).
 ok
*/
void requestUDUB(void *data, size_t datalen, RIL_Token t)
{
    /* user determined user busy */
            /* sometimes used: ATH */
    at_send_command("ATH", NULL);

    /* success or failure is ignored by the upper layer here.
          it will call GET_CURRENT_CALLS and determine success that way */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

/**
 * RIL_REQUEST_SET_MUTE
 *
 * Turn on or off uplink (microphone) mute.
 *
 * Will only be sent while voice call is active.
 * Will always be reset to "disable mute" when a new voice call is initiated.
 ok
*/
void requestSetMute(void *data, size_t datalen, RIL_Token t)
{
    int mute = ((char *)data)[0];	
    char *cmd;
    asprintf(&cmd, "AT+CMUT=%d", (int)mute);
    at_send_command(cmd, NULL);
    free(cmd);
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

/**
 * RIL_REQUEST_GET_MUTE
 *
 * Queries the current state of the uplink mute setting.
 ok
*/
void requestGetMute(void *data, size_t datalen, RIL_Token t)
{
	int ret;
    int muted;	
    ATResponse *p_response;
    char* line;
    ret = at_send_command_singleline("AT+CMUT?","+CMUT:",&p_response);	
    if(ret||!p_response->success)
	goto error;
    line = p_response->p_intermediates->line;
    ret = at_tok_start(&line);
    if (ret < 0) goto error;

    ret = at_tok_nextint(&line, &muted);
    if (ret < 0) goto error;
	
    RIL_onRequestComplete(t, RIL_E_SUCCESS, &muted, sizeof(int));
    at_response_free(p_response);
    return;
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
}

/**
 * RIL_REQUEST_LAST_CALL_FAIL_CAUSE
 *
 * Requests the failure cause code for the most recently terminated call.
*
 * See also: RIL_REQUEST_LAST_PDP_FAIL_CAUSE
 ok
 */
void requestLastCallFailCause(void *data, size_t datalen, RIL_Token t)
{
    RIL_onRequestComplete(t, RIL_E_SUCCESS, &s_lastCallFailCause, sizeof(int));
}

static void sendCallStateChanged(void *param)
{
    RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
                              NULL, 0);
}

/**
 * RIL_REQUEST_GET_CURRENT_CALLS
 *
 * Requests current call list
 *
 * "data" is NULL
 *
 * "response" must be a "const RIL_Call **"
 *
 * Valid errors:
 *
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE (radio resetting)
 *  GENERIC_FAILURE
 *      (request will be made again in a few hundred msec)
 */

void requestGetCurrentCalls(void *data, size_t datalen, RIL_Token t)
{
    int err;
    ATResponse *p_response;
    ATLine *p_cur;
    int countCalls;
    int countValidCalls;
    RIL_Call *p_calls;
    RIL_Call **pp_calls;
    int i;
    int needRepoll = 0;
	int need_acquire_wakelock=0;

    if(kRIL_HW_EM350 ==rilhw->model){
      requestGetCurrentCallsPTT(data,datalen,t);
      return;
    }

    err = at_send_command_multiline ("AT+CLCC", "+CLCC:", &p_response);

    if (err != 0 || p_response->success == 0) {
		goto error;
        return;
    }

    //count the calls
    for (countCalls = 0, p_cur = p_response->p_intermediates
            ; p_cur != NULL
            ; p_cur = p_cur->p_next
    ) {
        countCalls++;
    }

    //yes, there's an array of pointers and then an array of structures 
    pp_calls = (RIL_Call **)alloca(countCalls * sizeof(RIL_Call *));
    p_calls = (RIL_Call *)alloca(countCalls * sizeof(RIL_Call));
    memset (p_calls, 0, countCalls * sizeof(RIL_Call));

    // init the pointer array 
    for(i = 0; i < countCalls ; i++) {
        pp_calls[i] = &(p_calls[i]);
    }

    for (countValidCalls = 0, p_cur = p_response->p_intermediates
            ; p_cur != NULL
            ; p_cur = p_cur->p_next
    ) {
        err = callFromCLCCLine(p_cur->line, p_calls + countValidCalls,countValidCalls+1/*1 based*/);

		
        if (err != 0) {
            continue;
        }

		//anyway we need acquire wakelock for any data/voice call
		rilhw_acquire_wakelock();

		///*		
		//if(!rilhw->cdma_device)
		{
	        if (p_calls[countValidCalls].state != RIL_CALL_ACTIVE
	            && p_calls[countValidCalls].state != RIL_CALL_HOLDING)
	        {
	            needRepoll = 1;
	        }
		}
		//*/
		
		
        countValidCalls++;
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, pp_calls,
            countValidCalls * sizeof (RIL_Call *));

    at_response_free(p_response);
	if(needRepoll){
		
		const struct timespec TIMEVAL_CALLSTATEPOLL = { 1, 0 };
		enqueueRILEvent (sendCallStateChanged, NULL, &TIMEVAL_CALLSTATEPOLL);
	}

	if(!countCalls)
	{		
		rilhw_release_wakelock();
	}
    return;
error:
	//
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
}


/** 
 * RIL_REQUEST_DIAL
 *
 * Initiate voice call.
 ok
*/
void requestDial(void *data, size_t datalen, RIL_Token t)
{
    RIL_Dial *p_dial;
    char *cmd;
    const char *clir;
    int ret;

    p_dial = (RIL_Dial *)data;

	if(kRIL_HW_MC2716 ==rilhw->model||
	  kRIL_HW_MC8630==rilhw->model)
	{
		asprintf(&cmd, "AT+CDV%s", p_dial->address);
		
		ret = at_send_command(cmd, NULL);
		
		free(cmd);
		
	}else if(kRIL_HW_EM350 ==rilhw->model){
		request_ptt_group_p2p_call2(0,p_dial->address);		
	}else //gsm
	{
		switch (p_dial->clir) {
			case 1: clir = "I"; break;	/*invocation*/
			case 2: clir = "i"; break;	/*suppression*/
			default:
			case 0: clir = ""; break;	/*subscription default*/
		}
		
		asprintf(&cmd, "ATD%s%s;", p_dial->address,clir);
		
		ret = at_send_command(cmd, NULL);
		
		free(cmd);
		
	}

    /* success or failure is ignored by the upper layer here.
       it will call GET_CURRENT_CALLS and determine success that way */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

/**
 * RIL_REQUEST_ANSWER
 *
 * Answer incoming call.
 *
 * Will not be called for WAITING calls.
 * RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE will be used in this case
 * instead.
 ok
*/
void requestAnswer(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *atresponse = NULL;
    int err;

    if(kRIL_HW_EM350 ==rilhw->model){
        err = at_send_command("AT+CATA", &atresponse);
	pttcall_call_info_indicate(ePttCallActive,ePttCallInstanceDefault,
	ePttCallStatusProgressing,eAirInterfaceServiceVoiceP2PCall,
	0,1);
    }else
    	err = at_send_command("ATA", &atresponse);

    if (err < 0 || atresponse->success == 0)
        goto error;

    /* Success or failure is ignored by the upper layer here,
       it will call GET_CURRENT_CALLS and determine success that way. */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

finally:
    at_response_free(atresponse);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}

/**
 * RIL_REQUEST_HANGUP
 *
 * Hang up a specific line (like AT+CHLD=1x).
 ok
*/
void requestHangup(void *data, size_t datalen, RIL_Token t)
{

	switch(rilhw->model)
	{
		case kRIL_HW_MF210:
		case kRIL_HW_M305:
			at_send_command("AT+CHUP", NULL);
			break;
		case kRIL_HW_MC2716:
		case kRIL_HW_MC8630:
			at_send_command("AT+CHV", NULL);
			break;
		case kRIL_HW_EM350:
			hangup_ptt_group_p2p_call();
			break;
		case kRIL_HW_AD3812:
			{				
				int *p_line;
				
				int ret;
				char *cmd;
				
				p_line = (int *)data;
				
				// 3GPP 22.030 6.5.5
				// "Releases a specific active call X"
				asprintf(&cmd, "AT+CHLD=1%d", p_line[0]);
				
				ret = at_send_command(cmd, NULL);
				
				free(cmd);
			}
		default:
			at_send_command("ATH", NULL);    
			break;
	}


    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

}

/**
 * RIL_REQUEST_DTMF
 *
 * Send a DTMF tone
 *
 * If the implementation is currently playing a tone requested via
 * RIL_REQUEST_DTMF_START, that tone should be cancelled and the new tone
 * should be played instead.
 ok
*/
void requestDTMF(void *data, size_t datalen, RIL_Token t)
{
    char c = ((char *)data)[0];
    char *cmd=NULL;
	switch(rilhw->model)
	{
		case kRIL_HW_MC2716:	
		case kRIL_HW_MC8630:
			//AT^DTMF=call id,dtmf digit,on length,off length
			asprintf(&cmd, "AT^DTMF=0,%c,150", (int)c);	
			break;
		default:
			asprintf(&cmd, "AT+VTS=%c", (int)c);break;
			
	}
    at_send_command(cmd, NULL);
    if(cmd) free(cmd);
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

/**
 * RIL_REQUEST_DTMF_START
 *
 * Start playing a DTMF tone. Continue playing DTMF tone until 
 * RIL_REQUEST_DTMF_STOP is received .
 *
 * If a RIL_REQUEST_DTMF_START is received while a tone is currently playing,
 * it should cancel the previous tone and play the new one.
 *
 * See also: RIL_REQUEST_DTMF, RIL_REQUEST_DTMF_STOP.
 ok
 */
void requestDTMFStart(void *data, size_t datalen, RIL_Token t)
{
	char* cmd=NULL;
    char c = ((char *)data)[0];

	at_send_command("AT+CMUT=1", NULL);	

	
	switch(rilhw->model)
	{
		case kRIL_HW_MC2716:	
		case kRIL_HW_MC8630:
			//AT^DTMF=call id,dtmf digit,on length,off length
			asprintf(&cmd, "AT^DTMF=0,%c,150", (int)c);	
			break;
		default:
			asprintf(&cmd, "AT+VTS=%c", (int)c);
			break;
	}
    at_send_command(cmd, NULL);
    free(cmd);

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

/**
 * RIL_REQUEST_DTMF_STOP
 *
 * Stop playing a currently playing DTMF tone.
 *
 * See also: RIL_REQUEST_DTMF, RIL_REQUEST_DTMF_START.
 ok
 */
void requestDTMFStop(void *data, size_t datalen, RIL_Token t)
{
	at_send_command("AT+CMUT=0", NULL);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

void requestCDMADTMF(void *data, size_t datalen, RIL_Token t)
{
    char* dtmf = (( char **)data)[0];
	char* on = (( char **)data)[1];
	char* off = (( char **)data)[2];	
    char *cmd;
	char tone = dtmf[0];
	int on_ms,off_ms;
	
	on_ms=off_ms=0;
	if(on)	on_ms = atoi(on);
	if(off)	off_ms = atoi(off);

	if(on_ms>350)
		on_ms=350;
	else if(on_ms>300)
		on_ms=300;
	else if(on_ms>250)
		on_ms=250;
	else if(on_ms>200)
		on_ms=200;
	else if(on_ms>150)
		on_ms=150;
	else on_ms=200;
		
	DBG("requestCDMADTMF dtmf=%c, on=%d,off=%d\n",tone,on_ms,off_ms);
	//AT^DTMF=call id,dtmf digit,on length,off length
    asprintf(&cmd, "AT^DTMF=0,%c,%d,%d", tone,on_ms,off_ms);	
    at_send_command(cmd, NULL);
    free(cmd);
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

