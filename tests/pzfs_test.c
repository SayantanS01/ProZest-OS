#include "pzfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t disk[9][512];static unsigned checks,fail_read,fail_write;
#define C(x) do{++checks;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int rd(void *ctx,uint32_t lba,uint8_t *b){(void)ctx;if(lba>=9||fail_read)return 0;memcpy(b,disk[lba],512);return 1;}
static int wr(void *ctx,uint32_t lba,const uint8_t *b){(void)ctx;if(lba>=9||fail_write)return 0;memcpy(disk[lba],b,512);return 1;}
static uint32_t rng=0x12345678;static uint32_t rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void){struct pzfs f;struct pzfs_io io={0,rd,wr};uint8_t buf[512]={0},out[512],expected[8][512];size_t lengths[8]={0},n;int live[8]={0};char names[8][8];
 C(pzfs_crc((const uint8_t *)"123456789",9)==0xcbf43926);pzfs_empty_header(disk[0]);C(pzfs_mount(&f,io)==PZFS_OK);C(!pzfs_count(&f));
 for(unsigned i=0;i<8;i++)snprintf(names[i],8,"file%u",i);
 for(unsigned t=0;t<12000;t++){unsigned i=rnd()%8,op=rnd()%3;size_t len=rnd()%513;for(size_t j=0;j<len;j++)buf[j]=(uint8_t)rnd();
  if(op==0){C(pzfs_put(&f,names[i],buf,len)==PZFS_OK);memcpy(expected[i],buf,len);lengths[i]=len;live[i]=1;}
  else if(op==1){C(pzfs_remove(&f,names[i])==(live[i]?PZFS_OK:PZFS_MISSING));live[i]=0;}
  else{C(pzfs_mount(&f,io)==PZFS_OK);}
  unsigned count=0;for(unsigned j=0;j<8;j++){n=999;C(pzfs_read(&f,names[j],out,512,&n)==(live[j]?PZFS_OK:PZFS_MISSING));if(live[j]){C(n==lengths[j]);C(!memcmp(out,expected[j],n));count++;}else C(n==999);}C(pzfs_count(&f)==count);
 }
 for(unsigned i=0;i<8;i++)C(pzfs_put(&f,names[i],buf,512)==PZFS_OK);
 C(pzfs_put(&f,"overflow",buf,1)==PZFS_FULL);C(pzfs_put(&f,"../x",buf,1)==PZFS_INVALID);C(pzfs_put(&f,".",buf,1)==PZFS_INVALID);C(pzfs_put(&f,"abcdefghijklmnopqrstuvwx",buf,1)==PZFS_INVALID);C(pzfs_put(&f,"file0",buf,513)==PZFS_INVALID);C(pzfs_read(&f,"file0",out,511,&n)==PZFS_INVALID);
 for(unsigned i=0;i<512;i++){disk[0][i]^=1;C(pzfs_mount(&f,io)==PZFS_CORRUPT);disk[0][i]^=1;}
 C(pzfs_mount(&f,io)==PZFS_OK);for(unsigned i=1;i<=8;i++)disk[i][0]^=1;C(pzfs_read(&f,"file0",out,512,&n)==PZFS_CORRUPT);for(unsigned i=1;i<=8;i++)disk[i][0]^=1;
 fail_read=1;C(pzfs_read(&f,"file0",out,512,&n)==PZFS_IO);C(pzfs_mount(&f,io)==PZFS_IO);fail_read=0;C(pzfs_mount(&f,io)==PZFS_OK);
 fail_write=1;C(pzfs_put(&f,"file0",buf,1)==PZFS_IO);C(!f.mounted);fail_write=0;C(pzfs_mount(&f,io)==PZFS_OK);C(pzfs_read(&f,"file0",out,512,&n)==PZFS_OK&&n==512);
 printf("M17 filesystem assertions: %u PASS; 12000 operations\n",checks);return 0;}
