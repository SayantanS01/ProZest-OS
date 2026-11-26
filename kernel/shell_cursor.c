#include "shell.h"
static int eq(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static size_t len(const char *s){size_t n=0;while(s[n])n++;return n;}
unsigned shell_key(struct shell_keys *k,uint8_t b){
    static const char normal[128]={[2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',[10]='9',[11]='0',[12]='-',[13]='=',[14]=8,[16]='q',[17]='w',[18]='e',[19]='r',[20]='t',[21]='y',[22]='u',[23]='i',[24]='o',[25]='p',[26]='[',[27]=']',[30]='a',[31]='s',[32]='d',[33]='f',[34]='g',[35]='h',[36]='j',[37]='k',[38]='l',[39]=';',[40]='\'',[41]='`',[43]='\\',[44]='z',[45]='x',[46]='c',[47]='v',[48]='b',[49]='n',[50]='m',[51]=',',[52]='.',[53]='/',[57]=' '};
    static const char shifted[128]={[2]='!',[3]='@',[4]='#',[5]='$',[6]='%',[7]='^',[8]='&',[9]='*',[10]='(',[11]=')',[12]='_',[13]='+',[26]='{',[27]='}',[39]=':',[40]='"',[41]='~',[43]='|',[51]='<',[52]='>',[53]='?'};
    if(k->skip){k->skip--;return 0;}if(b==0xe1){k->skip=5;k->extended=0;return 0;}if(b==0xe0){k->extended=1;return 0;}if(k->extended){k->extended=0;return 0;}
    if((b&127)==42){k->left=!(b&128);return 0;}if((b&127)==54){k->right=!(b&128);return 0;}if(b==0x9c)return 10;if(b&128)return 0;
    unsigned c=(unsigned char)normal[b];if(k->left||k->right){if(c>='a'&&c<='z')c-=32;else if(shifted[b])c=(unsigned char)shifted[b];}return c;
}
unsigned shell_feed(struct shell_line *l,unsigned c){
    if(c==10){l->text[l->length]=0;return l->overflow?4:3;}
    if(c==8){if(l->overflow){l->overflow--;return 0;}if(!l->length)return 0;l->text[--l->length]=0;return 2;}
    if(c<32||c>126)return 0;
    if(l->length==SHELL_LINE){if(l->overflow<UINT32_MAX)l->overflow++;return 0;}
    l->text[l->length++]=(char)c;l->text[l->length]=0;return 1;
}
static char *spaces(char *p){while(*p==' ')p++;return p;}
static char *word(char **p){char *a=spaces(*p),*b=a;while(*b&&*b!=' ')b++;if(*b)*b++=0;*p=spaces(b);return a;}
static void out(struct shell_io *io,const char *s){io->out(io->ctx,s);}
static void decimal(struct shell_io *io,uint64_t n){char b[21];unsigned i=20;b[i]=0;do{b[--i]=(char)('0'+n%10);n/=10;}while(n);out(io,b+i);}
static void result(struct shell_io *io,int r){static const char *const messages[]={"ok\n","error: invalid argument\n","error: disk I/O\n","error: corrupt filesystem\n","error: file not found\n","error: filesystem full\n"};out(io,r>=0&&r<=PZFS_FULL?messages[r]:"error: unexpected status\n");}
static int reserved(const char *s){return eq(s,"seed.txt")||eq(s,"note.txt")||eq(s,"temp.txt");}
int shell_exec(struct pzfs *fs,struct shell_io *io,char *line){
    char *p=line,*cmd=word(&p);if(!*cmd)return 0;
    if(eq(cmd,"echo")){out(io,p);out(io,"\n");return 0;}
    if(eq(cmd,"help")||eq(cmd,"version")||eq(cmd,"mem")||eq(cmd,"clear")||eq(cmd,"ls")||eq(cmd,"exit")){
        if(*p){out(io,"error: unexpected arguments\n");return 0;}
        if(eq(cmd,"help"))out(io,"help version mem clear echo ls cat touch write rm exit\n");
        else if(eq(cmd,"version"))out(io,"ProZest OS v0.0.7-rc12-cursor\n");
        else if(eq(cmd,"mem")){out(io,"managed pages: ");decimal(io,io->managed);out(io," free pages: ");decimal(io,io->free);out(io,"\n");}
        else if(eq(cmd,"clear")){io->clear(io->ctx);out(io,"screen cleared\n");}
        else if(eq(cmd,"exit")){out(io,"shell halted\n");return 1;}
        else if(!fs->mounted)result(io,PZFS_INVALID);
        else {for(unsigned i=0;i<PZFS_SLOTS;i++){const uint8_t *e=fs->meta+16+32*i;if(e[0]){out(io,(const char *)e);out(io,"\n");}}}
        return 0;
    }
    if(!eq(cmd,"cat")&&!eq(cmd,"touch")&&!eq(cmd,"write")&&!eq(cmd,"rm")){out(io,"error: unknown command\n");return 0;}
    char *name=word(&p);if(!*name||(!eq(cmd,"write")&&*p)){result(io,PZFS_INVALID);return 0;}
    uint8_t b[512];size_t n=0;
    if(eq(cmd,"cat")){int r=pzfs_read(fs,name,b,sizeof b,&n);if(r!=PZFS_OK){result(io,r);return 0;}for(size_t i=0;i<n;i++){char s[2]={(char)((b[i]==10||(b[i]>=32&&b[i]<=126))?b[i]:'?'),0};out(io,s);}if(!n||b[n-1]!=10)out(io,"\n");return 0;}
    if(reserved(name)){out(io,"error: reserved file\n");return 0;}
    if(eq(cmd,"rm")){result(io,pzfs_remove(fs,name));return 0;}
    int r=pzfs_read(fs,name,b,sizeof b,&n);
    if(r!=PZFS_OK&&r!=PZFS_MISSING){result(io,r);return 0;}
    if(eq(cmd,"touch")&&r==PZFS_OK){result(io,PZFS_OK);return 0;}
    /* Keep one scratch slot free for the inherited storage regression on reboot. */
    if(r==PZFS_MISSING&&pzfs_count(fs)>=PZFS_SLOTS-1){result(io,PZFS_FULL);return 0;}
    result(io,pzfs_put(fs,name,(const uint8_t *)p,len(p)));return 0;
}
