#include "Si446x_thread.h"

volatile uint8_t Command_string[8]={};
volatile uint8_t Command=0;
BinarySemaphore Silabs_busy;
volatile uint32_t Active_Frequency=434075000;
volatile uint8_t Active_level=32;	/*Approx 15dBm*/

static void spicb(SPIDriver *spip);

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
 * SPI end transfer callback.
 */
static void spicb(SPIDriver *spip) {
  /* On transfer end just releases the slave select line.*/
  chSysLockFromIsr();
  spiUnselectI(spip);
  chSysUnlockFromIsr();
}

/*
 * SPI configuration structure.
 * Maximum speed/2 (6MHz), CPHA=0, CPOL=0, 8bits frames, MSb transmitted first.
 * The slave select line is the pin GPIOA_SPI1NSS on the port GPIOA.
 */
static const SPIConfig spicfg = {
  spicb,
  /* HW dependent part.*/
  GPIOA,
  GPIOA_NSEL,
  SPI_CR1_MSTR | SPI_CR1_BR_0
};

/*
 * Si446x spi comms - blocking using the DMA driver from ChibiOS
*/
void si446x_spi( uint8_t tx_bytes, uint8_t* tx_buff, uint8_t rx_bytes, uint8_t* rx_buff){
	uint8_t dummy_buffer[20]={};/*For dummy data*/
	spiSelect(&SPID1); /* Slave Select assertion. */
	spiExchange(&SPID1, tx_bytes, tx_buff, dummy_buffer); /* Atomic transfer operations. */
	spiUnselect(spip); /* Slave Select de-assertion. */
	dummy_buffer[0]=0x44;/*Silabs read command*/
	while(!palReadPad(GPIOB, GPIOB_CTS)){chThdSleepMicrosecond(20);}/*Wait for CTS high*/
	if(rx_bytes) {
		spiSelect(&SPID1); /* Slave Select assertion. */
		spiExchange(&SPID1, rx_bytes, dummy_buffer, rx_buff); /* Atomic transfer operations. */
		spiUnselect(spip); /* Slave Select de-assertion. */
	}
}

/**
  * @brief  This function sets silabs center frequency
  * @param  Center frequency in Hz
  * @retval None
  */
void si446x_set_frequency(uint32_t freq) {/*Set the output divider according to recommended ranges given in Si446x datasheet*/
	uint8_t band = 0;
	uint8_t tx_buffer[16];
	uint8_t rx_buffer[2];
	if (freq < 705000000UL) { Outdiv = 6; band = 1;};
	if (freq < 525000000UL) { Outdiv = 8; band = 2;};
	if (freq < 353000000UL) { Outdiv = 12; band = 3;};
	if (freq < 239000000UL) { Outdiv = 16; band = 4;};
	if (freq < 177000000UL) { Outdiv = 24; band = 5;};
	uint32_t f_pfd = 2 * VCXO_FREQ / Outdiv;
	uint32_t n = ((uint32_t)(freq / f_pfd)) - 1;
	float ratio = (float)freq / (float)f_pfd;
	float rest = ratio - (float)n;
	uint32_t m = (unsigned long)(rest * 524288UL);
	// set the band parameter
	uint32_t sy_sel = 8;
	memcpy(tx_buffer, (uint8_t [5]){0x11, 0x20, 0x01, 0x51, (band + sy_sel)}, 5*sizeof(uint8_t));
	si446x_spi( 5, tx_buffer, 0, rx_buffer);
	// Set the pll parameters
	uint32_t m2 = m / 0x10000;
	uint32_t m1 = (m - m2 * 0x10000) / 0x100;
	uint32_t m0 = (m - m2 * 0x10000 - m1 * 0x100);
	uint32_t channel_increment = 524288 * Outdiv * Active_shift / (2 * VCXO_FREQ);
	uint8_t c1 = channel_increment / 0x100;
	uint8_t c0 = channel_increment - (0x100 * c1);
	memcpy(tx_buffer, (uint8_t [10]){0x11, 0x40, 0x06, 0x00, n, m2, m1, m0, c1, c0}, 10*sizeof(uint8_t));
	si446x_spi( 10, tx_buffer, 0, rx_buffer);
	// Set the Power
	memcpy(tx_buffer, (uint8_t [5]){0x11, 0x22, 0x01, 0x01, Active_level}, 5*sizeof(uint8_t));
	si446x_spi( 5, tx_buffer, 0, rx_buffer);
}

/**
  * @brief  This function sets silabs deviation and channel spacing
  * @param  Deviation and spacing in Hz, BPS in bit units
  * @retval None
  */
void si446x_set_deviation_channel_bps(uint32_t deviation, uint32_t channel_space, uint32_t bps) {
	uint8_t tx_buffer[16];
	uint8_t rx_buffer[2];
	//Make sure that Si446x::sendFrequencyToSi446x() was called before this function, so that we have set the global variable 'Outdiv' properly
	//Outdiv = 8;
	float units_per_hz = (( 0x40000 * Outdiv ) / (float)VCXO_FREQ);
	// Set deviation for RTTY
	uint32_t modem_freq_dev = (uint32_t)(units_per_hz * deviation / 2.0 );
	uint32_t mask = 0b11111111;
	uint8_t modem_freq_dev_0 = mask & modem_freq_dev;
	uint8_t modem_freq_dev_1 = mask & (modem_freq_dev >> 8);
	uint8_t modem_freq_dev_2 = mask & (modem_freq_dev >> 16);
	memcpy(tx_buffer, (uint8_t [7]){0x11, 0x20, 0x03, 0x0A, modem_freq_dev_2, modem_freq_dev_1, modem_freq_dev_0}, 7*sizeof(uint8_t));
	si446x_spi( 7, tx_buffer, 0, rx_buffer);
	uint32_t channel_spacing = (uint32_t)(units_per_hz * channel_space / 2.0 );
	modem_freq_dev_0 = mask & channel_spacing ;
	modem_freq_dev_1 = mask & (channel_spacing >> 8);
	memcpy(tx_buffer, (uint8_t [6]){0x11, 0x40, 0x02, 0x04, modem_freq_dev_1, modem_freq_dev_0}, 6*sizeof(uint8_t));
	si446x_spi( 6, tx_buffer, 0, rx_buffer);
	bps*=10;		/*From WDS settings, modem speed is in 0.1bps units*/
	modem_freq_dev_0 = mask & bps;
	modem_freq_dev_1 = mask & (bps >> 8);
	modem_freq_dev_2 = mask & (bps >> 16);
	memcpy(tx_buffer, (uint8_t [7]){0x11, 0x20, 0x03, 0x03, modem_freq_dev_2, modem_freq_dev_1, modem_freq_dev_0}, 7*sizeof(uint8_t));
	si446x_spi( 7, tx_buffer, 0, rx_buffer);
}

/*
 * Si446x thread, times are in milliseconds.
 */
static THD_WORKING_AREA(waThreadSI, 1024);
static __attribute__((noreturn)) THD_FUNCTION(SI_Thread, arg) {

  (void)arg;
  chRegSetThreadName("si4432");
  /* Configuration goes here - setup the PLL carrier, TX modem settings and the Packet handler Tx functionality*/
	/*
	* Initializes the SPI driver 1.
	*/
	spiStart(&SPID1, &spicfg);
	/* Reset the radio */
	SDN_HIGH;
	chThdSleepMilliseconds(10);
	SDN_LOW;						/*Radio is now reset*/
	chThdSleepMilliseconds(10)				/*Wait another 10ms to boot*/
	while(!palReadPad(GPIOB, GPIOB_CTS)){chThdSleepMilliseconds(10);}/*Wait for CTS high after POR*/
	/* Configure the radio ready for use, use simple busy wait logic here, as only has to happen once */
	uint8_t part=0;
	{
	uint8_t tx_buffer[16];
	uint8_t rx_buffer[12];
	//divide VCXO_FREQ into its bytes; MSB first
	uint8_t x3 = VCXO_FREQ / 0x1000000;
	uint8_t x2 = (VCXO_FREQ - (uint32_t)x3 * 0x1000000) / 0x10000;
	uint8_t x1 = (VCXO_FREQ - (uint32_t)x3 * 0x1000000 - (uint32_t)x2 * 0x10000) / 0x100;
	uint8_t x0 = (VCXO_FREQ - (uint32_t)x3 * 0x1000000 - (uint32_t)x2 * 0x10000 - (uint32_t)x1 * 0x100); 
	memcpy(tx_buffer, (uint8_t [7]){0x02, 0x01, 0x01, x3, x2, x1, x0}, 7*sizeof(uint8_t));
	/*Now send the command over SPI1*/
	si446x_spi( 7, tx_buffer, 2, rx_buffer);
	while(GET_NIRQ|(!palReadPad(GPIOB, GBIOB_POR))){chThdSleepMilliseconds(10);}/*Wait for NIRQ low and POR high*/
	memcpy(tx_buffer, (uint8_t [4]){0x20, 0x00, 0x00, 0x00}, 4*sizeof(uint8_t));/*Clear all interrupts*/
	si446x_spi( 4, tx_buffer, 0, NULL);
	memcpy(tx_buffer, (uint8_t [2]){0x01, 0x01}, 2*sizeof(uint8_t));
	si446x_spi( 2, tx_buffer, 12, rx_buffer);
	part=rx_buffer[3];//Should be 0x44
	//Only enable the packet received interrupt - global interrupt config and PH interrupt config bytes
	memcpy(tx_buffer, (uint8_t [6]){0x11, 0x01, 0x02, 0x00, 0x01, 0x10}, 6*sizeof(uint8_t));
	si446x_spi( 6, tx_buffer, 0, NULL);
	//Setup the default frequency and power
	si446x_set_frequency(Active_Frequency);
	//Setup default channel config
	si446x_set_deviation_channel_bps(300, 3000, 200);
	}
  while (TRUE) {//Main loop either retunes or sends strings, uses a volatile global to pass string pointers, special strings 'u' and 'd'. Callback via semaphore
	chBSemWait(&Silabs_busy);/*Wait for something to happen...*/
	/*Process the comms here - SPI transactions to either load packet and send or tune up/down*/
	if(Command==1)
		Active_Frequency+=50;
	else if(Command==2)
		Active_Frequency-=50;
	else if(Command==3) {/*Load the string into the packet handler*/
		tx_buff[0]=0x66;/*The load to FIFO command*/
		strcpy(&tx_buff[1],Command_string);/*Followed by the payload*/
		si446x_spi( strlen(Command_string)+1, tx_buffer, 0, rx_buffer);
		/*Now go to TX mode, with return to ready mode on completion, always use channel 0, use Packet handler settings for the data length*/
		memcpy(tx_buffer, (uint8_t [5]){0x31, 0x00, 0x30, 0x00, 0x00}, 5*sizeof(uint8_t));
		si446x_spi( 5, tx_buffer, 0, rx_buffer);
	}
	if(Command && Command<3) /*Load the frequency into the PLL*/
		si446x_set_frequency(Active_Frequency);
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
