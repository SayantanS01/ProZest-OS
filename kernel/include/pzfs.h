#ifndef PROZEST_PZFS_H
#define PROZEST_PZFS_H
#include <stdint.h>
#include <stddef.h>
#define PZFS_SECTOR 512u
#define PZFS_SLOTS 8u
#define PZFS_DISK_SECTORS 2048u
#define PZFS_NAME 24u
struct pzfs_io { void *ctx; int (*read)(void *,uint32_t,uint8_t *); int (*write)(void *,uint32_t,const uint8_t *); };
struct pzfs { struct pzfs_io io; uint8_t meta[PZFS_SECTOR]; unsigned mounted; };
enum pzfs_result { PZFS_OK=0,PZFS_INVALID,PZFS_IO,PZFS_CORRUPT,PZFS_MISSING,PZFS_FULL };
uint32_t pzfs_crc(const uint8_t *,size_t);
void pzfs_empty_header(uint8_t *);
int pzfs_mount(struct pzfs *,struct pzfs_io);
int pzfs_read(struct pzfs *,const char *,uint8_t *,size_t,size_t *);
int pzfs_put(struct pzfs *,const char *,const uint8_t *,size_t);
int pzfs_remove(struct pzfs *,const char *);
unsigned pzfs_count(const struct pzfs *);
#endif
