#ifndef _INCLUDE_BOOT_FLASH_H_
#define _INCLUDE_BOOT_FLASH_H_

typedef struct ptentry ptentry;

//flags functions multi usage
//
//------------------------------------------------
//| reserved(16bit)|reserved(4bit)|fs(4bit)|partition(4bit)|type(4bit)|
//
#define FLASH_FLAGS_TYPE_MASK   0x0000000f
#define FLASH_FLAGS_NAND 0
#define FLASH_FLAGS_SPI  1
#define FLASH_FLAGS_MMC  2
#define FLASH_FLAGS_MTD  3
#define FLASH_FLAGS_FILE 4

#define FLASH_FLAGS_PART_MASK   0x000000f0
#define FLASH_PART(p) 		((p<<4)&FLASH_FLAGS_PART_MASK)

#define FLASH_FLAGS_FS_MASK 0x00000f00
#define FLASH_FLAGS_FS_RAW  (0<<8)
#define FLASH_FLAGS_FS_YAFFS (1<<8)


/* flash partitions are defined in terms of blocks
** (flash erase units)
*/
struct ptentry
{
    char name[32];
    unsigned start;
    unsigned length;
    unsigned flags;
};

/* tools to populate and query the partition table */
void flash_add_ptn(ptentry *ptn);
ptentry *flash_find_ptn(const char *name);
ptentry *flash_get_ptn(unsigned n);
unsigned flash_get_ptn_count(void);
void flash_dump_ptn(void);

//type can be FLASH_FLAGS_xxx
//name is for FLASH_FLAGS_FILE 
//
#define MTD_PROC_FILENAME   "/proc/mtd"

int flash_init(int type,const char* name);
int flash_erase(ptentry *ptn);
int flash_write(ptentry *ptn, unsigned long offset, const void *data, unsigned int bytes);

int flash_read(ptentry *ptn, unsigned long offset,const void *data, unsigned int bytes);


#endif

