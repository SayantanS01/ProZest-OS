#ifndef PROZEST_DEVICE_CORE_H
#define PROZEST_DEVICE_CORE_H
#include <stdint.h>
int pci_config_address(unsigned bus,unsigned dev,unsigned fn,unsigned reg,uint32_t *out);
struct key_decoder { unsigned down,press,release,invalid,extended; };
/* Set 1 decoder for unextended A key, ignores other valid keys. */
void key_decode(struct key_decoder *,uint8_t);
#endif
