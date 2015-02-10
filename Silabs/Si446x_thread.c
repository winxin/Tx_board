#include "Si446x_thread.h"

volatile uint8_t Command_string[8]={};
volatile uint8_t Command=0;
binary_semaphore_t Silabs_busy,Silabs_callback;
volatile uint32_t Active_Frequency=434075000;
volatile uint8_t Active_level=32;	/*Approx 15dBm*/
volatile uint16_t Active_shift=300;	/*300hz*/
uint8_t Active_banddiv=10;
int8_t Outdiv = 8;

const uint8_t Silabs_Header[4]="$$RO";

volatile uint16_t Silabs_Part_ID=0;	/*Used to check that the device is functional*/

uint8_t tx_buffer[16];			/*Globals used as comms buffers*/
uint8_t rx_buffer[12];

#define VCXO_FREQ 26000000UL
#define RSSI_THRESH -100

static void spicb(SPIDriver *spip);

void silabs_tune_up(BaseSequentialStream *chp, int argc, char *argv[]) {
	if (argc > 0) {
		chprintf(chp, "Tunes up by 50hz, Usage: u\r\n");
		return;
	}
	Command=1;
	chBSemSignal(&Silabs_busy);
	if(MSG_OK == chBSemWaitTimeout(&Silabs_callback, MS2ST(1000))) {
		chprintf(chp, "Frequency is: %u\r\n",Active_Frequency);
	}
}

void silabs_tune_down(BaseSequentialStream *chp, int argc, char *argv[]) {
	if (argc > 0) {
		chprintf(chp, "Tunes down by 50hz, Usage: d\r\n");
		return;
	}
	Command=2;
	chBSemSignal(&Silabs_busy);
	if(MSG_OK == chBSemWaitTimeout(&Silabs_callback, MS2ST(1000))) {
		chprintf(chp, "Frequency is: %u\r\n",Active_Frequency);
	}
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
	strncpy(Command_string,argv[0],6);
	Command=3;	
	chBSemSignal(&Silabs_busy);
	chBSemWaitTimeout(&Silabs_callback, MS2ST(1000));	
}

void silabs_get_part_id(BaseSequentialStream *chp, int argc, char *argv[]) {
	if (argc) {
		chprintf(chp, "Gets part ID, Usage: p \r\n");
		return;
	}
	chprintf(chp, "%4X\r\n",Silabs_Part_ID);
}

/* 
 * RF switch driver function, this function is blocking and is called by the shell handler. Pulses the output for 40ms.  
 */
void RF_switch(uint8_t state) {
	if(state==2)
		palSetPad(GPIOB, GPIOB_RFSWITCH_A);
	else if(state)
		palSetPad(GPIOA, GPIOA_RFSWITCH_B);
	else {
		palClearPad(GPIOB, GPIOB_RFSWITCH_A);
		palClearPad(GPIOA, GPIOA_RFSWITCH_B);/*Clear both the pins*/
	}
	if(state) 		/*Spec is 30ms switching time max*/
		gptStartOneShot(&GPTD3, 35);
}


/*
 * SPI end transfer callback.
 */
static void spicb(SPIDriver *spip) {
  /* On transfer end just releases the slave select line.*/
  chSysLockFromISR();
  spiUnselectI(spip);
  chSysUnlockFromISR();
}

/*
 * SPI configuration structure.
 * Maximum speed/4 (6MHz), CPHA=0, CPOL=0, 8bits frames, MSb transmitted first.
 * The slave select line is the pin GPIOA_SPI1NSS on the port GPIOA.
 */
static const SPIConfig spicfg = {
  spicb,
  /* HW dependent part.*/
  GPIOA,
  GPIOA_NSEL,
  SPI_CR1_MSTR | SPI_CR1_BR_0
};

static void switch_output_callback(GPTDriver *gpt_ptr) {
	RF_switch(2);
}

static void switch_off_callback(GPTDriver *gpt_ptr) {
	RF_switch(0);
}

static GPTConfig gpt4cfg =
{
    1000,                    /* timer clock.*/
    switch_output_callback    /* Timer callback.*/
};

static GPTConfig gpt3cfg =
{
    1000,                    /* timer clock.*/
    switch_off_callback    /* Timer callback.*/
};

/*
 * Si446x spi comms - blocking using the DMA driver from ChibiOS
*/
uint8_t si446x_spi( uint8_t tx_bytes, uint8_t* tx_buff, uint8_t rx_bytes, uint8_t* rx_buff){
	uint8_t dummy_buffer[20]={};/*For dummy data*/
	spiSelect(&SPID1); /* Slave Select assertion. */
	spiExchange(&SPID1, tx_bytes, tx_buff, dummy_buffer); /* Atomic transfer operations. */
	spiUnselect(&SPID1); /* Slave Select de-assertion. */
	dummy_buffer[0]=0x44;/*Silabs read command*/
	uint32_t millis = MS2ST(chVTGetSystemTime());
	while(!palReadPad(GPIOB, GPIOB_CTS)){
		chThdSleepMicroseconds(20);
		if((MS2ST(chVTGetSystemTime())-millis)>10){/*Silabs stalled*/
			return 1;		
		}
	}/*Wait for CTS high*/
	if(rx_bytes) {
		spiSelect(&SPID1); /* Slave Select assertion. */
		spiExchange(&SPID1, rx_bytes, dummy_buffer, rx_buff); /* Atomic transfer operations. */
		spiUnselect(&SPID1); /* Slave Select de-assertion. */
	}
	return 0;
}

/**
  * @brief  This function sets silabs center frequency
  * @param  Center frequency in Hz
  * @retval None
  */
uint8_t si446x_set_frequency(uint32_t freq) {/*Set the output divider according to recommended ranges given in Si446x datasheet*/
	uint8_t band = 0;
	uint8_t tx_buffer[16];
	uint8_t rx_buffer[2];
	uint8_t failure=0;
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
	Active_banddiv=sy_sel+band;
	memcpy(tx_buffer, (uint8_t [5]){0x11, 0x20, 0x01, 0x51, Active_banddiv}, 5*sizeof(uint8_t));
	failure=si446x_spi( 5, tx_buffer, 0, rx_buffer);
	// Set the pll parameters
	uint32_t m2 = m / 0x10000;
	uint32_t m1 = (m - m2 * 0x10000) / 0x100;
	uint32_t m0 = (m - m2 * 0x10000 - m1 * 0x100);
	uint32_t channel_increment = 524288 * Outdiv * Active_shift / (2 * VCXO_FREQ);
	uint8_t c1 = channel_increment / 0x100;
	uint8_t c0 = channel_increment - (0x100 * c1);
	memcpy(tx_buffer, (uint8_t [10]){0x11, 0x40, 0x06, 0x00, n, m2, m1, m0, c1, c0}, 10*sizeof(uint8_t));
	failure|=si446x_spi( 10, tx_buffer, 0, rx_buffer);
	// Set the Power
	memcpy(tx_buffer, (uint8_t [5]){0x11, 0x22, 0x01, 0x01, Active_level}, 5*sizeof(uint8_t));
	failure|=si446x_spi( 5, tx_buffer, 0, rx_buffer);
	return failure;
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
	float units_per_hz = ((float)( 0x40000 * Outdiv ) / (float)VCXO_FREQ);
	// Set deviation for RTTY
	uint32_t modem_freq_dev = (uint32_t)(units_per_hz * (float)deviation / 2.0 );
	uint32_t mask = 0b11111111;
	uint8_t modem_freq_dev_0 = mask & modem_freq_dev;
	uint8_t modem_freq_dev_1 = mask & (modem_freq_dev >> 8);
	uint8_t modem_freq_dev_2 = mask & (modem_freq_dev >> 16);
	memcpy(tx_buffer, (uint8_t [7]){0x11, 0x20, 0x03, 0x0A, modem_freq_dev_2, modem_freq_dev_1, modem_freq_dev_0}, 7*sizeof(uint8_t));
	si446x_spi( 7, tx_buffer, 0, rx_buffer);
	uint32_t channel_spacing = (uint32_t)(units_per_hz * channel_space);
	modem_freq_dev_0 = mask & channel_spacing ;
	modem_freq_dev_1 = mask & (channel_spacing >> 8);
	memcpy(tx_buffer, (uint8_t [6]){0x11, 0x40, 0x02, 0x04, modem_freq_dev_1, modem_freq_dev_0}, 6*sizeof(uint8_t));
	si446x_spi( 6, tx_buffer, 0, rx_buffer);
	bps*=Active_banddiv;		/*From WDS settings, modem speed is in 0.1bps units, but it seems to scale with the frequency, for Manchester is data bps*/
	modem_freq_dev_0 = mask & bps;
	modem_freq_dev_1 = mask & (bps >> 8);
	modem_freq_dev_2 = mask & (bps >> 16);
	//divide VCXO_FREQ into its bytes; MSB first, this is needed for the NCO frequency for Tx mode - equal to the xtal for <200kbps
	uint8_t x3 = VCXO_FREQ / 0x1000000;
	uint8_t x2 = (VCXO_FREQ - (uint32_t)x3 * 0x1000000) / 0x10000;
	uint8_t x1 = (VCXO_FREQ - (uint32_t)x3 * 0x1000000 - (uint32_t)x2 * 0x10000) / 0x100;
	uint8_t x0 = (VCXO_FREQ - (uint32_t)x3 * 0x1000000 - (uint32_t)x2 * 0x10000 - (uint32_t)x1 * 0x100); 
	memcpy(tx_buffer, (uint8_t [11]){0x11, 0x20, 0x07, 0x03, modem_freq_dev_2, modem_freq_dev_1, modem_freq_dev_0, x3, x2, x1, x0},11*sizeof(uint8_t));
	si446x_spi( 11, tx_buffer, 0, rx_buffer);
}

/**
  * @brief  This function sets silabs Rx modem config, its pretty much an exact copy of the balloon payload settings, only with packet for Tx. 
  * Tx Guassian/NCO settings not used, not sure if these might need to be added?
  * @param  None
  * @retval None
  */
void si446x_set_modem(void) {
	//Set to CW mode
	//Sets modem into direct asynchronous 2FSK mode using packet handler (default config is ok here), no Manchester
	memcpy(tx_buffer, (uint8_t [6]){0x11, 0x20, 0x02, 0x00, 0x02, 0x00}, 6*sizeof(uint8_t));
	si446x_spi( 5, tx_buffer, 0, rx_buffer);
	//Also configure the RX packet CRC stuff here, 6 byte payload for FIELD1, using CRC and CRC check for rx with seed, and 2FSK (note shared register area)
	memcpy(tx_buffer, (uint8_t [7]){0x11, 0x12, 0x03, 0x0E, 0x06, 0x00, 0xAA}, 7*sizeof(uint8_t));
	si446x_spi( 7, tx_buffer, 0, rx_buffer);
	//Configure the rx signal path, these setting are from WDS - lower the IF slightly and setup the CIC Rx filter
	memcpy(tx_buffer, (uint8_t [15]){0x11, 0x20, 0x0B, 0x19, 0x80, 0x08, 0x03, 0x80, 0x00, 0xF0, 0x10, 0x74, 0xE8, 0x00, 0x55}, 15*sizeof(uint8_t));
	si446x_spi( 15, tx_buffer, 0, rx_buffer);
	//Configure BCR - NCO settings for the RX signal path - WDS settings
	memcpy(tx_buffer, (uint8_t [16]){0x11, 0x20, 0x0C, 0x24, 0x06, 0x0C, 0xAB, 0x03, 0x03, 0x02, 0xC2, 0x00, 0x04, 0x32, 0xC0, 0x01}, 16*sizeof(uint8_t));
	si446x_spi( 16, tx_buffer, 0, rx_buffer);
	//Configure AFC/AGC settings for Rx path, WDS settings - only change the AFC here, as the other settings are only slightly tweaked by WDS
	memcpy(tx_buffer, (uint8_t [7]){0x11, 0x20, 0x03, 0x30, 0x03, 0x64, 0xC0}, 7*sizeof(uint8_t));//This just sets AFC limiter values
	si446x_spi( 7, tx_buffer, 0, rx_buffer);
	//Configure Rx search period control - WDS settings, note that the second setting can be overwritten if the frequency is changed
	memcpy(tx_buffer, (uint8_t [6]){0x11, 0x20, 0x02, 0x50, 0x84, 0x0A}, 6*sizeof(uint8_t));
	si446x_spi( 6, tx_buffer, 0, rx_buffer);
	//Configure Rx BCR and AFC config - WDS settings
	memcpy(tx_buffer, (uint8_t [6]){0x11, 0x20, 0x02, 0x54, 0x0F, 0x07}, 6*sizeof(uint8_t));
	si446x_spi( 6, tx_buffer, 0, rx_buffer);
	//Configure signal arrival detect - WDS settings
	memcpy(tx_buffer, (uint8_t [9]){0x11, 0x20, 0x05, 0x5B, 0x40, 0x04, 0x21, 0x78, 0x20}, 9*sizeof(uint8_t));
	si446x_spi( 9, tx_buffer, 0, rx_buffer);
	//Configure first and second set of Rx filter coefficients - WDS settings
	memcpy(tx_buffer, (uint8_t [16]){0x11, 0x21, 0x0C, 0x00, 0xFF, 0xBA, 0x0F, 0x51, 0xCF, 0xA9, 0xC9, 0xFC, 0x1B, 0x1E, 0x0F, 0x01}, 16*sizeof(uint8_t));
	si446x_spi( 9, tx_buffer, 0, rx_buffer);
	memcpy(tx_buffer, (uint8_t [16]){0x11, 0x21, 0x0C, 0x0C, 0xFC, 0xFD, 0x15, 0xFF, 0x00, 0x0F, 0xFF, 0xBA, 0x0F, 0x51, 0xCF, 0xA9}, 16*sizeof(uint8_t));
	si446x_spi( 9, tx_buffer, 0, rx_buffer);
	memcpy(tx_buffer, (uint8_t [16]){0x11, 0x21, 0x0C, 0x18, 0xC9, 0xFC, 0x1B, 0x1E, 0x0F, 0x01, 0xFC, 0xFD, 0x15, 0xFF, 0x00, 0x0F}, 16*sizeof(uint8_t));
	si446x_spi( 9, tx_buffer, 0, rx_buffer);
	//Configure the RSSI thresholding for RX mode, with 12dB jump threshold (reset if RSSI changes this much during Rx), RSSI mean with packet toggle
	//RSSI_THRESH is in dBm, it needs to be converted to 0.5dBm steps offset by ~130
	uint8_t rssi = (2*(RSSI_THRESH+130))&0xFF;
	memcpy(tx_buffer, (uint8_t [8]){0x11, 0x20, 0x04, 0x4A, rssi, 0x0C, 0x12, 0x3E}, 8*sizeof(uint8_t));
	si446x_spi( 8, tx_buffer, 0, rx_buffer);
	//Configure the match value, this constrains the first 4 bytes of data to match e.g. $$RO
	memcpy(tx_buffer, (uint8_t [16]){0x11, 0x30, 0x0C, 0x00,Silabs_Header[0], 0xFF, 0x41,Silabs_Header[1], 0xFF, 0x42,Silabs_Header[2], 0xFF, 0x43,Silabs_Header[3], 0xFF, 0x44}, 16*sizeof(uint8_t));
	si446x_spi( 16, tx_buffer, 0, rx_buffer);
	//Configure the Packet handler to (NOT use seperate FIELD config for RX), and turn off after packet rx
	memcpy(tx_buffer, (uint8_t [5]){0x11, 0x12, 0x01, 0x06, 0x00}, 5*sizeof(uint8_t));
	si446x_spi( 5, tx_buffer, 0, rx_buffer);
	//Use CCIT-16 CRC with 0xFFFF seed on the packet handler, same as UKHAS protocol
	memcpy(tx_buffer, (uint8_t [5]){0x11, 0x12, 0x01, 0x00, 0x85}, 5*sizeof(uint8_t));
	si446x_spi( 5, tx_buffer, 0, rx_buffer);
	//Use bytes for preamble length - so defaults to 8bytes
	memcpy(tx_buffer, (uint8_t [5]){0x11, 0x10, 0x01, 0x04, 0x31}, 5*sizeof(uint8_t));
	si446x_spi( 5, tx_buffer, 0, rx_buffer);
	//Set the sync word as two bytes 0xD391, this has good autocorrelation 8/1 peak to secondary ratio, default config used, no bit errors, 16 bit
	memcpy(tx_buffer, (uint8_t [6]){0x11, 0x11, 0x02, 0x01, 0xD3, 0x91}, 6*sizeof(uint8_t));
	si446x_spi( 6, tx_buffer, 0, rx_buffer);
}

void si446x_initialise(void) {
	/* Reset the radio */
	SDN_HIGH;
	chThdSleepMilliseconds(10);
	SDN_LOW;						/*Radio is now reset*/
	chThdSleepMilliseconds(10);				/*Wait another 10ms to boot*/
	while(!palReadPad(GPIOB, GPIOB_CTS)){chThdSleepMilliseconds(10);}/*Wait for CTS high after POR*/
	/* Configure the radio ready for use, use simple busy wait logic here, as only has to happen once */
	uint8_t part=0;
	//divide VCXO_FREQ into its bytes; MSB first
	uint8_t x3 = VCXO_FREQ / 0x1000000;
	uint8_t x2 = (VCXO_FREQ - (uint32_t)x3 * 0x1000000) / 0x10000;
	uint8_t x1 = (VCXO_FREQ - (uint32_t)x3 * 0x1000000 - (uint32_t)x2 * 0x10000) / 0x100;
	uint8_t x0 = (VCXO_FREQ - (uint32_t)x3 * 0x1000000 - (uint32_t)x2 * 0x10000 - (uint32_t)x1 * 0x100); 
	memcpy(tx_buffer, (uint8_t [7]){0x02, 0x01, 0x01, x3, x2, x1, x0}, 7*sizeof(uint8_t));
	/*Now send the command over SPI1*/
	si446x_spi( 7, tx_buffer, 2, rx_buffer);
	while(GET_NIRQ|(!palReadPad(GPIOB, GPIOB_POR))){chThdSleepMilliseconds(10);}/*Wait for NIRQ low and POR high*/
	memcpy(tx_buffer, (uint8_t [4]){0x20, 0x00, 0x00, 0x00}, 4*sizeof(uint8_t));/*Clear all interrupts*/
	si446x_spi( 4, tx_buffer, 0, NULL);
	memcpy(tx_buffer, (uint8_t [2]){0x01, 0x01}, 2*sizeof(uint8_t));
	si446x_spi( 2, tx_buffer, 12, rx_buffer);
	part=rx_buffer[3];//Should be 0x44
	Silabs_Part_ID=*((uint16_t*)(&rx_buffer[3]));/* This can now be used to check that part */
	//Only enable the packet received interrupt - global interrupt config and PH interrupt config bytes
	memcpy(tx_buffer, (uint8_t [6]){0x11, 0x01, 0x02, 0x00, 0x01, 0x10}, 6*sizeof(uint8_t));
	si446x_spi( 6, tx_buffer, 0, NULL);
	//Setup the default frequency and power
	si446x_set_frequency(Active_Frequency);
	//Setup default channel config
	si446x_set_deviation_channel_bps(300, 3000, 200);
	si446x_set_modem();
}

/*
 * Si446x thread, times are in milliseconds.
 */
static THD_WORKING_AREA(waThreadSI, 1024);
static __attribute__((noreturn)) THD_FUNCTION(SI_Thread, arg) {

  (void)arg;
  chRegSetThreadName("si4432");
	uint8_t si446x_failure=0;
  /* Configuration goes here - setup the PLL carrier, TX modem settings and the Packet handler Tx functionality*/
	/*
	* Initializes the SPI driver 1.
	*/
	spiStart(&SPID1, &spicfg);
	si446x_initialise();
	gptInit();
	gptStart(&GPTD4, &gpt4cfg);
	gptStart(&GPTD3, &gpt3cfg);
	RF_switch(2);//Put switch in the Rx configuration
  while (TRUE) {//Main loop either retunes or sends strings, uses a volatile global to pass string pointers, special strings 'u' and 'd'. Callback via semaphore
	if(MSG_OK == chBSemWaitTimeout(&Silabs_busy, MS2ST(100))) {/*Wait for something to happen...*/
		/*Process the comms here - SPI transactions to either load packet and send or tune up/down*/
		if(Command==1)
			Active_Frequency+=50;
		else if(Command==2)
			Active_Frequency-=50;
		else if(Command==3) {/*Load the string into the packet handler*/
			RF_switch(1);/*Turn the Agilent RF switch to relay the data*/
			chThdSleepMilliseconds(40);/*Wait for the switch to activate before proceeding*/
			tx_buffer[0]=0x66;/*The load to FIFO command*/
			strncpy(&(tx_buffer[1]),Command_string,6);/*Followed by the payload*/
			si446x_failure|=si446x_spi( strlen(Command_string)+1, tx_buffer, 0, rx_buffer);
			/*Now go to TX mode, with return to ready mode on completion, always use channel 0, use Packet handler settings for the data length*/
			memcpy(tx_buffer, (uint8_t [5]){0x31, 0x00, 0x30, 0x00, 0x00}, 5*sizeof(uint8_t));
			si446x_failure|=si446x_spi( 5, tx_buffer, 0, rx_buffer);
			gptStartOneShot(&GPTD4, 900); // 0.9 seconds to send the packet
		}
		if(Command && Command<3) /*Load the frequency into the PLL*/
			si446x_failure|=si446x_set_frequency(Active_Frequency);
		if(si446x_failure) {	/*Try to recover if radio breaks*/
			chThdSleepMilliseconds(400);/*Wait in case radio can finish what it was doing*/
			si446x_initialise();
			si446x_failure=0;
		}
		chBSemSignal(&Silabs_callback);
	}
  }
}

/**
  * @brief  This function spawns the si446x control thread
  * @param  void
  * @retval thread pointer to the spawned thread
  */
thread_t* Spawn_Si446x_Thread(void) {
	chBSemObjectInit(&Silabs_busy,FALSE);/*Init it as not taken*/
	chBSemObjectInit(&Silabs_callback,FALSE);/*Init it as not taken*/
	/*
	* Creates the thread. Thread has priority slightly above normal and takes no argument
	*/
	return chThdCreateStatic(waThreadSI, sizeof(waThreadSI), NORMALPRIO+1, SI_Thread, (void*)NULL);
}
