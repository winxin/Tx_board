#include <stdlib.h>
#include <stdint.h>
#include "ch.h"
#include "board.h"

#define	SDN_HIGH 	palSetPad(GPIOB, GPIOB_SDN)
#define	SDN_LOW		palClearPad(GPIOB, GPIOB_SDN)

#define GET_NIRQ	palReadPad(GPIOB, GBIOB_NIRQ)

void silabs_tube_up(BaseSequentialStream *chp, int argc, char *argv[]);
void silabs_tube_down(BaseSequentialStream *chp, int argc, char *argv[]);
void silabs_send_command(BaseSequentialStream *chp, int argc, char *argv[]);
void RF_switch(uint8_t state);
Thread* Spawn_Si446x_Thread(void);
