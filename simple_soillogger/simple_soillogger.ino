#include <SPI.h>
#include <Controllino.h>
#include <SDISerial.h>

#define DATALINE_PIN CONTROLLINO_D0  //choose a pin that supports interupts
// Digital 0 on Controllino = pin 2 on Arduino Mega 
#define INVERTED 1

SDISerial sdi_serial_connection(DATALINE_PIN, INVERTED);

//------------------------------------------------------------------------------

void setup() {
  sdi_serial_connection.begin();
  Serial.begin(9600);
  delay(500);
  Serial.println("OK INITIALIZED");
  delay(3000);
  // startup delay to allow sensor to powerup
  // and output its DDI serial string
}

void loop() {
  const int16_t responseWaitMs = 1000;
  char* response = get_measurement(responseWaitMs);
  Serial.println(response!=NULL&&response[0] != '\0'?response:"No Response!");
  delay(500);
}

//------------------------------------------------------------------------------

char* get_measurement(int16_t waitMs) {
  char* service_request = sdi_serial_connection.sdi_query("?M!10013", waitMs);
  // the time  above is to wait for service_request_complete
  char* service_request_complete = sdi_serial_connection.wait_for_response(waitMs);
  // will return once it gets a response
  return sdi_serial_connection.sdi_query("?D0!", waitMs);
}

//------------------------------------------------------------------------------
