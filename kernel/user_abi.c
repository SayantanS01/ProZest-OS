#include "user_abi.h"
int user_write_range(uint64_t a,uint64_t n){
    return n>0&&n<=USER_MAX_WRITE&&a>=USER_DATA&&a<USER_GUARD&&n<=USER_GUARD-a;
}
int user_fault_expected(unsigned m,uint64_t v,uint64_t e,uint64_t cr2,uint64_t k){
    switch(m){
    case 1:return v==14&&e==5&&cr2==k;
    case 2:return v==14&&e==7&&cr2==USER_CODE;
    case 3:return v==14&&e==21&&cr2==USER_DATA+64;
    case 4:return v==14&&e==4&&cr2==USER_GUARD;
    case 5:return v==13&&e==0;
    default:return 0;
    }
}
