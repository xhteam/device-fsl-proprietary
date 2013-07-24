/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>  // for size_t, etc.
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "mtd.h"




#ifdef SUPPORT_MTD_PARTITION
#include <mtd/mtd-user.h>

/*
 * MEMGETINFO
 */
static int getmeminfo (int fd,struct mtd_info_user *mtd)
{
	return (ioctl (fd,MEMGETINFO,mtd));
}

/*
 * MEMERASE
 */
static int memerase (int fd,struct erase_info_user *erase)
{
	return (ioctl (fd,MEMERASE,erase));
}

/*
 * MEMGETREGIONCOUNT
 * MEMGETREGIONINFO
 */
static int getregions (int fd,struct region_info_user *regions,int *n)
{
	int i,err;
	err = ioctl (fd,MEMGETREGIONCOUNT,n);
	if (err) return (err);
	for (i = 0; i < *n; i++)
	{
		regions[i].regionindex = i;
		err = ioctl (fd,MEMGETREGIONINFO,&regions[i]);
		if (err) return (err);
	}
	return (0);
}
#endif

int erase_flash (int fd,u_int32_t offset,u_int32_t bytes)
{
	
#ifdef SUPPORT_MTD_PARTITION
	int err;
	struct erase_info_user erase;
	erase.start = offset;
	erase.length = bytes;
	err = memerase (fd,&erase);
	if (err < 0)
	{
		perror ("MEMERASE");
		return (1);
	}
//	fprintf (stderr,"Erased %d bytes from address 0x%.8x in flash\n",bytes,offset);
#endif
	return (0);
}

static void printsize (u_int32_t x)
{
	int i;
	static const char *flags = "KMGT";
	printf ("%u ",x);
	for (i = 0; x >= 1024 && flags[i] != '\0'; i++) x /= 1024;
	i--;
	if (i >= 0) printf ("(%u%c)",x,flags[i]);
}

int flash_to_file (int fd,u_int32_t offset,size_t len,const char *filename)
{
	u_int8_t *buf = NULL;
	int outfd,err;
	int size = len * sizeof (u_int8_t);
	int n = len;

	if (offset != lseek (fd,offset,SEEK_SET))
	{
		perror ("lseek()");
		goto err0;
	}
	outfd = creat (filename,O_WRONLY);
	if (outfd < 0)
	{
		perror ("creat()");
		goto err1;
	}

retry:
	if ((buf = (u_int8_t *) malloc (size)) == NULL)
	{
#define BUF_SIZE	(64 * 1024 * sizeof (u_int8_t))
		fprintf (stderr, "%s: malloc(%#x)\n", __FUNCTION__, size);
		if (size != BUF_SIZE) {
			size = BUF_SIZE;
			fprintf (stderr, "%s: trying buffer size %#x\n", __FUNCTION__, size);
			goto retry;
		}
		perror ("malloc()");
		goto err0;
	}
	do {
		if (n <= size)
			size = n;
		err = read (fd,buf,size);
		if (err < 0)
		{
			fprintf (stderr, "%s: read, size %#x, n %#x\n", __FUNCTION__, size, n);
			perror ("read()");
			goto err2;
		}
		err = write (outfd,buf,size);
		if (err < 0)
		{
			fprintf (stderr, "%s: write, size %#x, n %#x\n", __FUNCTION__, size, n);
			perror ("write()");
			goto err2;
		}
		if (err != size)
		{
			fprintf (stderr,"Couldn't copy entire buffer to %s. (%d/%d bytes copied)\n",filename,err,size);
			goto err2;
		}
		n -= size;
	} while (n > 0);

	if (buf != NULL)
		free (buf);
	close (outfd);
//	printf ("Copied %d bytes from address 0x%.8x in flash to %s\n",len,offset,filename);
	return (0);

err2:
	close (outfd);
err1:
	if (buf != NULL)
		free (buf);
err0:
	return (1);
}

int file_to_flash (int fd,u_int32_t offset,u_int32_t len,const char *filename)
{
	u_int8_t *buf = NULL;
	FILE *fp;
	int err;
	int size = len * sizeof (u_int8_t);
	int n = len;

	if (offset != lseek (fd,offset,SEEK_SET))
	{
		perror ("lseek()");
		return (1);
	}
	if ((fp = fopen (filename,"r")) == NULL)
	{
		perror ("fopen()");
		return (1);
	}
retry:
	if ((buf = (u_int8_t *) malloc (size)) == NULL)
	{
		fprintf (stderr, "%s: malloc(%#x) failed\n", __FUNCTION__, size);
		if (size != BUF_SIZE) {
			size = BUF_SIZE;
			fprintf (stderr, "%s: trying buffer size %#x\n", __FUNCTION__, size);
			goto retry;
		}
		perror ("malloc()");
		fclose (fp);
		return (1);
	}
	do {
		if (n <= size)
			size = n;
		if (fread (buf,size,1,fp) != 1 || ferror (fp))
		{
			fprintf (stderr, "%s: fread, size %#x, n %#x\n", __FUNCTION__, size, n);
			perror ("fread()");
			free (buf);
			fclose (fp);
			return (1);
		}
		err = write (fd,buf,size);
		if (err < 0)
		{
			fprintf (stderr, "%s: write, size %#x, n %#x\n", __FUNCTION__, size, n);
			perror ("write()");
			free (buf);
			fclose (fp);
			return (1);
		}
		n -= size;
	} while (n > 0);

	if (buf != NULL)
		free (buf);
	fclose (fp);
//	printf ("Copied %d bytes from %s to address 0x%.8x in flash\n",len,filename,offset);
	return (0);
}
int mtd_read (int fd,u_int32_t offset,void* data,size_t len){
	u_int8_t *buf = NULL;
	int err;
	int size = len * sizeof (u_int8_t);
	int n = len;

	if (offset != lseek (fd,offset,SEEK_SET))
	{
		perror ("lseek()");
		goto err0;
	}

	buf = (u_int8_t*)data;
	do {
		if (n <= size)
			size = n;
		err = read (fd,buf,size);
		if (err < 0)
		{
			fprintf (stderr, "%s: read, size %#x, n %#x\n", __FUNCTION__, size, n);
			goto err0;
		}
		if (err != size)
		{
			fprintf (stderr,"Couldn't copy entire buffer to(%d/%d bytes copied)\n",err,size);
			goto err0;
		}
		buf+=size;
		n -= size;
	} while (n > 0);

	return (n);

err0:
	return -1;    
}
int mtd_write (int fd,u_int32_t offset,void* data,size_t len){
    u_int8_t *buf = NULL;
    int err;
    size_t size = len ;
    size_t n = len;

    if (offset != lseek (fd,offset,SEEK_SET))
    {
        perror ("lseek()");
        return (1);
    }
	buf = (u_int8_t *)data;

    do {
        if (n <= size)
            size = n;
        err = write (fd,buf,size);
        if (err < 0)
        {
            fprintf (stderr, "%s: write, size %#x, n %#x\n", __FUNCTION__, size, n);
            return err;
        }
        n -= size;
		buf=+size;
    } while (n > 0);

    return len;

}

int showinfo (int fd)
{
#ifdef SUPPORT_MTD_PARTITION

	int i,err,n;
	struct mtd_info_user mtd;
	static struct region_info_user region[1024];

	err = getmeminfo (fd,&mtd);
	if (err < 0)
	{
		perror ("MEMGETINFO");
		return (1);
	}

	err = getregions (fd,region,&n);
	if (err < 0)
	{
		perror ("MEMGETREGIONCOUNT");
		return (1);
	}

	printf ("mtd.type = ");
	switch (mtd.type)
	{
		case MTD_ABSENT:
			printf ("MTD_ABSENT");
			break;
		case MTD_RAM:
			printf ("MTD_RAM");
			break;
		case MTD_ROM:
			printf ("MTD_ROM");
			break;
		case MTD_NORFLASH:
			printf ("MTD_NORFLASH");
			break;
		case MTD_NANDFLASH:
			printf ("MTD_NANDFLASH");
			break;
		case MTD_DATAFLASH:
			printf ("MTD_DATAFLASH");
			break;
		case MTD_UBIVOLUME:
			printf ("MTD_UBIVOLUME");
		default:
			printf ("(unknown type - new MTD API maybe?)");
	}

	printf ("\nmtd.flags = ");
	if (mtd.flags == MTD_CAP_ROM)
		printf ("MTD_CAP_ROM");
	else if (mtd.flags == MTD_CAP_RAM)
		printf ("MTD_CAP_RAM");
	else if (mtd.flags == MTD_CAP_NORFLASH)
		printf ("MTD_CAP_NORFLASH");
	else if (mtd.flags == MTD_CAP_NANDFLASH)
		printf ("MTD_CAP_NANDFLASH");
	else if (mtd.flags == MTD_WRITEABLE)
		printf ("MTD_WRITEABLE");
	else
	{
		int first = 1;
		static struct
		{
			const char *name;
			int value;
		} flags[] =
		{
			{ "MTD_WRITEABLE", MTD_WRITEABLE },
			{ "MTD_BIT_WRITEABLE", MTD_BIT_WRITEABLE },
			{ "MTD_NO_ERASE", MTD_NO_ERASE },
			{ "MTD_POWERUP_LOCK", MTD_POWERUP_LOCK },
			{ NULL, -1 }
		};
		for (i = 0; flags[i].name != NULL; i++)
			if (mtd.flags & flags[i].value)
			{
				if (first)
				{
					printf (flags[i].name);
					first = 0;
				}
				else printf (" | %s",flags[i].name);
			}
	}

	printf ("\nmtd.size = ");
	printsize (mtd.size);

	printf ("\nmtd.erasesize = ");
	printsize (mtd.erasesize);

	printf ("\nmtd.writesize = ");
	printsize (mtd.writesize);

	printf ("\nmtd.oobsize = ");
	printsize (mtd.oobsize);

	printf ("\n"
			"regions = %d\n"
			"\n",
			n);

	for (i = 0; i < n; i++)
	{
		printf ("region[%d].offset = 0x%.8x\n"
				"region[%d].erasesize = ",
				i,region[i].offset,i);
		printsize (region[i].erasesize);
		printf ("\nregion[%d].numblocks = %d\n"
				"region[%d].regionindex = %d\n",
				i,region[i].numblocks,
				i,region[i].regionindex);
	}

#endif	
	return (0);
}


int mtd_erase (int fd,u_int32_t offset,size_t len){
	return erase_flash(fd,offset,len);
}


