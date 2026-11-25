#include "device_core.h"
int pci_config_address(unsigned b,unsigned d,unsigned f,unsigned r,uint32_t *out){
    if(!out||b>255||d>31||f>7||r>252||(r&3))return 0;
    *out=UINT32_C(0x80000000)|(b<<16)|(d<<11)|(f<<8)|r;return 1;
}
void key_decode(struct key_decoder *k,uint8_t b){
    if(!k)return;
    if(b==0xe0){k->extended=1;return;}
    if(b==0xe1){k->invalid++;k->extended=0;return;}
    if(k->extended){k->extended=0;return;}
    if(b==0x1e){if(!k->down){k->down=1;k->press++;}}
    else if(b==0x9e){if(k->down){k->down=0;k->release++;}else k->invalid++;}
}
