#include <stdlib.h>
#include <stdint.h>
#include "ch.h"
#include "board.h"

void silabs_tube_up(BaseSequentialStream *chp, int argc, char *argv[]);
void silabs_tube_down(BaseSequentialStream *chp, int argc, char *argv[]);
void silabs_send_command(BaseSequentialStream *chp, int argc, char *argv[]);
void RF_switch(uint8_t state);
Thread* Spawn_Si446x_Thread(void);
