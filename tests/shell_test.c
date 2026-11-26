#include "shell.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define C(x) do{checks++;assert(x);}while(0)
static uint8_t disk[9][512];
static int read_sector(void *c,uint32_t i,uint8_t *p){(void)c;if(i>=9)return 0;memcpy(p,disk[i],512);return 1;}
static int write_sector(void *c,uint32_t i,const uint8_t *p){(void)c;if(i>=9)return 0;memcpy(disk[i],p,512);return 1;}
static char output[4096];static size_t used;static unsigned cleared;
static void emit(void *c,const char *s){(void)c;size_t n=strlen(s);C(used+n<sizeof output);memcpy(output+used,s,n+1);used+=n;}
static void clear(void *c){(void)c;cleared++;}
static void cmd(struct pzfs *fs,struct shell_io *io,const char *s,const char *want,int exit){char b[97];C(strlen(s)<=96);strcpy(b,s);used=0;output[0]=0;C(shell_exec(fs,io,b)==exit);C(!strcmp(output,want));}
int main(void){
    struct shell_keys k={0};C(shell_key(&k,30)=='a');C(!shell_key(&k,0x9e));C(!shell_key(&k,42));C(shell_key(&k,30)=='A');C(shell_key(&k,12)=='_');C(!shell_key(&k,0xaa));C(shell_key(&k,12)=='-');C(!shell_key(&k,28));C(shell_key(&k,0x9c)==10);C(shell_key(&k,14)==8);C(!shell_key(&k,0xe0));C(!shell_key(&k,0x48));C(!shell_key(&k,0xe1));for(unsigned i=0;i<5;i++)C(!shell_key(&k,30));C(shell_key(&k,30)=='a');
    struct shell_line l={0};C(!shell_feed(&l,8));for(unsigned i=0;i<96;i++)C(shell_feed(&l,'x')==1);C(!shell_feed(&l,'y'));C(shell_feed(&l,10)==4);C(!shell_feed(&l,8));C(shell_feed(&l,8)==2);C(shell_feed(&l,'z')==1);C(shell_feed(&l,10)==3);C(l.length==96&&l.text[95]=='z'&&!l.text[96]);
    uint32_t rng=0x3d123456;unsigned expected=0,overflow=0;memset(&l,0,sizeof l);for(unsigned i=0;i<100000;i++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;unsigned c=rng%128;unsigned ev=shell_feed(&l,c);if(c==10){C(ev==(overflow?4:3));memset(&l,0,sizeof l);expected=overflow=0;}else if(c==8){if(overflow){overflow--;C(ev==0);}else if(expected){expected--;C(ev==2);}else C(!ev);}else if(c>=32&&c<=126){if(expected<96){expected++;C(ev==1);}else{overflow++;C(!ev);}}else C(!ev);C(l.length==expected&&l.overflow==overflow&&!l.text[expected]);}
    struct pzfs fs;struct pzfs_io diskio={0,read_sector,write_sector};pzfs_empty_header(disk[0]);C(pzfs_mount(&fs,diskio)==PZFS_OK);C(pzfs_put(&fs,"seed.txt",(const uint8_t*)"seed",4)==PZFS_OK);C(pzfs_put(&fs,"note.txt",(const uint8_t*)"note",4)==PZFS_OK);struct shell_io io={0,emit,clear,100,99};
    cmd(&fs,&io,"help","help version mem clear echo ls cat touch write rm exit\n",0);cmd(&fs,&io,"version","ProZest OS v0.0.7-rc10\n",0);cmd(&fs,&io,"mem","managed pages: 100 free pages: 99\n",0);cmd(&fs,&io,"echo hello world","hello world\n",0);cmd(&fs,&io,"ls","seed.txt\nnote.txt\n",0);cmd(&fs,&io,"touch a.txt","ok\n",0);cmd(&fs,&io,"write a.txt hello world","ok\n",0);cmd(&fs,&io,"touch a.txt","ok\n",0);cmd(&fs,&io,"cat a.txt","hello world\n",0);C(pzfs_mount(&fs,diskio)==PZFS_OK);cmd(&fs,&io,"cat a.txt","hello world\n",0);cmd(&fs,&io,"rm a.txt","ok\n",0);cmd(&fs,&io,"cat a.txt","error: file not found\n",0);cmd(&fs,&io,"rm seed.txt","error: reserved file\n",0);cmd(&fs,&io,"write ../bad test","error: invalid argument\n",0);cmd(&fs,&io,"ls extra","error: unexpected arguments\n",0);cmd(&fs,&io,"badcmd","error: unknown command\n",0);cmd(&fs,&io,"clear","screen cleared\n",0);C(cleared==1);for(unsigned i=0;i<5;i++){char b[16];snprintf(b,sizeof b,"touch f%u",i);cmd(&fs,&io,b,"ok\n",0);}cmd(&fs,&io,"touch full","error: filesystem full\n",0);cmd(&fs,&io,"exit","shell halted\n",1);
    printf("M18 shell assertions: %u PASS; 100000 randomized input bytes\n",checks);
}
