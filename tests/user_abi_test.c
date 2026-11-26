#include "user_abi.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line %d\n",__LINE__);return 1;}}while(0)
int main(void){
    for(uint64_t a=USER_DATA-64;a<USER_GUARD+64;a++)for(uint64_t n=0;n<130;n++){
        int expected=0;
        if(n&&n<=64){expected=1;for(uint64_t i=0;i<n;i++)if(a+i<USER_DATA||a+i>=USER_GUARD)expected=0;}
        C(user_write_range(a,n)==expected);
    }
    C(!user_write_range(UINT64_MAX,1));C(!user_write_range(USER_DATA,UINT64_MAX));C(!user_write_range(0,1));
    C(!user_write_range(UINT64_C(0xffffffff80000000),7));
    uint64_t k=UINT64_C(0xffffffff80004000),addresses[]={k,USER_CODE,USER_DATA+64,USER_GUARD,0};
    for(unsigned m=0;m<7;m++)for(unsigned v=0;v<32;v++)for(unsigned e=0;e<32;e++)for(unsigned a=0;a<5;a++){
        int ok=(m==1&&v==14&&e==5&&a==0)||(m==2&&v==14&&e==7&&a==1)||(m==3&&v==14&&e==21&&a==2)||(m==4&&v==14&&e==4&&a==3)||(m==5&&v==13&&e==0);
        C(user_fault_expected(m,v,e,addresses[a],k)==ok);
    }
    printf("M16 ABI assertions: %u PASS\n",checks);return 0;
}
