#include "ril-handler.h"

int stk_running = 0;
void requestSTKGetProfile(void *data, size_t datalen, RIL_Token t)
{
	int err = 0;
	int responselen = 0;
	ATResponse *p_response = NULL;
	char *response = NULL;
	char *line = NULL;

	err = at_send_command_singleline("AT+STKPROF?", "+STKPROF:", &p_response);

	if(err < 0 || p_response->success == 0) goto error;

	line = p_response->p_intermediates->line;
	err = at_tok_start(&line);
	if(err < 0) goto error;

	err = at_tok_nextint(&line, &responselen);
	if(err < 0) goto error;

	err = at_tok_nextstr(&line, &response);
	if(err < 0) goto error;

	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, response, responselen * sizeof(char));
	return;

error:
	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	
}

void requestSTKSetProfile(void *data, size_t datalen, RIL_Token t);
void requestSTKSendEnvelopeCommand(void *data, size_t datalen, RIL_Token t);
void requestSTKSendTerminalResponse(void *data, size_t datalen, RIL_Token t);


/**
 * RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING
 *
 * Indicates that the StkSerivce is running and is
 * ready to receive RIL_UNSOL_STK_XXXXX commands.
 *
 * "data" is NULL
 * "response" is NULL
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  GENERIC_FAILURE
 *
 */
void requestReportSTKServiceIsRunning(void *data, size_t datalen, RIL_Token t)
{
	stk_running = 1;
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL,0);
}

