#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENABLE_DEBUG
//debug flags
#define RIL_FLAG_DEBUG 		0x01
#define RIL_FLAG_ERROR		0x02
#define RIL_FLAG_WARN		0x04
#define RIL_FLAG_INFO  		0x08

static unsigned int	s_flags = RIL_FLAG_DEBUG|RIL_FLAG_ERROR|RIL_FLAG_WARN;
#ifdef ENABLE_DEBUG
#define WARN(...) \
	do{if(s_flags&RIL_FLAG_WARN) printf(__VA_ARGS__);}while(0) 
#define INFO(...) \
	do{if(s_flags&RIL_FLAG_INFO) printf(__VA_ARGS__);}while(0) 
#define ERROR(...) \
	do{if(s_flags&RIL_FLAG_ERROR) printf(__VA_ARGS__);}while(0) 
#define DBG(...) \
	do{if(s_flags&RIL_FLAG_DEBUG) printf(__VA_ARGS__);}while(0) 

#else
#define WARN(...) do{}while(0)
#define INFO(...) do{}while(0)
#define ERROR(...) do{}while(0)
#define DBG(...) do{}while(0)
#define FUNC_ENTER() do{}while(0)
#define FUNC_LEAVE() do{}while(0)
#endif

#include "at_tok.c"
typedef enum {
    RIL_E_SUCCESS = 0,
    RIL_E_RADIO_NOT_AVAILABLE = 1,     /* If radio did not start or is resetting */
    RIL_E_GENERIC_FAILURE = 2,
    RIL_E_PASSWORD_INCORRECT = 3,      /* for PIN/PIN2 methods only! */
    RIL_E_SIM_PIN2 = 4,                /* Operation requires SIM PIN2 to be entered */
    RIL_E_SIM_PUK2 = 5,                /* Operation requires SIM PIN2 to be entered */
    RIL_E_REQUEST_NOT_SUPPORTED = 6,
    RIL_E_CANCELLED = 7,
    RIL_E_OP_NOT_ALLOWED_DURING_VOICE_CALL = 8, /* data ops are not allowed during voice
                                                   call on a Class C GPRS device */
    RIL_E_OP_NOT_ALLOWED_BEFORE_REG_TO_NW = 9,  /* data ops are not allowed before device
                                                   registers in network */
    RIL_E_SMS_SEND_FAIL_RETRY = 10,             /* fail to send sms and need retry */
    RIL_E_SIM_ABSENT = 11,                      /* fail to set the location where CDMA subscription
                                                   shall be retrieved because of SIM or RUIM
                                                   card absent */
    RIL_E_SUBSCRIPTION_NOT_AVAILABLE = 12,      /* fail to find CDMA subscription from specified
                                                   location */
    RIL_E_MODE_NOT_SUPPORTED = 13,              /* HW does not support preferred network type */
    RIL_E_FDN_CHECK_FAILURE = 14,               /* command failed because recipient is not on FDN list */
    RIL_E_ILLEGAL_SIM_OR_ME = 15                /* network selection failed due to
                                                   illegal SIM or ME */
} RIL_Errno;


/* Used by RIL_REQUEST_CDMA_SEND_SMS and RIL_UNSOL_RESPONSE_CDMA_NEW_SMS */

#define RIL_CDMA_SMS_ADDRESS_MAX     36
#define RIL_CDMA_SMS_SUBADDRESS_MAX  36
#define RIL_CDMA_SMS_BEARER_DATA_MAX 255

typedef enum {
    RIL_CDMA_SMS_DIGIT_MODE_4_BIT = 0,     /* DTMF digits */
    RIL_CDMA_SMS_DIGIT_MODE_8_BIT = 1,
    RIL_CDMA_SMS_DIGIT_MODE_MAX32 = 0x10000000 /* Force constant ENUM size in structures */
} RIL_CDMA_SMS_DigitMode;

typedef enum {
    RIL_CDMA_SMS_NUMBER_MODE_NOT_DATA_NETWORK = 0,
    RIL_CDMA_SMS_NUMBER_MODE_DATA_NETWORK     = 1,
    RIL_CDMA_SMS_NUMBER_MODE_MAX32 = 0x10000000 /* Force constant ENUM size in structures */
} RIL_CDMA_SMS_NumberMode;

typedef enum {
    RIL_CDMA_SMS_NUMBER_TYPE_UNKNOWN                   = 0,
    RIL_CDMA_SMS_NUMBER_TYPE_INTERNATIONAL_OR_DATA_IP  = 1,
      /* INTERNATIONAL is used when number mode is not data network address.
       * DATA_IP is used when the number mode is data network address
       */
    RIL_CDMA_SMS_NUMBER_TYPE_NATIONAL_OR_INTERNET_MAIL = 2,
      /* NATIONAL is used when the number mode is not data network address.
       * INTERNET_MAIL is used when the number mode is data network address.
       * For INTERNET_MAIL, in the address data "digits", each byte contains
       * an ASCII character. Examples are "x@y.com,a@b.com - ref TIA/EIA-637A 3.4.3.3
       */
    RIL_CDMA_SMS_NUMBER_TYPE_NETWORK                   = 3,
    RIL_CDMA_SMS_NUMBER_TYPE_SUBSCRIBER                = 4,
    RIL_CDMA_SMS_NUMBER_TYPE_ALPHANUMERIC              = 5,
      /* GSM SMS: address value is GSM 7-bit chars */
    RIL_CDMA_SMS_NUMBER_TYPE_ABBREVIATED               = 6,
    RIL_CDMA_SMS_NUMBER_TYPE_RESERVED_7                = 7,
    RIL_CDMA_SMS_NUMBER_TYPE_MAX32 = 0x10000000 /* Force constant ENUM size in structures */
} RIL_CDMA_SMS_NumberType;

typedef enum {
    RIL_CDMA_SMS_NUMBER_PLAN_UNKNOWN     = 0,
    RIL_CDMA_SMS_NUMBER_PLAN_TELEPHONY   = 1,      /* CCITT E.164 and E.163, including ISDN plan */
    RIL_CDMA_SMS_NUMBER_PLAN_RESERVED_2  = 2,
    RIL_CDMA_SMS_NUMBER_PLAN_DATA        = 3,      /* CCITT X.121 */
    RIL_CDMA_SMS_NUMBER_PLAN_TELEX       = 4,      /* CCITT F.69 */
    RIL_CDMA_SMS_NUMBER_PLAN_RESERVED_5  = 5,
    RIL_CDMA_SMS_NUMBER_PLAN_RESERVED_6  = 6,
    RIL_CDMA_SMS_NUMBER_PLAN_RESERVED_7  = 7,
    RIL_CDMA_SMS_NUMBER_PLAN_RESERVED_8  = 8,
    RIL_CDMA_SMS_NUMBER_PLAN_PRIVATE     = 9,
    RIL_CDMA_SMS_NUMBER_PLAN_RESERVED_10 = 10,
    RIL_CDMA_SMS_NUMBER_PLAN_RESERVED_11 = 11,
    RIL_CDMA_SMS_NUMBER_PLAN_RESERVED_12 = 12,
    RIL_CDMA_SMS_NUMBER_PLAN_RESERVED_13 = 13,
    RIL_CDMA_SMS_NUMBER_PLAN_RESERVED_14 = 14,
    RIL_CDMA_SMS_NUMBER_PLAN_RESERVED_15 = 15,
    RIL_CDMA_SMS_NUMBER_PLAN_MAX32 = 0x10000000 /* Force constant ENUM size in structures */
} RIL_CDMA_SMS_NumberPlan;

typedef struct {
    RIL_CDMA_SMS_DigitMode digit_mode;
      /* Indicates 4-bit or 8-bit */
    RIL_CDMA_SMS_NumberMode number_mode;
      /* Used only when digitMode is 8-bit */
    RIL_CDMA_SMS_NumberType number_type;
      /* Used only when digitMode is 8-bit.
       * To specify an international address, use the following:
       * digitMode = RIL_CDMA_SMS_DIGIT_MODE_8_BIT
       * numberMode = RIL_CDMA_SMS_NOT_DATA_NETWORK
       * numberType = RIL_CDMA_SMS_NUMBER_TYPE_INTERNATIONAL_OR_DATA_IP
       * numberPlan = RIL_CDMA_SMS_NUMBER_PLAN_TELEPHONY
       * numberOfDigits = number of digits
       * digits = ASCII digits, e.g. '1', '2', '3'3, '4', and '5'
       */
    RIL_CDMA_SMS_NumberPlan number_plan;
      /* Used only when digitMode is 8-bit */
    unsigned char number_of_digits;
    unsigned char digits[ RIL_CDMA_SMS_ADDRESS_MAX ];
      /* Each byte in this array represnts a 40bit or 8-bit digit of address data */
} RIL_CDMA_SMS_Address;

typedef enum {
    RIL_CDMA_SMS_SUBADDRESS_TYPE_NSAP           = 0,    /* CCITT X.213 or ISO 8348 AD2 */
    RIL_CDMA_SMS_SUBADDRESS_TYPE_USER_SPECIFIED = 1,    /* e.g. X.25 */
    RIL_CDMA_SMS_SUBADDRESS_TYPE_MAX32 = 0x10000000 /* Force constant ENUM size in structures */
} RIL_CDMA_SMS_SubaddressType;

typedef struct {
    RIL_CDMA_SMS_SubaddressType subaddressType;
    /* 1 means the last byte's lower 4 bits should be ignored */
    unsigned char odd;
    unsigned char number_of_digits;
    /* Each byte respresents a 8-bit digit of subaddress data */
    unsigned char digits[ RIL_CDMA_SMS_SUBADDRESS_MAX ];
} RIL_CDMA_SMS_Subaddress;

typedef struct {
    int uTeleserviceID;
    unsigned char bIsServicePresent;
    int uServicecategory;
    RIL_CDMA_SMS_Address sAddress;
    RIL_CDMA_SMS_Subaddress sSubAddress;
    int uBearerDataLen;
    unsigned char aBearerData[ RIL_CDMA_SMS_BEARER_DATA_MAX ];
} RIL_CDMA_SMS_Message;

/* ----------------------------- */
/* -- User data encoding type -- */
/* ----------------------------- */
typedef enum {
    RIL_CDMA_SMS_ENCODING_OCTET        = 0,    /* 8-bit */
    RIL_CDMA_SMS_ENCODING_IS91EP,              /* varies */
    RIL_CDMA_SMS_ENCODING_ASCII,               /* 7-bit */
    RIL_CDMA_SMS_ENCODING_IA5,                 /* 7-bit */
    RIL_CDMA_SMS_ENCODING_UNICODE,             /* 16-bit */
    RIL_CDMA_SMS_ENCODING_SHIFT_JIS,           /* 8 or 16-bit */
    RIL_CDMA_SMS_ENCODING_KOREAN,              /* 8 or 16-bit */
    RIL_CDMA_SMS_ENCODING_LATIN_HEBREW,        /* 8-bit */
    RIL_CDMA_SMS_ENCODING_LATIN,               /* 8-bit */
    RIL_CDMA_SMS_ENCODING_GSM_7_BIT_DEFAULT,   /* 7-bit */
    RIL_CDMA_SMS_ENCODING_MAX32        = 0x10000000

} RIL_CDMA_SMS_UserDataEncoding;


#define ENCODING_GSM7BIT 			0 
#define ENCODING_ASCII 				1
#define ENCODING_IA5				2
#define ENCODING_OCTET				3
#define ENCODING_LATIN				4
#define ENCODING_LATIN_HEBREW		5
#define ENCODING_UNICODE			6
#define ENCODING_OTHER				7

static const char hextable[17]="0123456789ABCDEF";

static const char decode_table[17]=".1234567890*#...";

//
//
//Below code only verified on MC2716 platform
//
//
//
//
//CDMA sms
//
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



static void dump_cdma_sms_address(RIL_CDMA_SMS_Address* sms_addr)
{
	char digits[128],*ptr;
	DBG("address digitmode[%s],numbermode[%s],numbertype[%s],numberplan[%s],digits number[%d]\n",
		(sms_addr->digit_mode<RIL_CDMA_SMS_DIGIT_MODE_MAX32)?sms_digitmode[sms_addr->digit_mode]:"null",
		(sms_addr->number_mode<RIL_CDMA_SMS_NUMBER_MODE_MAX32)?sms_numbermode[sms_addr->number_mode]:"null",
		(sms_addr->number_type<RIL_CDMA_SMS_NUMBER_TYPE_MAX32)?sms_numbertype[sms_addr->number_type]:"null",
		(sms_addr->number_plan<RIL_CDMA_SMS_NUMBER_PLAN_MAX32)?sms_numberplan[sms_addr->number_plan]:"null",
		sms_addr->number_of_digits);
	int i;
	ptr = &digits[0];
	for(i=0;i<sms_addr->number_of_digits;i++)
		ptr+=sprintf(ptr,"%02x,",sms_addr->digits[i]);
	DBG("digits[%s]\n",digits);
	
}
static void dump_cdma_sms_subaddress(RIL_CDMA_SMS_Subaddress* sms_subaddr)
{
	char digits[128],*ptr;
	DBG("subaddress type[%s],odd[%d],digits number[%d]\n",
		(sms_subaddr->subaddressType<RIL_CDMA_SMS_SUBADDRESS_TYPE_MAX32)?sms_subaddresstype[sms_subaddr->subaddressType]:"null",
		sms_subaddr->odd,
		sms_subaddr->number_of_digits);
	int i;
	ptr = &digits[0];
	for(i=0;i<sms_subaddr->number_of_digits;i++)
		ptr+=sprintf(ptr,"%02x,",sms_subaddr->digits[i]);
	DBG("digits[%s]\n",digits);
	
}

void dump_cdma_sms_msg(RIL_CDMA_SMS_Message* sms)
{
	char digits[512],*ptr;
	DBG("CDMA sms\n");
	DBG("uTeleserviceID = %d\n",sms->uTeleserviceID);
	DBG("bIsServicePresent = %u\n",sms->bIsServicePresent);
	DBG("uServicecategory = %u\n",sms->uServicecategory);
	dump_cdma_sms_address(&sms->sAddress);
	dump_cdma_sms_subaddress(&sms->sSubAddress);
	DBG("beared data length[%d]\n",sms->uBearerDataLen);
	int i;
	ptr = &digits[0];
	for(i=0;i<sms->uBearerDataLen;i++)
		ptr+=sprintf(ptr,"%02x,",sms->aBearerData[i]);
	DBG("beared[%s]\n",digits);
}

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
char* decode_cdma_sms_address(char* from,int length)
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

static inline void setvalue_of_bit(char* s,int b,int val)
{
	int byte=b/8;
	int bit=b%8;

	if(val)
		s[byte]|=(val<<(7-bit));
	else
		s[byte]&=~(val<<(7-bit));

}
static inline void setvalue_of_bits(char* s,int sbit,int nbits,int val)
{
	int i;

	for(i=0;i<nbits;i++)
		setvalue_of_bit(s,sbit+i,(val>>(nbits-i-1))&1);
	
}
static inline int getvalue_of_bit(char*s,int b)
{
	int byte=b/8;
	int bit=b%8;
	
	int data=s[byte];
	if(data&(1<<(7-bit))) return 1;
		else return 0;


}
static inline int getvalue_of_bits(char* s,int sbit,int nbits)
{
	int val=0;
	int i;

	for(i=0;i<nbits;i++)
	{
		val += (getvalue_of_bit(s,sbit+i)<<(nbits-i-1));
	}
	return val;
}
char* decode_cdma_sms_message(char* pdu,int length,int* fmt)
{
	static char message[1024];
    int i=0,j;
    int code,sublength;
	char* ptr=pdu;
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
int encode_cdma_sms(const char* from,const char* extra,RIL_CDMA_SMS_Message* sms)
{
	int err;
	int i;
	int bytes,remainder;
	char* line;	
	int year,month,day,hour,minute,second;
	int lang,format,messagelength,nchars;
	char *response;
	char* ptr;
	char digits[128];
	

	bytes = 0;
	//init sms basic members
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
	DBG("sms arrived %d/%d/%d %d:%d:%d\n",year,month,day,hour,minute,second);
	//reset bearer data
	memset(sms->aBearerData,RIL_CDMA_SMS_BEARER_DATA_MAX,0);

	ptr = &digits[0];
	for(i=0;i<messagelength;i++)
		ptr+=sprintf(ptr,"%02x,",extra[i]);
	DBG("sms extra[%s]",digits);


	//TODO: add timestamp to android
	//encoding message id
	ptr = (char*)&sms->aBearerData[0];
	ptr[0] = 0; //sub parameter id
	ptr[1] = 3; //length 
	ptr[2] = 1<<4;//type delivery 
	ptr[3] = 0;//ignore message identifier
	ptr[4] = 0x00;
	ptr += 5;
	sms->uBearerDataLen = 5;
	//user data
	ptr[0] = 1; //sub parameter id
	DBG("encoding mode is %d from MT\n",format);
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
	
		
	DBG("encode cdma sms ok\n");
	dump_cdma_sms_msg(sms);
	return RIL_E_SUCCESS;
	
fail:
	DBG("encode cdma sms failed\n");
	return err;
	
}

static RIL_CDMA_SMS_Message sms;
int main(int argc,char** argv)
{
	char sms_header[128];
	int messagelength;
	char digits[1024],*ptr;		
	unsigned char * sms_message = "I love this world.";
	if(argc>1)
		sms_message = argv[1];
  messagelength = strlen(sms_message);
    //header pattern
		//13538065179,2011,1,9,12,24,51,0,1,18,0,0,0,3
		sprintf(sms_header,"^HCMT:%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
		"13538065179",
		2011,1,9,12,24,51,
		0,
		(sms_message[0]>0x7f)?ENCODING_UNICODE:ENCODING_ASCII,
		messagelength,
		0,0,0,3);
		
		encode_cdma_sms(sms_header,sms_message,&sms);
		
		return 0;
	  
		
		
		

}

