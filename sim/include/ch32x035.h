/* Host-simulator stand-in for the WCH device header. */
#ifndef SIM_CH32X035_H
#define SIM_CH32X035_H
#include <stdint.h>
typedef struct { volatile uint32_t BCR, BSHR; } GPIO_TypeDef;
extern GPIO_TypeDef *GPIOB;
#define GPIO_Pin_0  0x0001U
#define GPIO_Pin_1  0x0002U
#define GPIO_Pin_3  0x0008U
#define GPIO_Pin_6  0x0040U
#define GPIO_Pin_7  0x0080U
#define GPIO_Pin_9  0x0200U
#define GPIO_Pin_10 0x0400U
#define GPIO_Pin_11 0x0800U
#define GPIO_Pin_16 0x10000U
#define GPIO_Pin_17 0x20000U

/* Flash ROM API used by settings.c (sim/settings_test.c provides them). */
typedef enum { FLASH_BUSY = 1, FLASH_ERROR_PG, FLASH_ERROR_WRP, FLASH_COMPLETE,
               FLASH_TIMEOUT, FLASH_ALIGN_ERROR } FLASH_Status;
FLASH_Status FLASH_ROM_ERASE(uint32_t StartAddr, uint32_t Length);
FLASH_Status FLASH_ROM_WRITE(uint32_t StartAddr, uint32_t *pbuf, uint32_t Length);
const void *sim_flash_ptr(uint32_t addr);
static inline void __disable_irq(void) {}
static inline void __enable_irq(void) {}
#endif
