#ifndef _DEBUG_H
#define _DEBUG_H

#ifndef LOG_TAG
#define LOG_TAG "RILH"
#endif
#include <utils/Log.h>

#define ENABLE_DEBUG


//debug flags
#define RIL_FLAG_DEBUG 		0x01
#define RIL_FLAG_ERROR		0x02
#define RIL_FLAG_WARN		0x04
#define RIL_FLAG_INFO  		0x08

extern unsigned int	s_flags;
#ifdef ENABLE_DEBUG
#define WARN(...) \
	ALOGI_IF(s_flags&RIL_FLAG_WARN,__VA_ARGS__) 
#define INFO(...) \
	ALOGI_IF(s_flags&RIL_FLAG_INFO,__VA_ARGS__) 
#define ERROR(...) \
	ALOGI_IF(s_flags&RIL_FLAG_ERROR,__VA_ARGS__) 
#define DBG(...) \
	ALOGI_IF(s_flags&RIL_FLAG_DEBUG,__VA_ARGS__) 
#define FUNC_ENTER() \
	ALOGI_IF(s_flags&RIL_FLAG_DEBUG,"enter %s",__FUNCTION__)
#define FUNC_LEAVE() \
	ALOGI_IF(s_flags&RIL_FLAG_DEBUG,"leave %s",__FUNCTION__)

#else
#define WARN(...) do{}while(0)
#define INFO(...) do{}while(0)
#define ERROR(...) do{}while(0)
#define DBG(...) do{}while(0)
#define FUNC_ENTER() do{}while(0)
#define FUNC_LEAVE() do{}while(0)
#endif


void ril_dump_array(char* prefix, const char* a,int length);

#endif
