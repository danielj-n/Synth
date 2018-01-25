// An unifinished version of synth code with commented out clutter removed and messy tabs fixed 


#include "sam.h"
#include "SynthLUTs.h"

#define MAX_NOTES 8		
#define NUM_OF_KEYS 32

#define DETUNE_DONE 0
#define SELECT_DETUNE_NOTE 1


volatile long tickCount  = 0;	
volatile bool volatileLock = false;

int LFOType = 2;
float LFOVolume = 1.0;
long tickLFOStarted = 0;
int LFOLength = 2000;				//TODO: Write function to calculate this

int oscType[] = {0, 0, 0, 0};
float oscVolume[] = {1.0, 0.0, 0.0, 0.0};
int oscDetune[] = {0, 0, 0, 0};
int oscToDetune = 0;
int octaveDetune = 0;
int detuneMode = DETUNE_DONE;

int envelope[] = {0, 0xFFFFFFF, 0xFFFFFFF};	// Attack, sustain, release (0xFFFFFFFF sustain to keep forever)

typedef struct
{
	bool deleted;
	int keyPlaying;
	int notePlaying;
	long tickAdded;
} key;

key keys[8]; 

bool keyStates[NUM_OF_KEYS];

int getNextAmplitude()
{
	// NOTES
	int noteLUTVals[MAX_NOTES][4];
	for (int keyNum = 0; keyNum < MAX_NOTES; keyNum++)
	{
		if (!keys[keyNum].deleted)
		{
			for (int oscNum = 0; oscNum < 4; oscNum++)
			{
				double noteLength = (double) NOTES_LUT[keys[keyNum].notePlaying + oscDetune[oscNum]]; 
				double ticksSinceNoteAdded = (double) (tickCount - keys[keyNum].tickAdded);
				noteLUTVals[keyNum][oscNum] = ((long) (noteLength*ticksSinceNoteAdded)) % 2000;
			}
		}
	}

	// LFO	
	double ticksSinceLFOStarted = (double) (tickCount - tickLFOStarted);
	int LFOLUTVal = (int) (((long) ((double) LFOLength) * ticksSinceLFOStarted)) % 2000;
	
	// ENVELOPE
	float totalAmplitude = 0;
	for (int keyNum = 0; keyNum < MAX_NOTES; keyNum++)
	{
		if (!keys[keyNum].deleted)
		{
			// Applying the envelope
			float envelopeVolume = 1;
			long ticksSinceKeyPressed = tickCount - keys[keyNum].tickAdded;
			if (ticksSinceKeyPressed < envelope[0])
			{
				envelopeVolume = ((float) ticksSinceKeyPressed) / ((float) envelope[0]);
			}
			else if (ticksSinceKeyPressed >= envelope[1] && ticksSinceKeyPressed < envelope[2])
			{
				envelopeVolume = 1.0 - ((float) ticksSinceKeyPressed - (float) envelope[1]) / ((float) (envelope[2] - envelope[1]));
			}
			else if (ticksSinceKeyPressed >= envelope[2])
			{
				envelopeVolume = 0;
			}

			// Adding the note
			for (int oscNum = 0; oscNum < 4; oscNum++)
			{
				totalAmplitude += oscVolume[oscNum] * envelopeVolume * ((float) (WAVE_LUTS[ oscType[oscNum] ][ noteLUTVals[keyNum][oscNum] ]));
			}
		}
	}

	// Applying LFO
	totalAmplitude *= 1.0 - (LFOVolume * ((float) (WAVE_LUTS[LFOType][LFOLUTVal])) / 127.0 );	// 127.00 for 7 bit, 255.0 for 8-bit, etc.

	// Return the total
	return (int) totalAmplitude;
}

void dispDebug(int toPrint)
{
	DACC->DACC_CDR = toPrint * 31;
}

void systemInit()
{
	// WATCHDOG DISABLE 
	
	WDT->WDT_MR |= WDT_MR_WDDIS;
	
	
	// CONFIGURE FLASH FOR HIGH SPEEDS

	EFC0->EEFC_FMR = (5 << EEFC_FMR_FWS_Pos) | EEFC_FMR_CLOE;
	
	
	// PROGRAMMING MASTER CLOCK
	
	PMC->CKGR_PLLBR |= (25 << CKGR_PLLBR_DIVB_Pos) | (25 << CKGR_PLLBR_MULB_Pos) | (1 << CKGR_MOR_MOSCXTST_Pos);
	
	while (!(PMC->PMC_SR & PMC_SR_LOCKB));

	PMC->PMC_MCKR |= PMC_MCKR_PRES_CLK_1;
	
	while(!(PMC->PMC_SR & PMC_SR_MCKRDY));
	
	PMC->PMC_MCKR = (~PMC_MCKR_CSS_Msk & PMC->PMC_MCKR) | PMC_MCKR_CSS_PLLB_CLK;
		
	while(!(PMC->PMC_SR & PMC_SR_MCKRDY));		
	
	
	// ENABLE PERIPHERAL CLOCKS
	
	PMC->PMC_PCER0 |= (1 << TC0_IRQn);
	
	PMC->PMC_PCER0 |= (1 << DACC_IRQn);
	
	PMC->PMC_PCER0 |= (1 << SPI_IRQn);
	

	// ENABLE TC0 (TODO: It works, but could be cleaned up)

	TC0->TC_CHANNEL[0].TC_CCR |= TC_CCR_CLKEN;		// Might want to put this at the END of this block
	
	TC0->TC_CHANNEL[0].TC_CMR |= TC_CMR_WAVE;
	
	TC0->TC_CHANNEL[0].TC_CMR |= (TC_CMR_TCCLKS_TIMER_CLOCK2 | TC_CMR_EEVT_XC0 | TC_CMR_WAVSEL_UP_RC);
	
	TC0->TC_CHANNEL[0].TC_RC = 32000;			// should be 3000 for 40KHZ tick @120MHZ
	
	TC0->TC_CHANNEL[0].TC_IER |= TC_IER_CPCS;
	
	TC0->TC_CHANNEL[0].TC_CCR |= TC_CCR_SWTRG;		// Software trigger may be unrequired, look into this
	
	TC0->TC_CHANNEL[0].TC_CCR &= ~TC_CCR_SWTRG;		// Software trigger reset may also be unrequired
	
	
	// Enable DAC
	
	DACC->DACC_CHER |= DACC_CHER_CH0;
	
	
	// OUTPUT PIN CONFIG

	PIOA->PIO_ODR &= ~PIO_ODR_P0;
	
	PIOA->PIO_OER |= PIO_ODR_P0;
	
	PIOA->PIO_OWDR &= ~PIO_OWDR_P0;	
	
	PIOA->PIO_OWER |= PIO_OWDR_P0;
	
	// SPI PIN CONFIG
	
	REG_PIOA_PDR |= PIO_PDR_P11;	//NPCS0
		
	REG_PIOA_PDR |= PIO_PDR_P12;	//MISO
		
	REG_PIOA_PDR |= PIO_PDR_P13;	//MOSI
		
	REG_PIOA_PDR |= PIO_PDR_P14;	//MOSI
	
	
	// CONFIGURE SPI
		
	SPI->SPI_MR |= SPI_MR_MSTR;
		
	SPI->SPI_CSR[0] |= 255 << SPI_CSR_SCBR_Pos;	// Sets clock div to 255 from peripheral
		
	SPI->SPI_CSR[0] |= SPI_CSR_BITS_16_BIT;
		
	SPI->SPI_CR |= SPI_CR_SPIEN;
	
	
	// ENABLE INTERRUPT
		
	NVIC->ISER[0] |= (1 << TC0_IRQn);
}

void powerKeyMatrix(int matrixNum)
{

}

void readKeyMatrix(bool* matrixState, int keyMatrixNum)
{
	matrixState[0] = 1;
	matrixState[1] = 0;
	matrixState[2] = 0;
	matrixState[3] = 0;
}

// Detunes
void selectDetuneNote(int keyChanging)
{
	volatileLock = true;
	oscDetune[oscToDetune] = octaveDetune + keyChanging - 16;	// 16 = middle c, don't know what it really is yet
	volatileLock = false;
	detuneMode = DETUNE_DONE;
}

void addKey(int keyChanging)
{
	bool needsToDelete = true;
	long smallestTickKeyNum = 0;
	for (int keyNum = 0; keyNum < MAX_NOTES && needsToDelete == true; keyNum++)
	{
		if (keys[keyNum].deleted)
		{
			needsToDelete = false;
			volatileLock = true;
			keys[keyNum].deleted = false;
			keys[keyNum].keyPlaying = keyChanging;
			keys[keyNum].notePlaying = KEYS_TO_NOTES_LUT[keyChanging] + octaveDetune;
			keys[keyNum].tickAdded = tickCount;
			volatileLock = false;
		}
		if (keys[keyNum].tickAdded < keys[smallestTickKeyNum].tickAdded)
		{
			smallestTickKeyNum = keyNum;
		}
	}
	if (needsToDelete == true)
	{
		volatileLock = true;
		keys[smallestTickKeyNum].deleted = false;
		keys[smallestTickKeyNum].keyPlaying = keyChanging;
		keys[smallestTickKeyNum].notePlaying = KEYS_TO_NOTES_LUT[keyChanging] + octaveDetune;
		keys[smallestTickKeyNum].tickAdded = tickCount;
		volatileLock = false;
	}
}

// Removes a key
void removeKey(int keyChanging)
{
	bool keyFound = false;
	for (int keyNum = 0; keyNum < MAX_NOTES && keyFound == false; keyNum++)
	{
		if (!keys[keyNum].deleted && keys[keyNum].keyPlaying == keyChanging)
		{
			volatileLock = true;
			keys[keyNum].deleted = true;
			volatileLock = false;
		}
	}
}

// Decides whether to detune or add a key or remove a key	
void readKey(int keyChanging)
{
	if (detuneMode != SELECT_DETUNE_NOTE)
	{
		if (keyStates[keyChanging] == false)
		{
			addKey(keyChanging);
		}
		else
		{
			removeKey(keyChanging);
		}
	}
	else
	{
		selectDetuneNote(keyChanging);
	}
	keyStates[keyChanging] = !keyStates[keyChanging];
}

void readKeys(int keyMatrixNum)
{
	bool keyMatrixState[4];
	readKeyMatrix(keyMatrixState, keyMatrixNum);	// Writes the states of the four notes being read
	
	for (int keyMatrixOffset = 0; keyMatrixOffset < 4; keyMatrixOffset++)
	{
		int keyChanging = keyMatrixNum + keyMatrixOffset;
		if (keyMatrixState[keyMatrixOffset] != keyStates[keyChanging])
		{ 
			readKey(keyChanging);
		}
	}
}

int main(void)
{
	int keyMatrixNum = 0;
	int buttonMatrixNum = 0;
	int potMatrixNum = 0;
	
	for (int keyNum = 0; keyNum < NUM_OF_KEYS; keyNum++)
	{
		keyStates[keyNum] = false;
	}
	keyStates[16] = true;
	
	for (int keyNum = 0; keyNum < MAX_NOTES; keyNum++)
	{
		keys[keyNum].deleted = true;
	}
	
	systemInit();
	while (1)
	{
		powerKeyMatrix(keyMatrixNum);

		readKeys(keyMatrixNum);

		keyMatrixNum += 4;			// If you read X notes each time this should be X

		if (keyMatrixNum == NUM_OF_KEYS)
		{
			keyMatrixNum = 0;
		}
	}
	
	return 0;
}

void TC0_Handler()
{
	volatile uint32_t nothing = TC0->TC_CHANNEL[0].TC_SR;
	SPI->SPI_TDR |= getNextAmplitude();
	tickCount += 1;
}
