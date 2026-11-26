#include "pzfs.h"
/* Dedicated QEMU ISA IDE test channel; never use the normal host disk ports. */
#define ATA_BASE 0x1e8u
#define ATA_CTL 0x3eeu
static unsigned ata_ready;
static void ata_delay(void){for(unsigned i=0;i<4;i++)(void)irq_in(ATA_CTL);}
static int ata_wait(unsigned drq){for(unsigned i=0;i<2000000;i++){uint8_t s=irq_in(ATA_CTL);if(s==0||s==255)return 0;if(s&0x80)continue;if(s&0x21)return 0;if(!!(s&8)==drq)return 1;__asm__ volatile("pause");}return 0;}
static uint16_t ata_inw(void){uint16_t v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"((uint16_t)ATA_BASE):"memory");return v;}
static void ata_outw(uint16_t v){__asm__ volatile("outw %0,%1"::"a"(v),"Nd"((uint16_t)ATA_BASE):"memory");}
static int ata_identify(void){uint16_t id[256];ata_ready=0;irq_out(ATA_CTL,2);irq_out(ATA_BASE+6,0xa0);ata_delay();for(unsigned i=2;i<=5;i++)irq_out(ATA_BASE+i,0);irq_out(ATA_BASE+7,0xec);ata_delay();if(!ata_wait(1))return 0;for(unsigned i=0;i<256;i++)id[i]=ata_inw();if(!ata_wait(0))return 0;(void)irq_in(ATA_BASE+7);const char serial[21]="PROZEST-M17-LAB     ";for(unsigned i=0;i<10;i++)if((id[10+i]>>8)!=(uint8_t)serial[2*i]||(id[10+i]&255)!=(uint8_t)serial[2*i+1])return 0;uint32_t sectors=id[60]|((uint32_t)id[61]<<16);if(!(id[49]&0x200)||sectors!=PZFS_DISK_SECTORS)return 0;ata_ready=1;number("STORAGE SECTORS: ",sectors);return 1;}
static int ata_begin(uint32_t lba,uint8_t cmd){if(!ata_ready||lba>=PZFS_DISK_SECTORS||!ata_wait(0))return 0;irq_out(ATA_BASE+6,0xe0|(uint8_t)(lba>>24));ata_delay();irq_out(ATA_BASE+2,1);irq_out(ATA_BASE+3,(uint8_t)lba);irq_out(ATA_BASE+4,(uint8_t)(lba>>8));irq_out(ATA_BASE+5,(uint8_t)(lba>>16));irq_out(ATA_BASE+7,cmd);ata_delay();return ata_wait(1);}
static int ata_read(void *ctx,uint32_t lba,uint8_t *p){(void)ctx;if(!p||!ata_begin(lba,0x20))return 0;for(unsigned i=0;i<256;i++){uint16_t v=ata_inw();p[2*i]=(uint8_t)v;p[2*i+1]=(uint8_t)(v>>8);}ata_delay();int ok=ata_wait(0);(void)irq_in(ATA_BASE+7);return ok;}
static int ata_write(void *ctx,uint32_t lba,const uint8_t *p){(void)ctx;if(!p||lba>8||!ata_begin(lba,0x30))return 0;for(unsigned i=0;i<256;i++)ata_outw((uint16_t)(p[2*i]|((uint16_t)p[2*i+1]<<8)));ata_delay();if(!ata_wait(0))return 0;irq_out(ATA_BASE+7,0xe7);ata_delay();int ok=ata_wait(0);(void)irq_in(ATA_BASE+7);return ok;}
static int storage_equal(const uint8_t *a,const char *b,size_t n){for(size_t i=0;i<n;i++)if(a[i]!=(uint8_t)b[i])return 0;return 1;}
static void storage_run(void){
    struct pzfs fs;struct pzfs_io io={0,ata_read,ata_write};uint8_t b[512];size_t n=0;
    const char seed[]="ProZest storage seed\n",note[]="ProZest persistent file\n",temp[]="temporary";
    check("M17.1 BLOCK CONTEXT: PASS",!(irq_flags()&0x200)&&vmm_counts(&vmm_active));
    check("M17.2 ATA IDENTIFY: PASS",ata_identify());
    check("M17.3 SECTOR READ: PASS",ata_read(0,0,b)&&!ata_read(0,PZFS_DISK_SECTORS,b));
    check("M17.4 FILESYSTEM MOUNT: PASS",pzfs_mount(&fs,io)==PZFS_OK);
    check("M17.5 FILE READ: PASS",pzfs_read(&fs,"seed.txt",b,sizeof b,&n)==PZFS_OK&&n==sizeof seed-1&&storage_equal(b,seed,n));
    unsigned initial=pzfs_count(&fs);
    check("M18 STORAGE SLOT RESERVE: PASS",initial<=7&&pzfs_read(&fs,"temp.txt",b,sizeof b,&n)==PZFS_MISSING);
    int prior=pzfs_read(&fs,"note.txt",b,sizeof b,&n);check("M17 PERSISTENCE STATE: PASS",(prior==PZFS_MISSING&&initial<=6)||(prior==PZFS_OK&&initial>=2&&n==sizeof note-1&&storage_equal(b,note,n)));
    number("STORAGE PRIOR FILE: ",prior==PZFS_OK);
    unsigned expected=initial+(prior==PZFS_MISSING);
    check("M17.6 FILE CREATE: PASS",pzfs_put(&fs,"temp.txt",(const uint8_t *)temp,sizeof temp-1)==PZFS_OK&&pzfs_read(&fs,"temp.txt",b,sizeof b,&n)==PZFS_OK&&n==sizeof temp-1&&storage_equal(b,temp,n));
    check("M17.7 FILE UPDATE: PASS",pzfs_put(&fs,"note.txt",(const uint8_t *)temp,sizeof temp-1)==PZFS_OK&&pzfs_put(&fs,"note.txt",(const uint8_t *)note,sizeof note-1)==PZFS_OK);
    check("M17.8 FILE DELETE: PASS",pzfs_remove(&fs,"temp.txt")==PZFS_OK&&pzfs_read(&fs,"temp.txt",b,sizeof b,&n)==PZFS_MISSING&&pzfs_count(&fs)==expected);
    check("M17.9 REMOUNT VERIFY: PASS",pzfs_mount(&fs,io)==PZFS_OK&&pzfs_read(&fs,"note.txt",b,sizeof b,&n)==PZFS_OK&&n==sizeof note-1&&storage_equal(b,note,n)&&pzfs_count(&fs)==expected);
    check("M17.10 STORAGE CLEANUP: PASS",pzfs_put(&fs,"../bad",b,1)==PZFS_INVALID&&pzfs_put(&fs,"bad",b,513)==PZFS_INVALID&&vmm_counts(&vmm_active)&&!(irq_flags()&0x200));
    log_write(LOG_OK,"Persistent test disk and filesystem verified");qemu_debugcon_puts("M17 COMPLETE\n");
}
