#ifndef _SMS_H_
#define _SMS_H_
#include <telephony/ril_cdma_sms.h>
#define ENCODING_GSM7BIT 			0 
#define ENCODING_ASCII 				1
#define ENCODING_IA5				2
#define ENCODING_OCTET				3
#define ENCODING_LATIN				4
#define ENCODING_LATIN_HEBREW		5
#define ENCODING_UNICODE			6
#define ENCODING_OTHER				7

#define SMS_MEM_BM 0
#define SMS_MEM_ME 1
#define SMS_MEM_MT 2
#define SMS_MEM_SM 3
#define SMS_MEM_TA 4
#define SMS_MEM_SR 5
#define SMS_MEM_MAX 6

typedef struct sms_indication
{
	int sms_mem;
	int sms_index;
}ST_SMS_INDICATION,*PST_SMS_INDICATION;

char* decode_cdma_sms_address(RIL_CDMA_SMS_Message* sms);
char* decode_cdma_sms_message(RIL_CDMA_SMS_Message* sms,int* fmt,int* length);

int encode_cdma_sms(const char* from,const unsigned char* extra,RIL_CDMA_SMS_Message* sms);

void dump_cdma_sms_msg(const char* prefix,RIL_CDMA_SMS_Message* sms);

/*param must be PST_SMS_INDICATION*/
void on_new_cdma_sms(void *param);

int encode_gsm_sms_pdu(const char* from, const char* extra, char** pdu);
char* decode_gsm_sms_pdu(const char* data, char** addr, int* addr_len, int* fmt, int* length);

int encode_gsm_mgr_pdu(const char* s,char* extra,char** pdu);
int encode_gsm_mgl_pdu(const char* s,char* extra,char** pdu);

void on_new_gsm_sms(void *param);
#endif
