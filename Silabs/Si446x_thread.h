#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "ch.h"
#include "chprintf.h"
#include "hal.h"
#include "board.h"

#define	SDN_HIGH 	palSetPad(GPIOB, GPIOB_SDN)
#define	SDN_LOW		palClearPad(GPIOB, GPIOB_SDN)

#define GET_NIRQ	palReadPad(GPIOB, GPIOB_NIRQ)

void silabs_tune_up(BaseSequentialStream *chp, int argc, char *argv[]);
void silabs_tune_down(BaseSequentialStream *chp, int argc, char *argv[]);
void silabs_send_command(BaseSequentialStream *chp, int argc, char *argv[]);
void silabs_get_part_id(BaseSequentialStream *chp, int argc, char *argv[]);
void silabs_tune_reset(BaseSequentialStream *chp, int argc, char *argv[]);
void silabs_set_channel(BaseSequentialStream *chp, int argc, char *argv[]);
void RF_switch(uint8_t state);
thread_t* Spawn_Si446x_Thread(void);
