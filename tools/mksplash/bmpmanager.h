#ifndef _BMP_MANAGER_H
#define _BMP_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif
typedef struct bmp_obj{
	char name[32];
	unsigned long start;
	unsigned long size;
}bmp_t;

//bmp manager utilize flash driver to fetch bmp data,part is flash partition name
int bmp_manager_init(const char* part);
int bmp_manager_reset();
void bmp_manager_dump(void);

int bmp_manager_getbmp(const char* name,bmp_t* bmp);

//read /write bmp data
int bmp_manager_readbmp(const char* name,void* data,unsigned long size);
int bmp_manager_writebmp(const char* name,void* data,unsigned long size);



struct bmpmngr_env {
    int (*erase_flash) (int fd,unsigned int offset,unsigned int bytes);
    int (*flash_to_file)(int fd,unsigned int offset,size_t len,const char *filename);
    int (*file_to_flash) (int fd,unsigned int offset,size_t len,const char *filename);
    int (*showinfo) (int fd);

    int (*bmp_manager_reset)(void);
    void (*bmp_manager_dump)(void);

    int (*bmp_manager_getbmp)(const char* name,bmp_t* bmp);

    //read /write bmp data
    int (*bmp_manager_readbmp)(const char* name,void* data,unsigned long size);
    int (*bmp_manager_writebmp)(const char* name,void* data,unsigned long size);
};


int bmp_manager_init_env(struct bmpmngr_env* env);

#ifndef LIBBMPMNGR_PATH
#define LIBBMPMNGR_PATH "/usr/lib/libbmpmngr.so"
#endif

#ifdef __cplusplus
}
#endif

#endif

