//
//cdma sms decode and encode routine
//
#include <stdio.h>
#include <string.h>
#include <telephony/ril.h>
#include "ril-debug.h"
#include "ril-handler.h"
#include "sms.h"

//#define SMS_VERBOSE_DEBUG 

static const char hextable[17]="0123456789ABCDEF";

static const char decode_table[17]=".1234567890*#...";


static char gsm_pdu[512];

	

//
//
//Below code only verified on MC2716 platform
//
//
//
//
//CDMA sms
//
#ifdef SMS_VERBOSE_DEBUG

static const char* sms_digitmode[] =
{
	"4bit",
	"8bit"
};
static const char* sms_numbermode[] =
{
	"non-data",
	"data"
};
static const char* sms_numbertype[] =
{
	"unkown",
	"international_or_data_ip",
	"national_or_international_mail",
	"network",
	"subscriber",
	"alphanumeric",
	"abbreviated",
	"reserved_7"
};
static const char* sms_numberplan[] =
{
	"unkown",
	"telephony",
	"reserved_2",
	"data",
	"telex",
	"reserved_5",
	"reserved_6",
	"reserved_7",
	"reserved_8",
	"private",
	"reserved_10",
	"reserved_11",
	"reserved_12",
	"reserved_13",
	"reserved_14",
	"reserved_15"	
};
static const char* sms_subaddresstype[] =
{
	"nsap",
	"user_specified"
};

static void dump_array(char* prefix,unsigned char* a,int length)
{
	int i;
	char* ptr;
	char digits[2048] = {0};
	
	ptr = &digits[0];
	for(i=0;i<length;i++)
		ptr+=sprintf(ptr,"%02x,",a[i]);
	DBG("%s array = [%s]",prefix,digits);


}




static void dump_cdma_sms_address(RIL_CDMA_SMS_Address* sms_addr)
{
	DBG("address digitmode[%s],numbermode[%s],numbertype[%s],numberplan[%s],digits number[%d]",
		(sms_addr->digit_mode<RIL_CDMA_SMS_DIGIT_MODE_MAX32)?sms_digitmode[sms_addr->digit_mode]:"null",
		(sms_addr->number_mode<RIL_CDMA_SMS_NUMBER_MODE_MAX32)?sms_numbermode[sms_addr->number_mode]:"null",
		(sms_addr->number_type<RIL_CDMA_SMS_NUMBER_TYPE_MAX32)?sms_numbertype[sms_addr->number_type]:"null",
		(sms_addr->number_plan<RIL_CDMA_SMS_NUMBER_PLAN_MAX32)?sms_numberplan[sms_addr->number_plan]:"null",
		sms_addr->number_of_digits);
	dump_array("digit",sms_addr->digits,sms_addr->number_of_digits);
	
}
static void dump_cdma_sms_subaddress(RIL_CDMA_SMS_Subaddress* sms_subaddr)
{
	DBG("subaddress type[%s],odd[%d],digits number[%d]",
		(sms_subaddr->subaddressType<RIL_CDMA_SMS_SUBADDRESS_TYPE_MAX32)?sms_subaddresstype[sms_subaddr->subaddressType]:"null",
		sms_subaddr->odd,
		sms_subaddr->number_of_digits);
	dump_array("digit",sms_subaddr->digits,sms_subaddr->number_of_digits);
	
}

void dump_cdma_sms_msg(const char* prefix,RIL_CDMA_SMS_Message* sms)
{
	if(prefix)
		DBG("%s \n",prefix);
	else
		DBG("CDMA sms\n");
	DBG("uTeleserviceID = %d\n",sms->uTeleserviceID);
	DBG("bIsServicePresent = %u\n",sms->bIsServicePresent);
	DBG("uServicecategory = %u\n",sms->uServicecategory);
	dump_cdma_sms_address(&sms->sAddress);
	dump_cdma_sms_subaddress(&sms->sSubAddress);
	DBG("beared data length[%d]",sms->uBearerDataLen);
	dump_array("bearerdata",sms->aBearerData,sms->uBearerDataLen);
}
#endif
static inline char encode_pdu_digit(char digit )
{
	int i=0;	
	for(i=0;i<16;i++)
		if(decode_table[i]==digit)
			return i;
	return 0;
}

static inline char decode_pdu_digit(char digit)
{
	return decode_table[(int)digit];
}

static inline void setvalue_of_bit(unsigned char* s,int b,int val)
{
	int byte=b/8;
	int bit=b%8;

	if(val)
		s[byte]|=(val<<(7-bit));
	else
		s[byte]&=~(val<<(7-bit));

}
static inline void setvalue_of_bits(unsigned char* s,int sbit,int nbits,int val)
{
	int i;

	for(i=0;i<nbits;i++)
		setvalue_of_bit(s,sbit+i,(val>>(nbits-i-1))&1);
	
}
static inline int getvalue_of_bit(unsigned char*s,int b)
{
	int byte=b/8;
	int bit=b%8;
	
	int data=s[byte];
	if(data&(1<<(7-bit))) return 1;
		else return 0;


}
static inline int getvalue_of_bits(unsigned char* s,int sbit,int nbits)
{
	int val=0;
	int i;

	for(i=0;i<nbits;i++)
	{
		val += (getvalue_of_bit(s,sbit+i)<<(nbits-i-1));
	}
	return val;
}

static char* _decode_cdma_sms_address(unsigned char* from,int length)
{
	static char address[40];	
	int j;

	for(j=0;j<length;j++) 
		address[j]=decode_pdu_digit(from[j]);
	if(length<=0)
	{
		strcpy(address,"000000");
		length = 6;
	}
	address[length]='\0';

	return address;
}

static char* _decode_cdma_sms_message(unsigned char* pdu,int length,int* fmt,int* ret_length)
{
	static char message[1024];
    int i=0,j;
    int code,sublength;
	unsigned char* ptr=pdu;
	int encoding,nchars;

	if(length<=0)
	{
		strcpy(message,"bad sms encoding");
		*fmt = ENCODING_ASCII;//ascii		
	}
	while(i<length) 
	{
		sublength = ptr[1];
		switch(ptr[0])
		{
			case 0://message identifier
				{
					DBG("message identifier");
				}
				break;
			case 1://user data
				{
					encoding=getvalue_of_bits(&ptr[2],0,5);
					nchars=getvalue_of_bits(&ptr[2],5,8);
					if((RIL_CDMA_SMS_ENCODING_ASCII!=encoding)&&//IS-91 Extended Protocol Message
						(RIL_CDMA_SMS_ENCODING_UNICODE!=encoding))
					{
						WARN("unsupported sms encoding");
						strcpy(message,"bad sms encoding");
						*fmt = ENCODING_ASCII;//ascii		
					}
					else 
					{
						if(RIL_CDMA_SMS_ENCODING_ASCII==encoding)
						{
							//7bit mode
							*fmt = ENCODING_ASCII;
							DBG("sms encoding == ASCII,chars=%d\n",nchars);	
							for(j=0;j<nchars;j++)
	            				message[j]=getvalue_of_bits(&ptr[3],5+7*j,7);
							message[nchars] = '\0';	
							*ret_length = nchars;
							
						}
						else 
						{
							//16bit mode
							*fmt = ENCODING_UNICODE;
							DBG("sms encoding == UNICODE,chars=%d\n",nchars);
							int k;
							for(j=0,k=0;j<nchars;j++)
	            			{
	            				message[k++]=getvalue_of_bits(&ptr[3],5+16*j,8);
								message[k++]=getvalue_of_bits(&ptr[3],5+16*j+8,8);
							}
							message[nchars*2] = '\0';
							*ret_length = nchars*2;
						}
						
					}
					
				}
				break;
			default:
				WARN("unsupport parameter id %d",ptr[0]);
				break;
		}
		
		ptr+=sublength+2;
		i+=sublength+2;
	   
	}	
	return message;
}

char* decode_cdma_sms_address(RIL_CDMA_SMS_Message* sms)
{
	return _decode_cdma_sms_address(sms->sAddress.digits,sms->sAddress.number_of_digits);

	
}
char* decode_cdma_sms_message(RIL_CDMA_SMS_Message* sms,int* fmt,int* length)
{
	
	#ifdef SMS_VERBOSE_DEBUG
	dump_cdma_sms_msg("=================OUT================",sms);
	#endif
	
	return _decode_cdma_sms_message(sms->aBearerData,sms->uBearerDataLen,fmt,length);
}


int encode_gsm_sms_pdu(const char* from, const char* extra, char** pdu)
{
	int err;
	int i;
	int bytes,remainder;
	char* line;	
	int year,month,day,hour,minute,second;
	int lang,format,msg_len,nchars;
	char *response;
    char* ptr = NULL;

    *pdu = gsm_pdu;

	bytes = 0;
	//init sms basic members
	line = (char*)from;
	at_tok_start(&line);
	//callerid
	err = at_tok_nextstr(&line, &response);if(err) goto fail;	
	//year;
	err = at_tok_nextint(&line, &year); if(err) goto fail;	
	//month
	err = at_tok_nextint(&line, &month); if(err) goto fail;	
	//day
	err = at_tok_nextint(&line, &day); if(err) goto fail;	
	//hour
	err = at_tok_nextint(&line, &hour); if(err) goto fail;	
	//minute
	err = at_tok_nextint(&line, &minute); if(err) goto fail;	
	//second
	err = at_tok_nextint(&line, &second); if(err) goto fail;	
	//lang
	err = at_tok_nextint(&line, &lang); if(err) goto fail;	
	//format,indicate encoding mode
	err = at_tok_nextint(&line, &format); if(err) goto fail;	
	//messaglength 
	err = at_tok_nextint(&line, &msg_len); if(err) goto fail;	
	//ignore more params of rest

    /* 
     * encode sms 'hellohello' from 13424448540 to
     * 00040b813124448445f00000000000000000000ae8329bfd4697d9ec37 
     */
    gsm_pdu[0] = 0;
    sprintf(&gsm_pdu[strlen(gsm_pdu)], "00"); /* SCA */
    sprintf(&gsm_pdu[strlen(gsm_pdu)], "04");
    sprintf(&gsm_pdu[strlen(gsm_pdu)], "%02x", strlen(response)); /* sender number length */
    sprintf(&gsm_pdu[strlen(gsm_pdu)], "81");
    for (i=0; i<(int)((strlen(response)+1)/2); i++)
    {
        if (response[i*2+1] != 0)
            sprintf(&gsm_pdu[strlen(gsm_pdu)], "%c", response[i*2+1]);
        else
            sprintf(&gsm_pdu[strlen(gsm_pdu)], "%c", 'f');
        sprintf(&gsm_pdu[strlen(gsm_pdu)], "%c", response[i*2+0]);
    }
    sprintf(&gsm_pdu[strlen(gsm_pdu)], "00");
    sprintf(&gsm_pdu[strlen(gsm_pdu)], "%02x", format==6?0x0b:0x00);
    sprintf(&gsm_pdu[strlen(gsm_pdu)], "00000000000000"); /* timestamp */
    sprintf(&gsm_pdu[strlen(gsm_pdu)], "%02x", msg_len);
    if (format == 6) /* unicode */
    {
        for (i=0; i<msg_len; i++)
            sprintf(&gsm_pdu[strlen(gsm_pdu)], "%02x", extra[i]);
    }
    else
    {
        int j;
        int s;
        unsigned char data[256];

        /* covert ascii code to gsm 7bit */
        for (i=0, j=0, s=7; i<msg_len; i++)
        {
            data[j++] = (extra[i+1]<<s)|(extra[i]>>(7-s));
            s--;
            if (s==0)
            {
                s = 7;
                i++;
            }
        }

        for (i=0; i<j; i++)
        {
            sprintf(&gsm_pdu[strlen(gsm_pdu)], "%02x", data[i]);
        }
    }

	return RIL_E_SUCCESS;
	
fail:
	ERROR("encode cdma sms failed\n");
	return err;
	
}

char* decode_gsm_sms_pdu(const char* data, char** addr, int* addr_len, int* fmt, int* length)
{
    int i;
    int v;
    const char* ptr = data;
    char* ret_ptr;
    static char address[20];
    static char msg[256];
    int has_uhd = 0; /* has user header */
    int uhd_skip = 0; /* user header field size */

    if (!data || !fmt || !length)
    {
        ERROR("Invalid param\n");
        return NULL;
    }

    /* 
     * decode pdu like:
     * [01000b813124448445f0000004f4f29c0e] test
     * [01000b813124448445f0000b044f60597d] ??o? (Chinese nihao)
     */

    /* TP-Message-Type-Indicator */
    sscanf(ptr, "%2x", &v);
    ptr += 2;
    if (v&0x40) has_uhd = 1;

    /* TP-MR (TP-Message Reference) */
    sscanf(ptr, "%2x", &v);
    ptr += 2;

    /* address lengh */
    sscanf(ptr, "%2x", &v);
    ptr += 2;
    *addr_len = v;

    /* skip area code */
    ptr += 2;

    /* address */
    for (i=0; i<(*addr_len+1)/2; i++)
    {
        address[i*2+0] = ptr[i*2+1];
        address[i*2+1] = ptr[i*2+0];
    }
    if (address[(i-1)*2+1] == 'f') address[(i-1)*2+1] = 0;
    else address[i*2+0] = 0;
    *addr = address;
    ptr += (*addr_len+1)/2*2;

    /* TP-Protocol-Identifier (TP-PID) */
    sscanf(ptr, "%2x", &v);
    ptr += 2;

    /* TP-Data-Coding-Scheme */
    sscanf(ptr, "%2x", &v);
    ptr += 2;
    *fmt = v;

    /* data len */
    sscanf(ptr, "%2x", &v);
    ptr += 2;
    *length = v;

    if (*fmt != 0) /* unicode */
    {
        for (i=0; i<*length; i++)
        {
            sscanf(&ptr[i*2], "%2x", (int*)&msg[i]);
        }

        if (has_uhd) uhd_skip = msg[0]+1;
    }
    else /* gsm 7bit */
    {
        int j;
        int s;
        unsigned char buf[256];

        for (i=0; i<*length; i++)
        {
            sscanf(&ptr[i*2], "%2x", (int*)&buf[i]);
        }

        for (i=0, j=0, v=0, s=1; j<*length; i++)
        {
            msg[j++] = ((buf[i]<<(s-1))&0x7f)|v;
            v = buf[i]>>(8-s);
            s++;
            if (s == 8) 
            {
                s = 1;
                msg[j++] = v;
                v = 0;
            }
        }

        *fmt = 1;

        if (has_uhd) 
        {
            uhd_skip = (msg[0]+1)*8/7;
            if ((msg[0]+1)*8%7) uhd_skip++;
        }
    }

    ret_ptr = msg+uhd_skip;
    *length -= uhd_skip;

    return ret_ptr;
}


int encode_cdma_sms(const char* from,const unsigned char* extra,RIL_CDMA_SMS_Message* sms)
{
	int err;
	int i;
	int bytes,remainder;
	char* line;	
	int year,month,day,hour,minute,second;
	int lang,format,messagelength,nchars;
	char *response;
	unsigned char* ptr;
	

	bytes = 0;
	//init sms basic members
	memset(sms,0,sizeof(RIL_CDMA_SMS_Message));
	sms->bIsServicePresent = 0;
	sms->uServicecategory = 0;
	sms->uTeleserviceID = 4098;//fixed number?
	sms->sAddress.digit_mode = RIL_CDMA_SMS_DIGIT_MODE_4_BIT;
	sms->sAddress.number_mode = RIL_CDMA_SMS_NUMBER_MODE_NOT_DATA_NETWORK;
	sms->sAddress.number_type = RIL_CDMA_SMS_NUMBER_TYPE_UNKNOWN;
	sms->sAddress.number_plan = RIL_CDMA_SMS_NUMBER_PLAN_UNKNOWN;
	
	sms->sSubAddress.subaddressType = RIL_CDMA_SMS_SUBADDRESS_TYPE_NSAP;
	sms->sSubAddress.odd = 0;
	sms->sSubAddress.number_of_digits = 0;
	line = (char*)from;
	at_tok_start(&line);
	//callerid
	err = at_tok_nextstr(&line, &response);if(err) goto fail;	
	sms->sAddress.number_of_digits = strlen(response);
	for(i=0;i<sms->sAddress.number_of_digits;i++)
	{
		sms->sAddress.digits[i] = encode_pdu_digit(response[i]);
	}
	//year;
	err = at_tok_nextint(&line, &year); if(err) goto fail;	
	//month
	err = at_tok_nextint(&line, &month); if(err) goto fail;	
	//day
	err = at_tok_nextint(&line, &day); if(err) goto fail;	
	//hour
	err = at_tok_nextint(&line, &hour); if(err) goto fail;	
	//minute
	err = at_tok_nextint(&line, &minute); if(err) goto fail;	
	//second
	err = at_tok_nextint(&line, &second); if(err) goto fail;	
	//lang
	err = at_tok_nextint(&line, &lang); if(err) goto fail;	
	//format,indicate encoding mode
	err = at_tok_nextint(&line, &format); if(err) goto fail;	
	//messaglength 
	err = at_tok_nextint(&line, &messagelength); if(err) goto fail;	
	//ignore more params of rest


	#ifdef SMS_VERBOSE_DEBUG
	DBG("sms arrived %d/%d/%d %d:%d:%d\n",year,month,day,hour,minute,second);

	dump_array("sms pdu",(unsigned char*)extra,messagelength);
	#endif

	//encoding message id
	ptr = &sms->aBearerData[0];
	ptr[0] = 0; //sub parameter id
	ptr[1] = 3; //length 
	ptr[2] = 1<<4;//type delivery 
	ptr[3] = 0;//ignore message identifier
	ptr[4] = 0x00;
	ptr += 5;
	sms->uBearerDataLen = 5;
	//user data
	ptr[0] = 1; //sub parameter id
	if(ENCODING_ASCII==format)
	{
		nchars = messagelength;
		DBG("encoding ascii,nchars[%d]\n",nchars);
		bytes 		= (messagelength*7+5)/8;
		remainder 	=  (messagelength*7+5)%8;
		if(remainder)
			bytes++;
		bytes++;//for num_fields
		ptr[1] = bytes;							//subparam_len
		ptr[2] = RIL_CDMA_SMS_ENCODING_ASCII<<3;//msg_encoding
		setvalue_of_bits(&ptr[2],5,8,nchars);//num_fields
		for(i=0;i<messagelength;i++)
		{
			setvalue_of_bits(&ptr[2],13+i*7,7,extra[i]);
		}
		
	}
	else if(ENCODING_UNICODE==format)
	{
		//16 bit mode
		bytes 		= messagelength+1/*for 5bit encoding reserved*/;
		nchars      = messagelength/2;
		DBG("encoding unicode,nchars[%d] \n",nchars);
		
		bytes++;//for num_fields
		ptr[1] = bytes;							//subparam_len
		ptr[2] = RIL_CDMA_SMS_ENCODING_UNICODE<<3;//msg_encoding
		setvalue_of_bits(&ptr[2],5,8,nchars);  //num_fields
		for(i=0;i<messagelength;i++)
		{
			setvalue_of_bits(&ptr[2],13+i*8,8,extra[i]);
		}
		
	}
	else if(ENCODING_IA5==format)
		ptr[2] = RIL_CDMA_SMS_ENCODING_IA5<<3;
	else if(ENCODING_OCTET==format)
		ptr[2] = RIL_CDMA_SMS_ENCODING_OCTET<<3;
	else if(ENCODING_LATIN==format)
		ptr[2] = RIL_CDMA_SMS_ENCODING_LATIN<<3;
	else if(ENCODING_LATIN_HEBREW==format)
		ptr[2] = RIL_CDMA_SMS_ENCODING_LATIN_HEBREW<<3;
	else
		ptr[2] = RIL_CDMA_SMS_ENCODING_GSM_7_BIT_DEFAULT<<3;
	
	sms->uBearerDataLen += bytes+2;
	ptr +=bytes+2; 
	
	//add deferred delivery time
	ptr[0] = 6;
	ptr[1] = 6;
	ptr[2] = year%100;//year,only 0-99
	ptr[3] = month;
	ptr[4] = day;
	ptr[5] = hour;
	ptr[6] = minute;
	ptr[7] = second;
	
	sms->uBearerDataLen += 8;
		
	DBG("encode cdma sms ok\n");
	#ifdef SMS_VERBOSE_DEBUG
	dump_cdma_sms_msg("================IN=================",sms);
	#endif
	return RIL_E_SUCCESS;
	
fail:
	ERROR("encode cdma sms failed\n");
	return err;
	
}

static char* sms_mem_mapping_name[]=
{
	"BM",
	"ME",
    "MT",
  	"SM",
	"TA",
	"SR"
};
void on_new_cdma_sms(void *param)
{
	PST_SMS_INDICATION smi = (PST_SMS_INDICATION)param;
	if(smi->sms_mem<SMS_MEM_MAX)
	{
		char* command;
		//should I change sms read status?
		//this will leave HCMGR as unsolicited message to ril core process.
		asprintf(&command,"AT^HCMGR=%d",smi->sms_index);		
		at_send_command(command,NULL);
		free(command);

		asprintf(&command,"AT+CMGD=%d",smi->sms_index);		
		at_send_command(command,NULL);
		free(command);
		
		
	}
	free(smi);

}

void on_new_gsm_sms(void *param)
{
	int location = (int)param;    
	char *cmd;   
	asprintf(&cmd, "AT+CMGR=%d,0", location);      //change sms  read status 
	/* request the sms in a specific location */       
	at_send_command(cmd, NULL);  
	free(cmd);        
	/* remove the sms from specific location XXX temp fix*/
	asprintf(&cmd, "AT+CMGD=%d,0", location);   
	at_send_command(cmd, NULL);    
	free(cmd);    
}


//GSM CMGR format
//+CMGR:<stat>,[<alpha>],<length><CR><LF><pdu>
int encode_gsm_mgr_pdu(const char* s,char* extra,char** pdu)
{
	int err=-1;
	char* line; 
	int stat,length;
	char* alpha;
	char *response;

	//init sms basic members
	line = (char*)s;
	at_tok_start(&line);
	//stat
	err = at_tok_nextint(&line, &stat); if(err) goto fail;	
	//alpha
	err = at_tok_nextstr(&line, &alpha);if(err) goto fail;	
	//length
	err = at_tok_nextint(&line, &length);if(err) goto fail;	

	DBG("CMGR msg stat[%d]alpha[%s]length[%d]\n",stat,alpha,length);

	if(extra)
	{
		*pdu = extra;
		err=0;
	}
	return err;

fail:
	ERROR("%s can't parse gsm mgr msg correctly\n",__func__);
	return err;
	

}

//GSM CMGL format
//+CMGL:<index>,<stat>,[<alpha>],<length><CR><LF><pdu>
int encode_gsm_mgl_pdu(const char* s,char* extra,char** pdu)
{
	int err=-1;
	char* line; 
	int index,stat,length;
	char* alpha;
	char *response;

	//init sms basic members
	line = (char*)s;
	at_tok_start(&line);
	//index
	err = at_tok_nextint(&line, &index); if(err) goto fail;		
	//stat
	err = at_tok_nextint(&line, &stat); if(err) goto fail;	
	//alpha
	err = at_tok_nextstr(&line, &alpha);if(err) goto fail;	
	//length
	err = at_tok_nextint(&line, &length);if(err) goto fail; 

	DBG("CMGL msg index[%d],stat[%d]alpha[%s]length[%d]\n",index,stat,alpha,length);

	if(extra)
	{
		*pdu = extra;
		err=0;
	}
	//delete current sms on sms
	sms_delete(index);
	
	return err;

fail:
	ERROR("%s can't parse gsm mgl msg correctly\n",__func__);
	return err;

}


