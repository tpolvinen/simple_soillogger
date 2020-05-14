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
  Serial.println("OK INITIALIZED");
  delay(3000);
  // startup delay to allow sensor to powerup
  // and output its DDI serial string
}

void loop() {
  uint8_t wait_for_response_ms = 1000;
  char* response = get_measurement();
  Serial.println(response!=NULL&&response[0] != '\0'?response:"No Response!");
  delay(500);
}

//------------------------------------------------------------------------------

char* get_measurement() {
  char* service_request = sdi_serial_connection.sdi_query("?M!10013", 1000);
  // the time  above is to wait for service_request_complete
  char* service_request_complete = sdi_serial_connection.wait_for_response(1000);
  // will return once it gets a response
  return sdi_serial_connection.sdi_query("?D0!", 1000);
}

//------------------------------------------------------------------------------
