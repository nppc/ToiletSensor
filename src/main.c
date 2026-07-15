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

int main(void) {
  uint8_t retval;
  enter_DefaultMode_from_RESET();

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
