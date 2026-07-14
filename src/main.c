//#pragma src

#include "main.h"
#include "InitDevice.h"
#include "i2c.h"
//#include "i2c_bitbang.h"
//#include "adpcm_decoder.h"
//#include "flash.h"

void SiLabs_Startup(void) {
  // $[SiLabs Startup]
  // [SiLabs Startup]$

}


int main(void) {

  enter_DefaultMode_from_RESET();

  //i2c_init();
  //ADPCM_Start(6144);


  while(1){

      // interrupt driven
      test_i2c();

      waitNms(5000);
 //     ADPCM_Task();
  }
}
