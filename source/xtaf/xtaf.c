
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/reent.h>
#include <sys/stat.h>
#include <limits.h>
#include <sys/dirent.h>

#include <debug.h>
#include "fakeio.h"
#include "xtaf.h"

#include "ata.h"
#include "cache.h"

//	http://www.free60.org/FATX
//	http://www.free60.org/XTAF
//	http://nds.cmamod.com/x360/test/MSFTMemoryUnit.7z
//	http://free60.git.sourceforge.net/git/gitweb.cgi?p=free60/libxenon;a=blob_plain;f=libxenon/drivers/fat/fat.c;h=ebdabb029fc6d9693eb196b620df697ad4c70d65;hb=refs/heads/ced2911

#define XBOX_1_COMPATIBILY_PARTITION 0x120eb0000
#define XBOX_1_COMPATIBILY_PARTITION_SIZE 0x10000000
#define XBOX_360_DEVKIT_PARTITION	0xA17D0000
#define MU1_PARTITION	0x7FF000
#define MU_SIZE 243273728
#define XBOX_360_PARTITION 0x130eb0000 // HDD1 partition
#define XBOX_360_PARTITION_SIZE 0x1AC1AC6000//(0x0DF94BB0*0x200)-0x130eb0000


#include "xtaf_endian.h"

// debug message
#define XTAF_DEBUG 1

#define inline


/** get next cluster used by the file stream **/
uint32_t xfat_get_next_cluster(xtaf_partition_private *priv, uint32_t cluster_id) {

	bool err;
	struct _xtaf_partition_hdr_s * part_hdr = &priv->partition_hdr;

	//se position a l'adresse de la fat
	uint64_t file_system_size = priv->file_system_size;
	uint32_t spc = part_hdr->sector_per_cluster; /* sectors per cluster */

	uint64_t numclusters = file_system_size / spc;
	uint8_t fatmult = numclusters >= 0xfff4 ? sizeof (uint32_t) : sizeof (uint16_t);
	
	uint32_t nextClusterPos = cluster_id * fatmult;
	
	uint32_t seek_sector = nextClusterPos/XENON_DISK_SECTOR_SIZE;
	uint32_t seek_pos = nextClusterPos%XENON_DISK_SECTOR_SIZE;

	uint32_t sector = priv->fat_offset +  priv->partition_start_offset + seek_sector;
	
	uint32_t next;

	//ioread(priv, (unsigned char*) &next, priv->fat_offset + (cluster_id * fatmult), fatmult);
	
	bool read_err = _XTAF_cache_readPartialSector (priv->cache, &next, sector,seek_pos,fatmult);
	
	//ioread(priv,&next,(priv->fat_offset + (cluster_id * fatmult))*0x200,fatmult);
	
	xprintf("%lx\n",(uint64_t)sector*0x200);
	
	if (fatmult == sizeof (uint32_t)) {
		next = host2be32((uint32_t) next);
	} else {
		next = host2be16((uint16_t) next);
	}


	xprintf("next cluster : %08x\n", next);

	return next;
};

/** **/
static const char * parse_path(char * fat_name, const char * path) {
	int i = 0; // strlen((char*)ctx->fat_name+1);

	memset(fat_name, 0, 0x2A + 1);

	while (*path == '/')
		path++;

	while (*path && *path != '/') {
		fat_name[i++] = *path++;
	};
	return path;
};

#ifndef XENON
static void buffer_dump(void * buf, int size){
	int i;
	char* cbuf=(char*) buf;

	printf("---- %p %d\n",buf,size);

	while(size>0){
		for(i=0;i<16;++i){
			printf("%02x ",*cbuf);
			++cbuf;
		}
		printf("\n");
		size-=16;
	}
}
#endif

/** parse a sector and get file/directory information  **/
int xtaf_parse_entry(xtaf_partition_private * priv, struct _xtaf_directory_s * data) {	
	uint64_t sector =  priv->current_sector + priv->root_offset +  priv->partition_start_offset;
	
	//xprintf("parse entry offset : %16lx\r\n", sector*0x200);
	
	memset(data, 0, sizeof (struct _xtaf_directory_s));
	
	//priv->extent_offset = 0x3000;

	bool read_err = _XTAF_cache_readPartialSector (priv->cache, data, sector ,priv->extent_offset,sizeof (struct _xtaf_directory_s));
	
	//buffer_dump(data,sizeof(struct _xtaf_directory_s));

	if (read_err == true) {

		priv->extent_offset += sizeof (struct _xtaf_directory_s);

		data->file_size = host2be32(data->file_size);
		data->starting_cluster = host2be32(data->starting_cluster);

		//end of table
		if (data->filename_size == 0xFF) {
			//XTAFError("xtaf_parse_entry : not an entry\r\n"); // not an error
			return 1;
		}else{
			data->filename[data->filename_size]=0;
		}
		return 0;
	}
	xprintf("xtaf_parse_entry : Read error ... %x\n", read_err);
	return -1;
}


int xtaf_check_filename(const char *s1, const char *s2, size_t n) {
	//xprintf("check_filename %s - %s\n",s1,s2);
	if (strlen(s1) == n) {
		return strncasecmp(s1, s2, n);
	}
	return -1;
}

/**
 * return 2 => found dir
 * return 1 => found file
 * reutrn <0 => error
 **/
int xtaf_parse(xtaf_file_private * file_private, char *name) {
	const char *tt = name;
	char fat_name[0x2A];
	
	int cluster_size = file_private->partition->partition_hdr.sector_per_cluster;
		
	while (1) {
		tt = parse_path(fat_name, tt);
		/*
if (tt[0] == 0) {
	xprintf("Dir Found : %s\r\n", name);
	return 0;
}
		 */
		//printf("parse_path %s\r\n",tt);
		while (1) {
			// browse the fat
			//int err = xtaf_parse_entry(priv, &pCtx->finfo);
			//int err = xtaf_parse_entry(file_private->partition, &file_private->finfo);
			int err = xtaf_parse_entry(file_private->partition, &file_private->finfo);
			if (err == 1) {
				xprintf("xtaf_parse_entry finished\r\n");
				return 0;
			} else if (err == 0) {
				//xprintf("found : %s\r\n", file_private->finfo.filename);
				//xprintf("%s - %s \r\n", (char*) pCtx->fat_name, (char*) pCtx->finfo.filename);
			} else {
				xprintf("error : %d\r\n", err);
				return -1;
			}
			
			//printf("%")

			// check if the wanted file/dir is the same as the found one
			if (xtaf_check_filename(fat_name, (char*) file_private->finfo.filename, file_private->finfo.filename_size) == 0) {
				if (file_private->finfo.file_size == 0) {
					
					file_private->partition->extent_offset = 0;
					file_private->partition->current_sector = ((file_private->finfo.starting_cluster - 1) * cluster_size);

					xprintf("found a dir %s\r\n", fat_name);
					strcpy(name, tt);
					return 2;
				} else {
					xprintf("found a file %s\r\n", file_private->finfo.filename);
					xprintf("size %d\r\n", file_private->finfo.file_size);
					
					file_private->partition->extent_offset = 0;
					file_private->partition->current_sector = ((file_private->finfo.starting_cluster - 1) * cluster_size);
					
					uint64_t offset = ((file_private->finfo.starting_cluster - 1) * cluster_size) + file_private->partition->root_offset +  file_private->partition->partition_start_offset;
					xprintf("offset : %16lx\r\n", offset*0x200);
					
					strcpy(name, tt);
					return 1;
				}
			}
		}
	}
	
	
	xprintf("Can't open\n");
	//can't open
	return -1;
}



/**
 * return 2 => found dir
 * return 1 => found file
 * reutrn <0 => error
 **/
int xtaf_get_dir(xtaf_dir_entry * entry, char *name) {
	const char *tt = name;
	char fat_name[0x2A];
	
	int cluster_size = entry->partition->partition_hdr.sector_per_cluster;
	
	if ((name[0] == 0) || (name[0] == '/' && name[1] == 0)) {
		// root fs
		return 0;
	}
		
	while (1) {
		tt = parse_path(fat_name, tt);
		/*
if (tt[0] == 0) {
	xprintf("Dir Found : %s\r\n", name);
	return 0;
}
		 */
		//printf("parse_path %s\r\n",tt);
		
		
		while (1) {
			// browse the fat
			//int err = xtaf_parse_entry(priv, &pCtx->finfo);
			//int err = xtaf_parse_entry(file_private->partition, &file_private->finfo);
			int err = xtaf_parse_entry(entry->partition, &entry->finfo);
			if (err == 1) {
				xprintf("xtaf_parse_entry failed\r\n");
				return -1;
			} else if (err == 0) {
				//xprintf("found : %s\r\n", entry->finfo.filename);
				//xprintf("%s - %s \r\n", (char*) pCtx->fat_name, (char*) pCtx->finfo.filename);
			} else {
				xprintf("error : %d\r\n", err);
				return -1;
			}
			
			//printf("%")

			// check if the wanted file/dir is the same as the found one
			if (xtaf_check_filename(fat_name, (char*) entry->finfo.filename, entry->finfo.filename_size) == 0) {
				if (entry->finfo.file_size == 0) {
					
					entry->partition->extent_offset = 0;
					entry->partition->current_sector = ((entry->finfo.starting_cluster - 1) * cluster_size);

					//xprintf("found a dir %s\r\n", fat_name);
					strcpy(name, tt);
					return 2;
				} else {
					xprintf("found a file %s\r\n", entry->finfo.filename);
					xprintf("size %d\r\n", entry->finfo.file_size);
					
					entry->partition->extent_offset = 0;
					entry->partition->current_sector = ((entry->finfo.starting_cluster - 1) * cluster_size);
					
					uint64_t offset = ((entry->finfo.starting_cluster - 1) * cluster_size) + entry->partition->root_offset +  entry->partition->partition_start_offset;
					xprintf("offset : %16lx\r\n", offset*0x200);
					
					strcpy(name, tt);
					return 1;
				}
			}
		}
	}
	
	
	xprintf("Can't open\n");
	//can't open
	return -1;
}


static struct xtaf_context ctx;
#ifdef XENON
extern DISC_INTERFACE xenon_ata_ops;

static const DISC_INTERFACE* get_io_ata(void) {
	return &xenon_ata_ops;
}
#endif
void init_xtaf(){
	DISC_INTERFACE* disc = get_io_ata();
	
	xtaf_init(&ctx, disc);
}