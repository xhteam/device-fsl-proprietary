#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>  
#include <sys/stat.h>
#include "flashmanager.h"
#include "mtd.h"


#define alignment_down(a, size) ((a/size)*size)
#define alignment_up(a, size) (((a+size-1)/size)*size) 

#define dprintf printf

#define MAX_PTN 16

//dumy partition is only for transfer data 
#define DUMMY_PART_NAME "dummy"

static ptentry ptable[MAX_PTN];
static unsigned pcount = 0;

void flash_add_ptn(ptentry *ptn)
{
    if(pcount < MAX_PTN){
        memcpy(ptable + pcount, ptn, sizeof(*ptn));
        pcount++;
    }
}

void flash_dump_ptn(void)
{
    unsigned n;
    for(n = 0; n < pcount; n++) {
        ptentry *ptn = ptable + n;
        printf("ptn %d name='%s' start=0x%x len=0x%x\n",
                n, ptn->name, ptn->start, ptn->length);
    }
}


ptentry *flash_find_ptn(const char *name)
{
    unsigned n;
    for(n = 0; n < pcount; n++) {//ori is pcount
        if(!strcasecmp(ptable[n].name, name)) {
            return ptable + n;
        }
    }
    return 0;
}

ptentry *flash_get_ptn(unsigned n)
{
    if(n < pcount) {
        return ptable + n;
    } else {
        return 0;
    }
}

unsigned flash_get_ptn_count(void)
{
    return pcount;
}

//for MTD based operation,we delegate ops to mtd
int flash_erase(ptentry *ptn)
{
	int devtype = ptn->flags&FLASH_FLAGS_TYPE_MASK;
	int devpart = (ptn->flags&FLASH_FLAGS_PART_MASK)>>4;
	if(FLASH_FLAGS_MTD==devtype){
		char devname[64];
	    int fd;
		sprintf(devname,"/dev/mtd%d",devpart);
		fd = open(devname,O_SYNC|O_RDWR);
	    if(fd<0) return fd;
	    mtd_erase(fd,0,ptn->length);
	    close(fd);
	}
    return 0;    
}

int flash_write(ptentry *ptn, unsigned long offset, const void *data, unsigned int bytes)
{
	int devtype = ptn->flags&FLASH_FLAGS_TYPE_MASK;
	int devpart = (ptn->flags&FLASH_FLAGS_PART_MASK)>>4;
	if(FLASH_FLAGS_MTD==devtype){
		char* ptnbuf;
		char devname[64];
	    int fd;
		sprintf(devname,"/dev/mtd%d",devpart);
		fd = open(devname,O_SYNC|O_RDWR);
	    if(fd<0){
			//retry legacy devicename
			sprintf(devname,"/dev/mtd/mtd%d",devpart);
			fd = open(devname,O_SYNC|O_RDWR);
	    }
	    if(fd<0) return fd;

		//read 
		ptnbuf = malloc(ptn->length);
		if(!ptnbuf){
			close(fd);
			return -ENOMEM;
		}
		if(!mtd_read(fd,0,ptnbuf,ptn->length)){
			//update buffer
			memcpy(ptnbuf+offset,data,bytes);

			//erase whole partition
			if(!mtd_erase(fd,0,ptn->length)){
				//write back to partition
				mtd_write(fd,0,ptnbuf,ptn->length);
			}
		}
		free(ptnbuf);
	    close(fd);
	}else if(FLASH_FLAGS_FILE==devtype){
		char devname[64];
	    int fd;
		unsigned char *ptr;
		if((offset+bytes)>ptn->length){
			printf("out of space o[%d],l[%d],s[%d]\n",offset,bytes,ptn->length);
			return -1;
		}
		sprintf(devname,"%s",ptn->name);	
	    fd = open(devname,O_RDWR);
		if(fd<0) return fd;
		//using mmap to change content		
		ptr = mmap(0, ptn->length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		if (ptr == MAP_FAILED) {
			return -2;
		}
		memcpy(ptr+offset,data,bytes);
	    munmap((void *)ptr, ptn->length);
	    close(fd);
	}
	
    return 0;    

}

int flash_read(ptentry *ptn, unsigned long offset, const void *data, unsigned int bytes)
{ 
	int devtype = ptn->flags&FLASH_FLAGS_TYPE_MASK;
	int devpart = (ptn->flags&FLASH_FLAGS_PART_MASK)>>4;
	if(FLASH_FLAGS_MTD==devtype){
		char devname[64];
	    int fd;
		sprintf(devname,"/dev/mtd%d",devpart);	
	    fd = open(devname,O_SYNC|O_RDONLY);
	    if(fd<0) {
			//retry legacy devicename
			sprintf(devname,"/dev/mtd/mtd%d",devpart);
			fd = open(devname,O_SYNC|O_RDONLY);
		}
		if(fd<0) return fd;
	    mtd_read(fd,offset,data,bytes);
	    close(fd);
	}else if(FLASH_FLAGS_FILE==devtype){
		char devname[64];
	    int fd;
		sprintf(devname,"%s",ptn->name);	
	    fd = open(devname,O_RDWR);
		if(fd<0) return fd;
		lseek(fd,offset,SEEK_SET);
	    write(fd,data,bytes);
	    close(fd);
		
	}
    return 0;    

}


int flash_init(int type,const char* name)
{
	ptentry entry;

    int fd;
    int i;
    ssize_t nbytes;

	if(FLASH_FLAGS_MTD==type){
		char buf[2048];
		const char *bufp;
	    /* Open and read the file contents.
	     */
	    fd = open(name?name:MTD_PROC_FILENAME, O_RDONLY);
	    if (fd < 0) {
	        goto bail;
	    }
	    nbytes = read(fd, buf, sizeof(buf) - 1);
	    close(fd);
	    if (nbytes < 0) {
	        goto bail;
	    }
	    buf[nbytes] = '\0';

	    /* Parse the contents of the file, which looks like:
	     *
	     *     # cat /proc/mtd
	     *     dev:    size   erasesize  name
	     *     mtd0: 00080000 00020000 "bootloader"
	     *     mtd1: 00400000 00020000 "mfg_and_gsm"
	     *     mtd2: 00400000 00020000 "0000000c"
	     *     mtd3: 00200000 00020000 "0000000d"
	     *     mtd4: 04000000 00020000 "system"
	     *     mtd5: 03280000 00020000 "userdata"
	     */
	    bufp = buf;
	    while (nbytes > 0) {
	        int mtdnum, mtdsize, mtderasesize;
	        int matches;
	        char mtdname[64];
	        mtdname[0] = '\0';
	        mtdnum = -1;

	        matches = sscanf(bufp, "mtd%d: %x %x \"%63[^\"]",
	                &mtdnum, &mtdsize, &mtderasesize, mtdname);
	        /* This will fail on the first line, which just contains
	         * column headers.
	         */
	        if (matches == 4) {
				strcpy(entry.name,mtdname);
				entry.flags = FLASH_FLAGS_MTD|FLASH_PART(mtdnum);
				entry.start = 0;
				entry.length = mtdsize;
				flash_add_ptn(&entry);
	        }

	        /* Eat the line.
	         */
	        while (nbytes > 0 && *bufp != '\n') {
	            bufp++;
	            nbytes--;
	        }
	        if (nbytes > 0) {
	            bufp++;
	            nbytes--;
	        }
	    }
	}else if(FLASH_FLAGS_FILE==type){
		struct stat st;
		if(!name)
			goto bail;		
		strcpy(entry.name,name);
		stat(name,&st);
		entry.flags = FLASH_FLAGS_FILE;
		entry.start = 0;
		entry.length = st.st_size;
		flash_add_ptn(&entry);		
	}
	else
		goto bail;

	return 0;
bail:	
	return -1;
}

