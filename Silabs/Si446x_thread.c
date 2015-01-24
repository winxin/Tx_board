#include "Si446x_thread.h"

volatile uint8_t Command_string[8]={};
volatile uint8_t Command=0;
BinarySemaphore Silabs_busy;
volatile uint32_t Active_Frequency;

void silabs_tube_up(BaseSequentialStream *chp, int argc, char *argv[]) {
	if (argc > 0) {
		chprintf(chp, "Tunes up by 50hz, Usage: u\r\n");
		return;
	}
	Command=1;
	chBSemSignal(&Silabs_busy);
	chBSemWait(&Silabs_busy);
}

void silabs_tube_down(BaseSequentialStream *chp, int argc, char *argv[]) {
	if (argc > 0) {
		chprintf(chp, "Tunes down by 50hz, Usage: d\r\n");
		return;
	}
	Command=2;
	chBSemSignal(&Silabs_busy);
	chBSemWait(&Silabs_busy);
}

void silabs_send_command(BaseSequentialStream *chp, int argc, char *argv[]) {
	if (argc != 1) {
		chprintf(chp, "Sends a packet, Usage: s <packet>\r\n");
		return;
	}
	if (strlen(argv[0]) != 6) {
		chprintf(chp, "<packet> must be exactly 6 characters\r\n");
		return;
	}
	RF_switch(1);
	uint8_t packet_len
	strcpy(Command_string,argv[0]);
	Command=3;	
	chBSemSignal(&Silabs_busy);
	chBSemWait(&Silabs_busy);
	RF_switch(0);
}

/* 
 * RF switch driver function, this function is blocking and is called by the shell handler. Pulses the output for 40ms.  
 */
void RF_switch(uint8_t state) {
	if(state)
		palSetPad(GPIOB, GPIOB_RFSWITCH_A);
	else
		palSetPad(GPIOB, GPIOB_RFSWITCH_B);
	chThdSleepMilliseconds(35);/*Spec is 30ms switching time max*/
	palClearPad(GPIOB, GPIOB_RFSWITCH_A);
	palClearPad(GPIOB, GPIOB_RFSWITCH_B);/*Clear both the pins*/
}

/*
 * Si446x thread, times are in milliseconds.
 */
static THD_WORKING_AREA(waThreadSI, 1024);
static __attribute__((noreturn)) THD_FUNCTION(SI_Thread, arg) {

  (void)arg;
  chRegSetThreadName("si4432");
  /* Configuration goes here - setup the PLL carrier, TX modem settings and the Packet handler Tx functionality*/


  while (TRUE) {//Main loop either retunes or sends strings, uses a volatile global to pass string pointers, special strings 'u' and 'd'. Callback via semaphore
	chBSemWait(&Silabs_busy);/*Wait for something to happen...*/
	/*Process the comms here - SPI transactions to either load packet and send or tune up/down*/
	if(Command==1)
		Active_Frequency+=50;
	else if(Command==2)
		Active_Frequency-=50;
	else if(Command==3) {/*Load the string into the packet handler*/


	}
	if(Command && Command<3) {/*Load the frequency into the PLL*/



	}
	chBSemSignal(&Silabs_busy);
  }
}

/**
  * @brief  This function spawns the si446x control thread
  * @param  void
  * @retval thread pointer to the spawned thread
  */
Thread* Spawn_Si446x_Thread(void) {
	chBSemInit(&Silabs_busy,FALSE);/*Init it as not taken*/
	/*
	* Creates the thread. Thread has priority slightly above normal and takes no argument
	*/
	return chThdCreateStatic(waThreadSI, sizeof(waThreadSI), NORMALPRIO+1, SI_Thread, (void*)NULL);
}
