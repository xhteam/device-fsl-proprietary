#include <stdio.h>
#include <telephony/ril.h>
#include <telephony/ril_cdma_sms.h>
#include "sms.h"
#include "at_tok.h"
#include "ril-handler.h"

static char s_outstanding_acknowledge = 0;

#define OUTSTANDING_SMS    0
#define OUTSTANDING_STATUS 1

/**
 * RIL_REQUEST_GSM_BROADCAST_GET_SMS_CONFIG
 ok
 */
void requestGSMGetBroadcastSMSConfig(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *atresponse = NULL;
    int err;
    char *cmd;
    char* line;
    RIL_GSM_BroadcastSmsConfigInfo* configInfo;

    err = at_send_command_singleline("AT+CSCB?", "+CSCB:", &atresponse);

    if (err < 0 || atresponse->success == 0)
        goto error;

    configInfo = malloc(sizeof(RIL_GSM_BroadcastSmsConfigInfo));
    memset(configInfo, 0, sizeof(RIL_GSM_BroadcastSmsConfigInfo));

    line = atresponse->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    /* Get the string that yields the service ids.
       We expect the form: "fromServiceId-toServiceId". */    
    err = at_tok_nextstr(&line, &line);
    if (err < 0)
        goto error;

    line = strsep(&line, "\"");
    if (line == NULL)
        goto error;

    err = at_tok_nextint(&line, &configInfo->fromServiceId);
    if (err < 0)
        goto error;

    line = strsep(&line, "-");
    if (line == NULL)
        goto error;

    err = at_tok_nextint(&line, &configInfo->toServiceId);
    if (err < 0)
        goto error;

    /* Get the string that yields the coding schemes.   
       We expect the form: "fromCodeScheme-toCodeScheme". */    
    err = at_tok_nextstr(&line, &line);
    if (err < 0)
        goto error;

    line = strsep(&line, "\"");

    if (line == NULL)
        goto error;

    err = at_tok_nextint(&line, &configInfo->fromCodeScheme);
    if (err < 0)
        goto error;

    line = strsep(&line, "-");
    if (line == NULL)
        goto error;

    err = at_tok_nextint(&line, &configInfo->toCodeScheme);
    if (err < 0)
        goto error;

    err = at_tok_nextbool(&line, (char*)&configInfo->selected);
    if (err < 0)
        goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &configInfo, sizeof(RIL_GSM_BroadcastSmsConfigInfo));

finally:
    free(configInfo);
    at_response_free(atresponse);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}

/**
 * RIL_REQUEST_GSM_BROADCAST_SET_SMS_CONFIG
 ok
 */
void requestGSMSetBroadcastSMSConfig(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *atresponse = NULL;
    int err;
    char *cmd;
    RIL_GSM_BroadcastSmsConfigInfo* configInfo = (RIL_GSM_BroadcastSmsConfigInfo*)data;

    if (!configInfo->selected)
        goto error;

    /* TODO Should this test be done or shall we just let the modem return error. */       
    if ((configInfo->toServiceId - configInfo->fromServiceId) > 10)
        goto error;
        
    asprintf(&cmd, "AT+CSCB=0,\"%d-%d\",\"%d-%d\"", configInfo->fromServiceId, configInfo->toServiceId, configInfo->fromCodeScheme, configInfo->toCodeScheme);

    err = at_send_command(cmd, &atresponse);

    if (err < 0 || atresponse->success == 0)
        goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

finally:
    at_response_free(atresponse);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}

/**
 * RIL_REQUEST_GSM_SMS_BROADCAST_ACTIVATION
 ok
 */
void requestGSMSMSBroadcastActivation(void *data, size_t datalen, RIL_Token t)
{
	ATResponse *atResponse = NULL;
	int mode, mt, bm, ds, bfr, skip;
	int activation;
	char *tok;
	char* cmd;
	int err;

	(void) datalen;

	/* AT+CNMI=[<mode>[,<mt>[,<bm>[,<ds>[,<bfr>]]]]] */
	err = at_send_command_singleline("AT+CNMI?", "+CNMI:", &atResponse);
    if (err < 0 || atResponse->success == 0)
        goto error;

	tok = atResponse->p_intermediates->line;

	err = at_tok_start(&tok);
	if (err < 0)
		goto error;
	err = at_tok_nextint(&tok, &mode);
	if (err < 0)
		goto error;
	err = at_tok_nextint(&tok, &mt);
	if (err < 0)
		goto error;
	err = at_tok_nextint(&tok, &skip);
	if (err < 0)
		goto error;
	err = at_tok_nextint(&tok, &ds);
	if (err < 0)
		goto error;
	err = at_tok_nextint(&tok, &bfr);
	if (err < 0)
		goto error;

	/* 0 - Activate, 1 - Turn off */
	activation = *((const int *)data);
	if (activation == 0)
		bm = 2;
	else
		bm = 0;

    asprintf(&cmd, "AT+CNMI=%d,%d,%d,%d,%d", mode, mt, bm, ds, bfr);
	at_response_free(atResponse);
	err = at_send_command(cmd,&atResponse);
	free(cmd);
    if (err < 0 || atResponse->success == 0)
        goto error;

	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

finally:
	at_response_free(atResponse);
	return;

error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	goto finally;


}

/**
 * RIL_REQUEST_SEND_SMS
 * 
 * Sends an SMS message.
 ok
*/
void requestSendSMS(void *data, size_t datalen, RIL_Token t)
{
  if(kRIL_HW_MC2716==rilhw->model||
  	 kRIL_HW_MC8630==rilhw->model)
  {  	
    requestCDMASendSMS(data, datalen, t);
	return;
  }
    int err;
    const char *smsc;
    const char *pdu;
    int tpLayerLength;
    char *cmd1, *cmd2;
    RIL_SMS_Response response;
    ATResponse *p_response = NULL;

    smsc = ((const char **)data)[0];
    pdu = ((const char **)data)[1];

    tpLayerLength = strlen(pdu)/2;

    // "NULL for default SMSC"
    if (smsc == NULL) {
        smsc= "00";
    }

    asprintf(&cmd1, "AT+CMGS=%d", tpLayerLength);
    asprintf(&cmd2, "%s%s", smsc, pdu);

    err = at_send_command_sms(cmd1, cmd2, "+CMGS:", &p_response);

    if (err != 0 || p_response->success == 0) goto error;

    memset(&response, 0, sizeof(response));

    /* FIXME fill in messageRef and ackPDU */

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(response));
    at_response_free(p_response);

    return;
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
}



/**
 * RIL_REQUEST_SEND_SMS_EXPECT_MORE
 * 
 * Send an SMS message. Identical to RIL_REQUEST_SEND_SMS,
 * except that more messages are expected to be sent soon. If possible,
 * keep SMS relay protocol link open (eg TS 27.005 AT+CMMS command).
 ok
*/
void requestSendSMSExpectMore(void *data, size_t datalen, RIL_Token t)
{
    /* Throw the command on the channel and ignore any errors, since we
       need to send the SMS anyway and subsequent SMSes will be sent anyway. */
    at_send_command("AT+CMMS=1", NULL);

    requestSendSMS(data, datalen, t);
}

/**
 * RIL_REQUEST_SMS_ACKNOWLEDGE
 *
 * Acknowledge successful or failed receipt of SMS previously indicated
 * via RIL_UNSOL_RESPONSE_NEW_SMS .
 ok
*/
void requestSMSAcknowledge(void *data, size_t datalen, RIL_Token t)
{
    int ackSuccess;

	if(kRIL_HW_MC8630==rilhw->model||kRIL_HW_MC2716==rilhw->model)
	{
		//supress CNMA for 8630 module because its bug
		at_send_command("AT+CNMA", NULL);
	}
	else
	{
	    ackSuccess = ((int *)data)[0];

	    if (ackSuccess == 1) {
	        at_send_command("AT+CNMA=1", NULL);
	    } else if (ackSuccess == 0)  {
	        at_send_command("AT+CNMA=2", NULL);
	    } else {
	        ERROR("unsupported arg to RIL_REQUEST_SMS_ACKNOWLEDGE\n");
	        goto error;
	    }
	}
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
    return;
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

}

/**
 * RIL_REQUEST_WRITE_SMS_TO_SIM
 *
 * Stores a SMS message to SIM memory.
 ok
*/
void requestWriteSmsToSim(void *data, size_t datalen, RIL_Token t)
{
    RIL_SMS_WriteArgs *p_args;
    char *cmd;
    int length;
    int err;
    ATResponse *p_response = NULL;

    p_args = (RIL_SMS_WriteArgs *)data;

    length = strlen(p_args->pdu)/2;
    asprintf(&cmd, "AT+CMGW=%d,%d", length, p_args->status);

    err = at_send_command_sms(cmd, p_args->pdu, "+CMGW:", &p_response);
	free(cmd);

    if (err != 0 || p_response->success == 0) goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
    at_response_free(p_response);

    return;
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
}




/**
 * RIL_REQUEST_DELETE_SMS_ON_SIM
 *
 * Deletes a SMS message from SIM memory.
 ok
*/
void requestDeleteSmsOnSim(void *data, size_t datalen, RIL_Token t)
{
       char * cmd;
	 int err;
       ATResponse * p_response = NULL;
       asprintf(&cmd, "AT+CMGD=%d", ((int *)data)[0]);
       err = at_send_command(cmd, &p_response);
       free(cmd);
       if (err < 0 || p_response->success == 0) {
                RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        } else {
                RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
        }
        at_response_free(p_response);
}

/**
 * RIL_REQUEST_GET_SMSC_ADDRESS
 ok
 */
void requestGetSMSCAddress(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *atresponse = NULL;
    int err;
    char *line;
    char *response;

    err = at_send_command_singleline("AT+CSCA?", "+CSCA:", &atresponse);

    if (err < 0 || atresponse->success == 0)
        goto error;

    line = atresponse->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    err = at_tok_nextstr(&line, &response);
    if (err < 0)
        goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(char *));

finally:
    at_response_free(atresponse);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}

/**
 * RIL_REQUEST_SET_SMSC_ADDRESS
 ok
 */
void requestSetSMSCAddress(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *atresponse = NULL;
    int err;
    char *cmd;
    const char *smsc = ((const char *)data);

    asprintf(&cmd, "AT+CSCA=\"%s\"", smsc);
    err = at_send_command(cmd, &atresponse);
    free(cmd);
    if (err < 0 || atresponse->success == 0)
        goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

finally:
    at_response_free(atresponse);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}


/**
 * RIL_REQUEST_CDMA_SEND_SMS
 *
 * Send a CDMA SMS message
 *
 * "data" is const RIL_CDMA_SMS_Message *
 *
 * "response" is a const RIL_SMS_Response *
 *
 * Based on the return error, caller decides to resend if sending sms
 * fails. The CDMA error class is derived as follows,
 * SUCCESS is error class 0 (no error)
 * SMS_SEND_FAIL_RETRY is error class 2 (temporary failure)
 * and GENERIC_FAILURE is error class 3 (permanent and no retry)
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  SMS_SEND_FAIL_RETRY
 *  GENERIC_FAILURE
 *
 */
void requestCDMASendSMS(void *data, size_t datalen, RIL_Token t)
{	
	int sms_encoding=ENCODING_ASCII;
	int s_smsPDULength;
    int err;
	RIL_CDMA_SMS_Message* cdma_sms;	
	RIL_SMS_Response response;
    ATResponse *atresponse = NULL;
	char* command;
	int i;

    cdma_sms = (RIL_CDMA_SMS_Message *)data;
	
	//dump_cdma_sms_msg(cdma_sms);
	/*
	  MC2716 
	  1.send sms AT command
		^HCMGS=<da>[,<toda>]<CR> 
		text is entered<ctrl-Z/ESC>> 
	  2.Only 4-bit mode support
	*/

	/*set sms parameters
	AT^HSMSSS = ack,prt,fmt,prv
	ack:
		0-> do not send sms report
		1-> send sms report
	prt:priority
		0->normal
		1->interactive
		2->urgent
		3->emergency
	fmt:encoding format
		0-> GSM 7 BIT 
		1->  ASCII
		2-> IA5
		3-> OCTET
		4-> LATIN
		5-> LATIN_HEBREW
		6->  UNICODE
		7-> Other
	prv:privacy
		0->normal
		1->restricted
		2->confidential
		3->secret
*/
	int fmt,message_length;
    char* addr;
    int addr_len;
    const char* smsc;
    const char* pdu;
    char* msg = NULL;
    
    smsc = ((const char **)data)[0];
    pdu = ((const char **)data)[1];
//    ril_dump_array("pdu", pdu, strlen(pdu));
    
    DBG("requestCDMASendSMS, decoding message...\n");
	msg = decode_gsm_sms_pdu(pdu, &addr, &addr_len, &fmt, &message_length);

    if (!msg)
    {
        ERROR("requestCDMASendSMS, Decode gsm msg failed\n");

        goto error;
    }

    ERROR("requestCDMASendSMS\n");
    ERROR("addr_len: %d\n", addr_len);
    ERROR("addr: %s\n", addr);
    ERROR("data_len: %d\n", message_length);
    ERROR("fmt: %d\n", fmt);

    if (fmt == 1)
        sms_encoding = 1;
    else
        sms_encoding = 6;
    s_smsPDULength = message_length;
	at_set_sms_param(sms_encoding,s_smsPDULength);
    asprintf(&command,"AT^HSMSSS=%d,%d,%d,%d",0,0,sms_encoding,0);
	at_send_command(command,NULL);
	free(command);
	
    asprintf(&command, "AT^HCMGS=\"%s\"", addr);

    err = at_send_command_sms(command, msg, "^HCMGS:", &atresponse);
	free(command);

    if (err != 0 || atresponse->success == 0) goto error;

    char* line = atresponse->p_intermediates->line;
	
    memset(&response, 0, sizeof(response));
	
    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line, &response.messageRef);
    if (err < 0)
        goto error;
	response.errorCode = -1;
    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(response));
    at_response_free(atresponse);

    return;
error:
    RIL_onRequestComplete(t, RIL_E_SMS_SEND_FAIL_RETRY, NULL, 0);
    at_response_free(atresponse);
}


void requestCDMASMSAcknowledge(void *data, size_t datalen, RIL_Token t)
{
	RIL_CDMA_SMS_Ack* ack=(RIL_CDMA_SMS_Ack*)data;	
	//just ignore errorcode since MT does not support
	at_send_command("AT+CNMA",NULL);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL,0);
}

void FindUnreadSms(void* param)
{
	//pdu mode
	at_send_command("AT+CMGF=0",NULL);
	/*query current sms stored in MT*/

	//check sms received but not read	
	at_send_command("AT+CMGL=0",NULL);//unsolicited sms message will be sent to handler
	
}
void CheckSMSStorage(void)
{
	const struct timespec TIMEVAL_SMS = { 1, 0 };

	enqueueRILEvent (FindUnreadSms, NULL, &TIMEVAL_SMS);
}

static void sm_sms_delete(void* param)
{
	int idx = (int)param;
	char* cmd;
	asprintf(&cmd,"AT+CMGD=%d",idx);
	at_send_command(cmd,NULL);
	free(cmd);
	
}
void sms_delete(int index)
{
	enqueueRILEvent (sm_sms_delete, (void*)index, NULL);
	
}

