#ifndef PROZEST_USER_ABI_H
#define PROZEST_USER_ABI_H
#include <stdint.h>
#define USER_CODE UINT64_C(0x400000)
#define USER_DATA UINT64_C(0x401000)
#define USER_GUARD UINT64_C(0x402000)
#define USER_STACK UINT64_C(0x403000)
#define USER_TOP UINT64_C(0x404000)
#define USER_CS 0x33u
#define USER_SS 0x2bu
#define USER_MAX_WRITE 64u
int user_write_range(uint64_t address,uint64_t length);
int user_fault_expected(unsigned mode,uint64_t vector,uint64_t error,uint64_t cr2,uint64_t kernel_address);
#endif
