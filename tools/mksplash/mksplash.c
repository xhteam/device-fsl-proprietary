
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include "flashmanager.h"
#include "bmpmanager.h"

typedef struct bmp_source {
	struct bmp_source* next;
	char* source;
}BMP_SOURCE,*PBMP_SOURCE;

typedef struct splash_img{
	char* image;
	int size;
	PBMP_SOURCE src;
	int verbose;
}SPLASH_IMG,*PSPLASH_IMG;

static void usage(const char* progname);

static unsigned long simple_strtoul(const char *cp,char **endp,unsigned int base)
{
	unsigned long result = 0,value;

	if (*cp == '0') {
		cp++;
		if ((*cp == 'x') && isxdigit(cp[1])) {
			base = 16;
			cp++;
		}
		if (!base) {
			base = 8;
		}
	}
	if (!base) {
		base = 10;
	}
	while (isxdigit(*cp) && (value = isdigit(*cp) ? *cp-'0' : (islower(*cp)
	    ? toupper(*cp) : *cp)-'A'+10) < base) {
		result = result*base + value;
		cp++;
	}
	if (endp)
		*endp = (char *)cp;
	return result;
}

#if 0
static long simple_strtol(const char *cp,char **endp,unsigned int base)
{
	if(*cp=='-')
		return -simple_strtoul(cp+1,endp,base);
	return simple_strtoul(cp,endp,base);
}
#endif
static int ustrtoul(const char *cp, char **endp, unsigned int base)
{
	unsigned long result = simple_strtoul(cp, endp, base);
	switch (**endp) {
	case 'G' :
		result *= 1024;
		/* fall through */
	case 'M':
		result *= 1024;
		/* fall through */
	case 'K':
	case 'k':
		result *= 1024;
		if ((*endp)[1] == 'i') {
			if ((*endp)[2] == 'B')
				(*endp) += 3;
			else
				(*endp) += 2;
		}
	}
	return result;
}

int set_splash_size (PSPLASH_IMG splash, char *size){
	char* e;
	splash->size = ustrtoul (size,&e,0);
	return 0;
}
int set_splash_image (PSPLASH_IMG splash, char *image){
	/* Check for existence */
	if(!access(image, F_OK  ))
	{	
	   struct stat sbuf;
	   if(stat(image,&sbuf) < 0) {
	   	printf("%s exist but access failed\n",image);
		return -1;
   	   }
	   if(splash->verbose){
		   printf("%s already exist,overlay to filesize[%d]\n",image,sbuf.st_size);
		}
	   splash->size = sbuf.st_size;
	   /* Check for write permission */
	   if( (access( image, W_OK ))< 0 ){
		   printf("%s exist but without write permission\n",image);
		   return -2;
   		}
	}else {
		//target not exist,create it now?
		char* buf = malloc(splash->size);
		int fd = open(image,O_CREAT|O_RDWR,S_IRWXU);
		if(fd<0){
			printf("failed to create %s\n",image);
			return -3;
		}
		if(!buf){
			printf("out of memory\n");
			close(fd);
			return -4;
		}
		printf("splash image size=%d\n",splash->size);
		memset(buf,0,splash->size);
		write(fd,buf,splash->size);
		close(fd);
		free(buf);
	}
	splash->image = image;
	return 0;
}
int set_splash_source (PSPLASH_IMG splash, char *source){	
	if(!access(source, F_OK  )){
		PBMP_SOURCE bmp = malloc(sizeof(BMP_SOURCE));
		if(!bmp){
			printf("out of memory\n");
			return -1;
		}
		bmp->next = NULL;
		bmp->source = source;
		if(!splash->src){
			splash->src = bmp;
		}else {
			//iterate to last one
			PBMP_SOURCE iter = splash->src;
			while(iter->next) iter = iter->next;
			iter->next = bmp;
		}
	}else {
		printf("can't access bmp[%s]\n",source);
	}
		
	return 0;
}

int splash_init(PSPLASH_IMG splash){
	memset(splash,0,sizeof(*splash));
	splash->size = 2*1024*1024;

	return 0;
}
int splash_dispose(PSPLASH_IMG splash){
	PBMP_SOURCE iter,next;
	if(splash->src){
		iter = splash->src;
		do {
			next = iter->next;
			free(iter);
			iter = next;
		}while(iter);
	}

	return 0;
	
}
int splash_sanity_check(PSPLASH_IMG splash){
	
	
	if(!splash->image||
		(splash->size<0)||
		(!splash->src)){
		if(splash->verbose)
			printf("sanity check failed\n");
		return -1;
	}
	
	PBMP_SOURCE iter,next;
	printf("\n\nSPLASH IMAGE INFO:\n");
	printf("image:%s\n",splash->image?splash->image:"null");
	printf("size:%d\n",splash->size);
	printf("sources:\n");
	
	iter = splash->src;
	do {
		next = iter->next;
		printf("%s\n",iter->source?iter->source:"null");
		iter = next;
	}while(iter);
	printf("\n\n");
	return 0;
}
int splash_update(PSPLASH_IMG splash){
	PBMP_SOURCE iter,next;
	int fd;
	struct stat sbuf;
	char* buf;
	char bmpname[32];
	iter = splash->src;
	do {
		next = iter->next;
		printf("prepare to add %s\n",iter->source);
		
		if(stat(iter->source,&sbuf) < 0) {
		 printf("access %s failed,ignore...\n",iter->source);
		}else {
			buf = malloc(sbuf.st_size);
			if(!buf) {
				printf("out of memory\n");
			}else {
				fd = open(iter->source,O_RDONLY);
				if(fd>=0){
					char* filename;
					char* token;
					read(fd,buf,sbuf.st_size);					
					(filename = strrchr (iter->source,'/')) ? filename++ : (filename = iter->source);
					strncpy(bmpname,filename,31);
					//truncate ext filename					
					token = strrchr (bmpname,'.');
					if(token)
						*token = '\0';

					bmp_manager_writebmp(bmpname,buf,sbuf.st_size);
					close(fd);						
				}
				free(buf);
			}
		}
		iter = next;
	}while(iter);
	return 0;
}
int
main (int argc, char **argv)
{
	SPLASH_IMG splash;
	splash_init(&splash);	
	const char *progname;
	(progname = strrchr (argv[0],'/')) ? progname++ : (progname = argv[0]);
	while (--argc > 0 && **++argv == '-') {
		while (*++*argv) {
			switch (**argv) {
			case 's':
				if ((--argc <= 0) ||
					(set_splash_size (&splash,*++argv)) < 0)
					usage (progname);
				goto NXTARG;
			case 'o':
				if ((--argc <= 0) ||
					(set_splash_image(&splash,*++argv)) < 0)
					usage (progname);
				goto NXTARG;
			case 'b':
				if ((--argc <= 0) ||
					(set_splash_source(&splash,*++argv)) < 0)
					usage (progname);
				goto NXTARG;
			case 'v':
				splash.verbose++;
				break;
			default:
				usage (progname);goto bail;
			}
		}
NXTARG:		;
	}

	if(splash_sanity_check(&splash)){
		usage(progname);
		goto bail; 
	}
	printf("start to making splash\n");
	//init flashmanager
	flash_init(FLASH_FLAGS_FILE,splash.image);
	//init bmpmanager
	bmp_manager_init(splash.image);

	splash_update(&splash);

	bmp_manager_dump();
	splash_dispose(&splash);
	exit (EXIT_SUCCESS);
bail:
	splash_dispose(&splash);
	exit(-2);
}



void
usage (const char* progname)
{
	fprintf (stderr, "Usage: %s [-s size] -o image -b bmpfile\n"
			 "          -s ==> set target image size (default is 2MB)\n"
			 "          -o ==> set target image file\n"
			 "          -b ==> set image source bmp file,can be multi instances\n",
		progname);

}

