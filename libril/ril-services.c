#include <stdio.h>
#include <assert.h>
#include "ril-handler.h"

/**
 * RIL_REQUEST_QUERY_CLIP
 *
 * Queries the status of the CLIP supplementary service
 *
 * (for MMI code "*#30#")
 *
 * "data" is NULL
 * "response" is an int *
 * (int *)response)[0] is 1 for "CLIP provisioned"
 *                           and 0 for "CLIP not provisioned"
 *                           and 2 for "unknown, e.g. no network etc"
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE (radio resetting)
 *  GENERIC_FAILURE
 */
void requestQueryClip(void *data, size_t datalen, RIL_Token t)
{
	ATResponse *p_response = NULL;
	int err;
	char *line;
	int response;
	err = at_send_command_singleline("AT+CLIP?","+CLIP:",&p_response);
	if(err < 0 || p_response->success == 0) goto error;

	line = p_response->p_intermediates->line;
	err = at_tok_start(&line);
	if(err < 0) goto error;

	/* The first number is discarded */
	err = at_tok_nextint(&line, &response);
	if(err < 0) goto error;

	err = at_tok_nextint(&line, &response);
	if(err < 0) goto error;

	RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));
	at_response_free(p_response);
	return;

error:
	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

/**
 * RIL_REQUEST_CANCEL_USSD
 * 
 * Cancel the current USSD session if one exists.
 */
void requestCancelUSSD(void *data, size_t datalen, RIL_Token t)
{
	ATResponse *p_response;
    	int err;
	p_response = NULL;
      err = at_send_command_numeric("AT+CUSD=2", &p_response);
      if (err < 0 || p_response->success == 0) {
             RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
      } else {
                RIL_onRequestComplete(t, RIL_E_SUCCESS,
                    p_response->p_intermediates->line, sizeof(char *));
      }
      at_response_free(p_response);
}

/**
 * RIL_REQUEST_SEND_USSD
 *
 * Send a USSD message
 *
 * If a USSD session already exists, the message should be sent in the
 * context of that session. Otherwise, a new session should be created.
 *
 * The network reply should be reported via RIL_UNSOL_ON_USSD
 *
 * Only one USSD session may exist at a time, and the session is assumed
 * to exist until:
 *   a) The android system invokes RIL_REQUEST_CANCEL_USSD
 *   b) The implementation sends a RIL_UNSOL_ON_USSD with a type code
 *      of "0" (USSD-Notify/no further action) or "2" (session terminated)
 *
 * "data" is a const char * containing the USSD request in UTF-8 format
 * "response" is NULL
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  FDN_CHECK_FAILURE
 *  GENERIC_FAILURE
 *
 * See also: RIL_REQUEST_CANCEL_USSD, RIL_UNSOL_ON_USSD
 */
void  requestSendUSSD(void *data, size_t datalen, RIL_Token t)
{
    const char *ussdRequest;

    ussdRequest = (char *)(data);


    RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);

// @@@ TODO

}


/**
 * RIL_UNSOL_SUPP_SVC_NOTIFICATION
 *
 * Reports supplementary service related notification from the network.
 */
void onSuppServiceNotification(const char *s, int type)
{
    RIL_SuppSvcNotification ssnResponse;
    char *line;
    char *tok;
    int err;

    line = tok = strdup(s);

    memset(&ssnResponse, 0, sizeof(ssnResponse));
    ssnResponse.notificationType = type;

    err = at_tok_start(&tok);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&tok, &ssnResponse.code);
    if (err < 0)
        goto error;

    if (ssnResponse.code == 16 || 
        (type == 0 && ssnResponse.code == 4) ||
        (type == 1 && ssnResponse.code == 1)) {
        err = at_tok_nextint(&tok, &ssnResponse.index);
        if (err < 0)
            goto error;
    }

    /* RIL_SuppSvcNotification has two more members that we won't
       get from the +CSSI/+CSSU. Where do we get them, if we ever do? */

    RIL_onUnsolicitedResponse(RIL_UNSOL_SUPP_SVC_NOTIFICATION,
                              &ssnResponse, sizeof(ssnResponse));

error:
    free(line);
}

/**
 * RIL_UNSOL_ON_USSD
 *
 * Called when a new USSD message is received.
 */
void onUSSDReceived(const char *s)
{
    char **response;
    char *line;
    int err = -1;
    int i = 0;
    int n = 0;

    line = alloca(strlen(s) + 1);
    strcpy(line, s);
    line[strlen(s)] = 0;

    response = alloca(2 * sizeof(char *));
    response[0] = NULL;

    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line, &i);
    if (err < 0)
        goto error;

    if (i < 0 || i > 5)
        goto error;

    response[0] = alloca(2);
    sprintf(response[0], "%d", i);

    n = 1;

    if (i < 2) {
        n = 2;

        err = at_tok_nextstr(&line, &response[1]);
        if (err < 0)
            goto error;
    }

    /* TODO: We ignore the <dcs> parameter, might need this some day. */

    RIL_onUnsolicitedResponse(RIL_UNSOL_ON_USSD, response,
                              n * sizeof(char *));

error:
    return;
}

/**
 * RIL_REQUEST_GET_CLIR
 *
 * Gets current CLIR status
 * "data" is NULL
 * "response" is int *
 * ((int *)data)[0] is "n" parameter from TS 27.007 7.7
 * ((int *)data)[1] is "m" parameter from TS 27.007 7.7
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  GENERIC_FAILURE
 */
void requestGetCLIR(void *data, size_t datalen, RIL_Token t)
{
   /* Even though data and datalen must be NULL and 0 respectively this
	* condition is not verified
	*/
   ATResponse *p_response = NULL;
   int response[2];
   char *line = NULL;
   int err = 0;

   err = at_send_command_singleline("AT+CLIR?", "+CLIR:", &p_response);

   if (err < 0 || p_response->success == 0) goto error;

   line = p_response->p_intermediates->line;
   err = at_tok_start(&line);
   if (err < 0) goto error;

   err = at_tok_nextint(&line, &(response[0]));
   if (err < 0) goto error;

   err = at_tok_nextint(&line, &(response[1]));
   if (err < 0) goto error;

   RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(response));
   at_response_free(p_response);
   return;

error:
   RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
   at_response_free(p_response);
   
}

/**
 * RIL_REQUEST_GET_CLIR
 *
 * Gets current CLIR status
 * "data" is NULL
 * "response" is int *
 * ((int *)data)[0] is "n" parameter from TS 27.007 7.7
 * ((int *)data)[1] is "m" parameter from TS 27.007 7.7
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  GENERIC_FAILURE
 */
void requestSetCLIR(void *data, size_t datalen, RIL_Token t)
{
	char *cmd = NULL;
	int err = 0;

	asprintf(&cmd, "AT+CLIR=%d", ((int *)data)[0]);

	err = at_send_command(cmd, NULL);
	free(cmd);

	if (err < 0)
		RIL_onRequestComplete(t, RIL_E_PASSWORD_INCORRECT, NULL, 0);
	else
		RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

/**
 * RIL_REQUEST_QUERY_CALL_FORWARD_STATUS
 *
 * "data" is const RIL_CallForwardInfo *
 *
 * "response" is const RIL_CallForwardInfo **
 * "response" points to an array of RIL_CallForwardInfo *'s, one for
 * each distinct registered phone number.
 *
 * For example, if data is forwarded to +18005551212 and voice is forwarded
 * to +18005559999, then two separate RIL_CallForwardInfo's should be returned
 *
 * If, however, both data and voice are forwarded to +18005551212, then
 * a single RIL_CallForwardInfo can be returned with the service class
 * set to "data + voice = 3")
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  GENERIC_FAILURE
 */
void requestQueryCallForwardStatus(void *data, size_t datalen, RIL_Token t)
{
	int err = 0;
	int i = 0;
	int n = 0;
	int tmp = 0;
	ATResponse *p_response = NULL;
	ATLine *p_cur;
	RIL_CallForwardInfo **responses = NULL;
	char *cmd;

	err = at_send_command_multiline("AT+CCFC=0,2", "+CCFC:", &p_response);

	if(err != 0 || p_response->success == 0)
		goto error;

	for (p_cur = p_response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next)
		n++;

	responses = alloca(n * sizeof(RIL_CallForwardInfo *));

	for(i = 0; i < n; i++) {
		responses[i] = alloca(sizeof(RIL_CallForwardInfo));
		responses[i]->status = 0;
		responses[i]->reason = 0;
		responses[i]->serviceClass = 0;
		responses[i]->toa = 0;
		responses[i]->number = "";
		responses[i]->timeSeconds = 0;
	}

	for (i = 0,p_cur = p_response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next, i++) {
		char *line = p_cur->line;

		err = at_tok_start(&line);
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &(responses[i]->status));
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &(responses[i]->serviceClass));
		if (err < 0) goto error;

		if(!at_tok_hasmore(&line)) continue;

		err = at_tok_nextstr(&line, &(responses[i]->number));
		if (err < 0) goto error;

		if(!at_tok_hasmore(&line)) continue;

		err = at_tok_nextint(&line, &(responses[i]->toa));
		if (err < 0) goto error;

		if(!at_tok_hasmore(&line)) continue;
		at_tok_nextint(&line,&tmp);

		if(!at_tok_hasmore(&line)) continue;
		at_tok_nextint(&line,&tmp);

		if(!at_tok_hasmore(&line)) continue;
		err = at_tok_nextint(&line, &(responses[i]->timeSeconds));
		if (err < 0) goto error;

	}

	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, responses, sizeof(RIL_CallForwardInfo **));
	return;

error:
	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

/**
 * RIL_REQUEST_SET_CALL_FORWARD
 *
 * Configure call forward rule
 *
 * "data" is const RIL_CallForwardInfo *
 * "response" is NULL
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  GENERIC_FAILURE
 */
void requestSetCallForward(void *data, size_t datalen, RIL_Token t)
{
	int err = 0;
	char *cmd = NULL;
	RIL_CallForwardInfo *info = NULL;

	info = ((RIL_CallForwardInfo *) data);

	if(data == NULL) goto error;

	asprintf(&cmd, "AT+CCFC = %d, %d, \"%s\"",
			info->reason, info->status,
			info->number);

	err = at_send_command(cmd, NULL);

	if (err < 0 ) goto error;

	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
	free(cmd);
	return;

error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	free(cmd);
}

/**
 * RIL_REQUEST_QUERY_CALL_WAITING
 *
 * Query current call waiting state
 *
 * "data" is const int *
 * ((const int *)data)[0] is the TS 27.007 service class to query.
 * "response" is a const int *
 * ((const int *)response)[0] is 0 for "disabled" and 1 for "enabled"
 *
 * If ((const int *)response)[0] is = 1, then ((const int *)response)[1]
 * must follow, with the TS 27.007 service class bit vector of services
 * for which call waiting is enabled.
 *
 * For example, if ((const int *)response)[0]  is 1 and
 * ((const int *)response)[1] is 3, then call waiting is enabled for data
 * and voice and disabled for everything else
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  GENERIC_FAILURE
 */
void requestQueryCallWaiting(void *data, size_t datalen, RIL_Token t)
{
	ATResponse *p_response = NULL;
	int err;
	char *cmd = NULL;
	char *line;
	int class;
	int response[2];

	class = ((int *)data)[0];

	asprintf(&cmd, "AT+CCWA=1,2,%d",class);

	err = at_send_command_singleline(cmd, "+CCWA:", &p_response);

	if (err < 0 || p_response->success == 0) goto error;

	line = p_response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &(response[0]));
	if (err < 0) goto error;

	if (at_tok_hasmore(&line)) {
		err = at_tok_nextint(&line, &(response[1]));
		if (err < 0) goto error;
	}

	RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(response));
	at_response_free(p_response);
	free(cmd);
	return;

error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	at_response_free(p_response);
	free(cmd);
}

/**
 * RIL_REQUEST_SET_CALL_WAITING
 *
 * Configure current call waiting state
 *
 * "data" is const int *
 * ((const int *)data)[0] is 0 for "disabled" and 1 for "enabled"
 * ((const int *)data)[1] is the TS 27.007 service class bit vector of
 *                           services to modify
 * "response" is NULL
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  GENERIC_FAILURE
 */
void requestSetCallWaiting(void *data, size_t datalen, RIL_Token t)
{
	ATResponse *p_response = NULL;
	int err;
	char *cmd = NULL;
	int enabled, class;

	if((datalen<2)||(data==NULL)) goto error;

	enabled = ((int *)data)[0];
	class = ((int *)data)[1];

	asprintf(&cmd, "AT+CCWA=0,%d,%d",enabled,class);

	err = at_send_command(cmd, NULL);

	if (err < 0 ) goto error;

	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
	at_response_free(p_response);
	free(cmd);
	return;

error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	at_response_free(p_response);
	free(cmd);
}

/**
 * RIL_REQUEST_SET_SUPP_SVC_NOTIFICATION
 *
 * Enables/disables supplementary service related notifications
 * from the network.
 *
 * Notifications are reported via RIL_UNSOL_SUPP_SVC_NOTIFICATION.
 *
 * "data" is int *
 * ((int *)data)[0] is == 1 for notifications enabled
 * ((int *)data)[0] is == 0 for notifications disabled
 *
 * "response" is NULL
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  GENERIC_FAILURE
 *
 * See also: RIL_UNSOL_SUPP_SVC_NOTIFICATION.
 */
void requestSetSuppSvcNotification(void *data, size_t datalen, RIL_Token t)
{
    int err;
    int ssn = ((int *) data)[0];
    char *cmd;


    asprintf(&cmd, "AT+CSSN=%d,%d", ssn, ssn);

    err = at_send_command(cmd, NULL);
    free(cmd);
    if (err < 0)
        goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

finally:
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}
