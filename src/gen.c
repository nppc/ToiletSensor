#include "main.h"

// max 65 seconds
void waitNms(uint16_t ms){
	// convert ms to timer ticks
	// 1tick = 0.0408us
	// 1ms = 24510 timer ticks (H register = 96)
	// we can use PCA timer ticks as it runs alleays with SYSCK speed
	// lets use only PCA0H to make it easier to work with 16bit registers of timer that we cant pause
	uint8_t tmp,tmp1;
	while(ms!=0){
		tmp = PCA0L; // we need to read first PCA0L to latch PCA0H
		tmp = PCA0H;
		tmp+=96*2;	// don't yet know why 105 and not 96...
			while(1){
				tmp1 = PCA0L; // we need to read first PCA0L to latch PCA0H
				tmp1 = PCA0H;
				if(tmp1 == tmp){break;}
		}
		ms--;
	}
}


