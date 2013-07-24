#ifndef _MTD_H_
#define _MTD_H_
#include <sys/types.h>

int ustrtoul(const char *cp, char **endp, unsigned int base);


int erase_flash (int fd,u_int32_t offset,u_int32_t bytes);
int flash_to_file (int fd,u_int32_t offset,size_t len,const char *filename);
int file_to_flash (int fd,u_int32_t offset,u_int32_t len,const char *filename);
int showinfo (int fd);

int mtd_read (int fd,u_int32_t offset,void* data,size_t len);
int mtd_write (int fd,u_int32_t offset,void* data,size_t len);
int mtd_erase (int fd,u_int32_t offset,size_t len);


#endif 

