#include <stdio.h>
#include <string.h>
#include "itu_network.h"
//TODO
//for better performance,the mcc table quiry algorithm should be changed.
typedef struct mcc_table mcc_table;
typedef struct mnc_table mnc_table;
typedef struct itu_network itu_network;
struct mcc_table
{
	const char* mcc;
	short mnc_length;		
	short mnc_number;
	struct mnc_table* mnc_list;	
	struct mcc_table* next;
};
struct mnc_table
{
	const char* mnc;
	char* short_alpha;
	char* long_alpha;
	char* numeric;
};

struct itu_network
{
	int init;
	mcc_table* mcc;
};

static itu_network network={.init=0};

static mnc_table argentina_mcc[] =
{
	{"010","Moviles","Radiocomunicaciones Moviles","722010"},
	{"020","Nextel","Nextel Argentina srl","722020"},
	{"070","Telefonica","Telefonica","722070"},
	{"310","CTI","CTI PCS S.A.","722310"},
	{"320","Interior Norte","Interior Norte","722320"},		
	{"330","Interior","Interior","722330"},	
	{"341","Telecom Personal","Telecom Personal","722341"}
};
static mcc_table argentina = 
{
	"722",//mcc
	3,//mnc length
	7,//number of operators
	argentina_mcc,
	0,//link anchor
};

/*
	China
*/
static mnc_table china_mcc[] =
{
	{"00","CMCC","China Mobile","46000"},
	{"01","CHUNM","China Unicom","46001"},
	{"02","CMCC","China Mobile","46002"},
	{"03","CHT","China Telecom","46003"},
	{"05","CHT","China Telecom","46005"},
	{"06","CHUNM","China Unicom","46006"},	
	{"07","CMCC","China Mobile","46007"}
};
static mcc_table china = 
{
	"460",//mcc
	2,//mnc length
	7,//number of operators
	china_mcc,
	0,//link anchor
};


/*
	indonisa
*/
static mnc_table indonisa_mcc[] =
{
	{"00","Telkom Flexi","Telkom Flexi","51000"},
	{"01","Satelindo","Satelindo","51001"},
	{"08","Natrindo","Lippo Telecom","51008"},
	{"10","Telkomsel","Telkomsel","51010"},
	{"11","Excelcomindo","Excelcomindo","51011"},	
	{"21","Indosat - M3","Indosat - M3","51021"},	
	{"28","Komselindo","Komselindo","51028"}	
};
static mcc_table indonisa = 
{
	"510",//mcc
	2,//mnc length
	7,//number of operators
	indonisa_mcc,
	0,//link anchor

};


/*
	malaysia
*/
static mnc_table malaysia_mcc[] =
{
	{"10","DIGI","DIGI Telecommunications","50210"},
	{"12","Malaysian Mobile Services","Malaysian Mobile Services","50212"},
	{"13","Celcom","Celcom (Malaysia) Berhad","50213"},
	{"14","Telekom","Telekom Malaysia Berhad","50214"},
	{"16","DIGI","DIGI Telecommunications","50216"},	
	{"17","Malaysian Mobile Services","Malaysian Mobile Services","50217"},	
	{"18","U Mobile","U Mobile","50218"},
	{"19","CelCom","CelCom (Malaysia) Berhad","50219"},	
	{"20","Electcoms","Electcoms Wireless Sdn Bhd","50220"}	
};
static mcc_table malaysia = 
{
	"502",//mcc
	2,//mnc length
	9,//number of operators
	malaysia_mcc,
	0,//link anchor

};


static void itu_network_init(void)
{
	network.mcc = &argentina;
	argentina.next = &china;
	china.next = &indonisa;
	indonisa.next = &malaysia;
	
	network.init = 1;
}
int network_query_operator(const char* imsi,itu_operator* opr)
{
	mcc_table* mcc;
	mnc_table* mnc;
	int mnc_number;
	if(!network.init)
	{
		itu_network_init();
	}
	if(!imsi||!opr) return -1;
		
	mcc = network.mcc;
	while(mcc)
	{
		if(!strncmp(imsi,mcc->mcc,3))
		{
			mnc_number = 0;
			mnc = mcc->mnc_list;
			while(mnc_number<mcc->mnc_number)
			{
				if(!strncmp(imsi+3,mnc->mnc,mcc->mnc_length))
				{
					//found it					
					opr->short_alpha = mnc->short_alpha;
					opr->long_alpha = mnc->long_alpha;
					opr->numeric = mnc->numeric;
					return 0;					
				}
				mnc_number++;mnc++;
			}
			
		}
		mcc = mcc->next;
	}
	
	return -2;
	
	
}


