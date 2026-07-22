//#pragma src

#include "main.h"
#include "InitDevice.h"
#include "i2c.h"
//#include "i2c_bitbang.h"
#include "adpcm_decoder.h"
#include "flash.h"

void SiLabs_Startup(void) {
  // $[SiLabs Startup]
  // [SiLabs Startup]$

}

//uint8_t xdata testBuffer[70];

int main(void) {

//  uint8_t i;
  enter_DefaultMode_from_RESET();

//  for(i=0;i<70;i++){testBuffer[i]=0;}

  i2c_init();

//  eeprom_read_continuous_start(0,10); // start reading 10 bytes
//
//  for(i=0;i<10;i++){
//    while(!eeprom_read_continuous_poll());
//    testBuffer[i] = eeprom_read_continuous_get_byte();
//  }
//
//  while(1){};

  //test_write_flash_data();

//  retval = test_verify_flash_data();

  ADPCM_Start(11264);


  while(1){

      //test_i2c();

      //waitNms(5000);
      ADPCM_Task();

      if(!ADPCM_IsBusy()){
          waitNms(200);
          ADPCM_Start(11264);
      }
  }
}
