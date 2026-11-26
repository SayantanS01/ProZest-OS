#include "pzfs.h"
static const uint8_t magic[8]={'P','Z','F','S','0','0','1','!'};
static void zero(uint8_t *p,size_t n){while(n--)*p++=0;}
static void copy(uint8_t *p,const uint8_t *q,size_t n){while(n--)*p++=*q++;}
static int same(const uint8_t *p,const uint8_t *q,size_t n){while(n--)if(*p++!=*q++)return 0;return 1;}
static int empty(const uint8_t *p,size_t n){while(n--)if(*p++)return 0;return 1;}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
uint32_t pzfs_crc(const uint8_t *p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return ~c;}
static void seal(uint8_t *p){put32(p+508,pzfs_crc(p,508));}
void pzfs_empty_header(uint8_t *p){zero(p,512);copy(p,magic,8);put32(p+8,1);put32(p+12,PZFS_DISK_SECTORS);seal(p);}
static int name_encode(const char *s,uint8_t *out){if(!s)return 0;zero(out,PZFS_NAME);unsigned i=0;for(;i<PZFS_NAME;i++){unsigned char c=(unsigned char)s[i];if(!c)break;if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return 0;out[i]=c;}return i>0&&i<PZFS_NAME&&!(i==1&&out[0]=='.')&&!(i==2&&out[0]=='.'&&out[1]=='.');}
static uint8_t *entry(uint8_t *m,unsigned i){return m+16+32*i;}
static const uint8_t *centry(const uint8_t *m,unsigned i){return m+16+32*i;}
static int valid(const uint8_t *m){if(!same(m,magic,8)||get32(m+8)!=1||get32(m+12)!=PZFS_DISK_SECTORS||get32(m+508)!=pzfs_crc(m,508)||!empty(m+272,236))return 0;for(unsigned i=0;i<PZFS_SLOTS;i++){const uint8_t *e=centry(m,i);if(!e[0]){if(!empty(e,32))return 0;continue;}uint8_t n[24];if(!name_encode((const char *)e,n)||!same(n,e,24)||get32(e+24)>512)return 0;for(unsigned j=0;j<i;j++)if(same(e,centry(m,j),24))return 0;}return 1;}
int pzfs_mount(struct pzfs *f,struct pzfs_io io){if(!f)return PZFS_INVALID;f->mounted=0;if(!io.read||!io.write)return PZFS_INVALID;f->io=io;if(!io.read(io.ctx,0,f->meta))return PZFS_IO;if(!valid(f->meta))return PZFS_CORRUPT;f->mounted=1;return PZFS_OK;}
static int locate(const struct pzfs *f,const uint8_t *n){for(unsigned i=0;i<PZFS_SLOTS;i++)if(same(centry(f->meta,i),n,24))return (int)i;return -1;}
unsigned pzfs_count(const struct pzfs *f){unsigned n=0;if(f&&f->mounted)for(unsigned i=0;i<PZFS_SLOTS;i++)n+=centry(f->meta,i)[0]!=0;return n;}
int pzfs_read(struct pzfs *f,const char *name,uint8_t *out,size_t cap,size_t *length){uint8_t n[24],buf[512];if(!f||!f->mounted||!out||!length||!name_encode(name,n))return PZFS_INVALID;int i=locate(f,n);if(i<0)return PZFS_MISSING;const uint8_t *e=centry(f->meta,(unsigned)i);size_t len=get32(e+24);if(cap<len)return PZFS_INVALID;if(!f->io.read(f->io.ctx,(unsigned)i+1,buf))return PZFS_IO;if(pzfs_crc(buf,len)!=get32(e+28)||!empty(buf+len,512-len))return PZFS_CORRUPT;copy(out,buf,len);*length=len;return PZFS_OK;}
static int commit(struct pzfs *f,uint8_t *m){seal(m);if(!f->io.write(f->io.ctx,0,m)){f->mounted=0;return PZFS_IO;}copy(f->meta,m,512);return PZFS_OK;}
int pzfs_put(struct pzfs *f,const char *name,const uint8_t *data,size_t len){uint8_t n[24],buf[512],m[512];if(!f||!f->mounted||(!data&&len)||len>512||!name_encode(name,n))return PZFS_INVALID;int i=locate(f,n);if(i<0){for(unsigned j=0;j<PZFS_SLOTS;j++)if(!centry(f->meta,j)[0]){i=(int)j;break;}}if(i<0)return PZFS_FULL;zero(buf,512);copy(buf,data,len);copy(m,f->meta,512);uint8_t *e=entry(m,(unsigned)i);copy(e,n,24);put32(e+24,(uint32_t)len);put32(e+28,pzfs_crc(buf,len));if(!f->io.write(f->io.ctx,(unsigned)i+1,buf)){f->mounted=0;return PZFS_IO;}return commit(f,m);}
int pzfs_remove(struct pzfs *f,const char *name){uint8_t n[24],m[512];if(!f||!f->mounted||!name_encode(name,n))return PZFS_INVALID;int i=locate(f,n);if(i<0)return PZFS_MISSING;copy(m,f->meta,512);zero(entry(m,(unsigned)i),32);return commit(f,m);}
