#include <stdlib.h>
#include <stdio.h>
#include "bmpmanager.h"
#include "flashmanager.h"



#define BMPMANAGER_ALIGNSIZE (512)
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define alignment_up(value, align) roundup(value,align)
#define DEFAULT_BMPMNGR_PART "splash"

#define BMP_MNGR_MAGIC 0x626d6772 /*BMGR*/
#define BMP_MNGR_MAX_ELEMENTS 10
//store size is packed to BMPMANAGER_ALIGNSIZE bytes
typedef struct bmp_store{
	unsigned long magic;
	unsigned long count;
	bmp_t bmps[BMP_MNGR_MAX_ELEMENTS];//max 10 pictures is enough for us	
}bmp_store_t  __attribute__((aligned (BMPMANAGER_ALIGNSIZE)));
typedef struct bmpmanager_obj{
	unsigned char init;
	ptentry*   part;
	bmp_store_t store;
}bmpmanager_t;

static bmpmanager_t mngr={0};


void bmp_manager_dump(void){
	bmp_store_t* store = &mngr.store;
    int i;
    //dump all 
	printf("==============================\n");
	printf("found %u bmps\n",store->count);
	for(i=0;i<store->count;i++){
		printf("%s,start:0x%lx,size:0x%lx\n",store->bmps[i].name,
			store->bmps[i].start,
			store->bmps[i].size);
	}
	printf("==============================\n");

}

int bmp_manager_reset(){
    bmp_store_t* store = &mngr.store;
	
	ptentry* bmppart = mngr.part;
	if(!bmppart)	{
	    bmppart = flash_find_ptn(DEFAULT_BMPMNGR_PART);
	    mngr.part = bmppart;
    }
	if(!bmppart) {
    	return -1;
	}

    store->magic = BMP_MNGR_MAGIC;
    store->count = 0;
		

    //writeback 
    flash_write(bmppart,bmppart->length-alignment_up(sizeof(bmp_store_t),BMPMANAGER_ALIGNSIZE),
        store,alignment_up(sizeof(bmp_store_t),BMPMANAGER_ALIGNSIZE));

    return 0;
}
//bmp manager utilize flash driver to fetch bmp data,part is flash partition name
int bmp_manager_init(const char* part){
	bmp_store_t* store = &mngr.store;
	
	ptentry* bmppart = flash_find_ptn(part?part:DEFAULT_BMPMNGR_PART);
	if(!bmppart) {
    	return -1;
	}

	mngr.part = bmppart;
	//read from last location
	flash_read(bmppart,
		bmppart->length-alignment_up(sizeof(bmp_store_t),BMPMANAGER_ALIGNSIZE),
		(void*)store,alignment_up(sizeof(bmp_store_t),BMPMANAGER_ALIGNSIZE));
		
	if(store->magic!=BMP_MNGR_MAGIC){ 
		int ret;
		int bmpsize=0;
		char head[BMPMANAGER_ALIGNSIZE];
		//never inited
		store->magic = BMP_MNGR_MAGIC;
		store->count = 0;

		//check start already store one splash bmp
		
		ret = flash_read(bmppart,0,head,BMPMANAGER_ALIGNSIZE);
		if(!ret&&(head[0]==0x42)&&(head[1]==0x4d))
		{
			bmpsize=(head[5]<<24)|(head[4]<<16)|(head[3]<<8)|head[2];
			store->count++;
			store->bmps[0].start = 0;
			store->bmps[0].size  = alignment_up(bmpsize,BMPMANAGER_ALIGNSIZE);
			memset(store->bmps[0].name,0,32);
			strcpy(store->bmps[0].name,"splash");

		}
		//writeback 
		flash_write(bmppart,bmppart->length-alignment_up(sizeof(bmp_store_t),BMPMANAGER_ALIGNSIZE),
			store,alignment_up(sizeof(bmp_store_t),BMPMANAGER_ALIGNSIZE));
	}
	if(store->count>BMP_MNGR_MAX_ELEMENTS) store->count=10;

	
	mngr.init++;

	return 0;
}
int bmp_manager_getbmp(const char* name,bmp_t* bmp){
	bmp_store_t* store = &mngr.store;
	int i;	
	for(i=0;i<store->count&&i<BMP_MNGR_MAX_ELEMENTS;i++){
		if(!strcmp(store->bmps[i].name,name)){
			if(bmp)	memcpy(bmp,&store->bmps[i],sizeof(bmp_t));
			return 0;
		}
			
	}

	return -1;
	
}

int bmp_manager_readbmp(const char* name,void* data,unsigned long size){
	ptentry* entry;
	bmp_t bmp;
	int ret;
	if(!mngr.init)
		bmp_manager_init(0);
	entry = mngr.part;
	if(!entry||!name) return -1;
	ret = bmp_manager_getbmp(name,&bmp);
	if(ret<0){
	    if(!strcmp(name,"splash"))
	        ret = bmp_manager_getbmp("bmp.splash",&bmp);
	    if(ret<0){
	        return -1;
        }
    }
	return flash_read(entry,bmp.start,data,(size>bmp.size)?bmp.size:size);
}

int bmp_manager_writebmp(const char* name,void* data,unsigned long size){
	bmp_store_t* store = &mngr.store;
	ptentry* entry;
	bmp_t bmp;
	int i;
	if(!mngr.init)
		bmp_manager_init(0);	
	entry = mngr.part;		
	if(!entry||!name) return -1;;
	if(!bmp_manager_getbmp(name,&bmp)){
		//update ,some complicated but change to be simple
		//we used more higher memory space to store latter bmp data
		//0x1500000 normally as initrd memory
		int location=0;
		unsigned long used,space;
		used=0;
		for(i=0;i<store->count;i++){
			if(!strcmp(store->bmps[i].name,name)) break;
			used+=store->bmps[i].size;
		}
		location = i;
		if(store->count>(location+1)){
			char* movebuf;
			//read latter bmp to temp memory
			unsigned long movestart = store->bmps[location+1].start;
			unsigned long movesize=0;
			unsigned long mystart = bmp.start;
			unsigned long mysize = bmp.size;
			
			
			for(i=location+1;i<store->count;i++){
				movesize+=store->bmps[i].size;
			}
			//test space is enough
			space = entry->length-used-movesize-alignment_up(sizeof(bmp_store_t),BMPMANAGER_ALIGNSIZE);
			size = alignment_up(size,BMPMANAGER_ALIGNSIZE);
			if(space<size) return -1;

			
            movebuf = malloc(movesize);
            if(!movebuf){
                fprintf(stderr,"failed to allocate buffer for bmp loader\n");
				return -1;
            }
			//start to move
			//FIXME: hard code for higher memory 
			flash_read(entry,movestart,(void*)movebuf,movesize);

			//update store
			store->count--;
			memmove(&store->bmps[location],&store->bmps[location+1],(store->count-location)*sizeof(bmp_t));
			for(i=location;i<store->count;i++){
				store->bmps[i].start-=mysize;
			}

			//write data
			flash_write(entry,mystart,(void*)movebuf,movesize);
			flash_write(entry,mystart+movesize,data,size);

			//update store for me
			memset(store->bmps[store->count].name,0,32);		
			strncpy(store->bmps[store->count].name,name,31);
			store->bmps[store->count].start = movestart+movesize-mysize;
			store->bmps[store->count].size = size;
			store->count++;
			//update store
			flash_write(entry,entry->length-alignment_up(sizeof(bmp_store_t),BMPMANAGER_ALIGNSIZE),
				store,alignment_up(sizeof(bmp_store_t),BMPMANAGER_ALIGNSIZE));

			free(movebuf);
			
		}else{//the last one
			space=entry->length-used-alignment_up(sizeof(bmp_store_t),BMPMANAGER_ALIGNSIZE);
			size = alignment_up(size,BMPMANAGER_ALIGNSIZE);
			if(space<size) return -1;

			
			//write bmp data
			flash_write(entry,used,data,size);
			//update new size
			store->bmps[store->count-1].size = size;
			
			//writeback 
			flash_write(entry,entry->length-alignment_up(sizeof(bmp_store_t),BMPMANAGER_ALIGNSIZE),
				store,alignment_up(sizeof(bmp_store_t),BMPMANAGER_ALIGNSIZE));
			
		}
		
		
	}else {
		unsigned long used,space;
		int i;
		if(store->count>=BMP_MNGR_MAX_ELEMENTS) return -1;
		//calcute left space
		for(i=0,used=0;i<store->count;i++){
			used+=store->bmps[i].size;
		}
		space = entry->length-used-alignment_up(sizeof(bmp_store_t),BMPMANAGER_ALIGNSIZE);
		size = alignment_up(size,BMPMANAGER_ALIGNSIZE);
		if(space<size) return -1;//no enough space

		//write bmp data
		flash_write(entry,used,data,size);
		//update manager
		memset(store->bmps[store->count].name,0,32);		
		strncpy(store->bmps[store->count].name,name,31);
		store->bmps[store->count].start = used;
		store->bmps[store->count].size = size;
		store->count++;

		//writeback 
		flash_write(entry,entry->length-alignment_up(sizeof(bmp_store_t),BMPMANAGER_ALIGNSIZE),
			store,alignment_up(sizeof(bmp_store_t),BMPMANAGER_ALIGNSIZE));
		
		
	}

	return 0;
}


#include "mtd.h"
#include "flashmanager.h"

int bmp_manager_init_env(struct bmpmngr_env* env){   
    int ret = flash_init(FLASH_FLAGS_MTD,NULL);       
    ret |= bmp_manager_init(0);

    if(ret) return ret;

    if(env){
        env->erase_flash = erase_flash;        
        env->flash_to_file = flash_to_file;
        env->file_to_flash = file_to_flash;
        env->showinfo      = showinfo;

        env->bmp_manager_reset = bmp_manager_reset;
        env->bmp_manager_dump = bmp_manager_dump;
        env->bmp_manager_getbmp = bmp_manager_getbmp;
        env->bmp_manager_readbmp = bmp_manager_readbmp;
        env->bmp_manager_writebmp= bmp_manager_writebmp;       
    }

    return 0;
}



