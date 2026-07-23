//#pragma src

#include "main.h"
#include "InitDevice.h"
#include "i2c.h"
#include "adpcm_decoder.h"
//#include "flash.h"

void SiLabs_Startup(void) {
  // $[SiLabs Startup]
  // [SiLabs Startup]$

}

//uint8_t xdata testBuffer[70];

int main(void) {

  enter_DefaultMode_from_RESET();

  i2c_init();


//  waitNms(10000);
//  test_write_flash_data();

//  retval = test_verify_flash_data();

  ADPCM_Start(11264, 8192);


  while(1){

      //ADPCM_Task();
      ADPCM_Task_interpolated();

      if(!ADPCM_IsBusy()){
          waitNms(500);
          ADPCM_Start(11264, 8192);
      }
  }
}
