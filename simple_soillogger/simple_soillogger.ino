#include <SPI.h>
#include <Controllino.h>

//------------------------------------------------------------------------------
#include <SDISerial.h>

#define DATALINE_PIN CONTROLLINO_IN0  // pin 18 on Arduino Mega //choose a pin that supports interupts
#define INVERTED 1

SDISerial sdi_serial_connection(DATALINE_PIN, INVERTED);

//------------------------------------------------------------------------------


void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:

}
