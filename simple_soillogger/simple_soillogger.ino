#include <SPI.h>
#include <Controllino.h>
#include <SDISerial.h>

const int8_t addressArray[6] = {0, 1, 2, 3, 4, 5};

const int16_t responseWaitMs = 1000;

char* response;
char tempMeasurement[50];        // temporary array for use when parsing

int8_t address = 0;
float dielectricPermittivity = 0.0;
float electricalConductivity = 0.0;
float temperature = 0.0;

#define DATALINE_PIN CONTROLLINO_D0  //choose a pin that supports interupts
// Digital 0 on Controllino = pin 2 on Arduino Mega
#define INVERTED 1

SDISerial sdi_serial_connection(DATALINE_PIN, INVERTED);

//------------------------------------------------------------------------------

void setup() {
  sdi_serial_connection.begin();
  Serial.begin(9600);
  delay(500);

  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.println("            Simple Soil Logger starting!");
  Serial.println("------------------------------------------------------------");
  Serial.println();

  delay(3000);
  // startup delay to allow sensor to powerup
  // and output its DDI serial string
}

//------------------------------------------------------------------------------

void loop() {

  for (int8_t i = 0; i < sizeof addressArray / sizeof addressArray[0]; i++) {
    int8_t sensorAddress = addressArray[i];

    Serial.println("------------------------------------------------------------");

    response = get_measurement(responseWaitMs, sensorAddress);

    Serial.print("Raw sensor response: ");
    Serial.println(response != NULL && response[0] != '\0' ? response : "No Response!");

    if (response != NULL && response[0] != '\0') {
      strcpy(tempMeasurement, response); // tempMeasurement = tempChars  and  response = receivedChars
      // this temporary copy is necessary to protect the original data
      //   because strtok() used in parseData() replaces the commas with \0

      parseData();
      showParsedData();
      delay(500);
    }

    dielectricPermittivity = 0.0; // clean previous measurements
    electricalConductivity = 0.0;
    temperature = 0.0;

    Serial.println("------------------------------------------------------------");
    Serial.println();
    Serial.println();

    delay(500);
  }
}

//------------------------------------------------------------------------------

char* get_measurement(int16_t waitMs, int8_t address) {

  char service_request_query_M[10];
  sprintf(service_request_query_M, "%iM!10013", address);

  Serial.print("Sensor query M: ");
  Serial.println(service_request_query_M);

  char* service_request = sdi_serial_connection.sdi_query(service_request_query_M, waitMs);
  // the time  above is to wait for service_request_complete

  char* service_request_complete = sdi_serial_connection.wait_for_response(waitMs);
  // will return once it gets a response

  char service_request_query_D0[10];
  sprintf(service_request_query_D0, "%iD0!", address);

  Serial.print("Sensor query D0: ");
  Serial.println(service_request_query_D0);

  return sdi_serial_connection.sdi_query(service_request_query_D0, waitMs);

}

//------------------------------------------------------------------------------

void parseData() {      // split the data into its parts

  char * strtokIndx; // this is used by strtok() as an index

  strtokIndx = strtok(tempMeasurement, "+");     // get the first part - the address
  address = atoi(strtokIndx);      // convert this part to an integer

  strtokIndx = strtok(NULL, "+"); // this continues where the previous call left off
  dielectricPermittivity = atof(strtokIndx);     // convert this part to a float

  strtokIndx = strtok(NULL, "+");
  electricalConductivity = atof(strtokIndx);     // convert this part to a float

  strtokIndx = strtok(NULL, "+-");
  temperature = atof(strtokIndx);     // convert this part to a float

}

//------------------------------------------------------------------------------

void showParsedData() {
  Serial.println();
  Serial.println("Sensor response in variables:");
  Serial.println("------------------------------");
  Serial.print("address = ");
  Serial.println(address);
  Serial.print("dielectricPermittivity = ");
  Serial.println(dielectricPermittivity, 3);
  Serial.print("electricalConductivity = ");
  Serial.println(electricalConductivity, 3);
  Serial.print("temperature = ");
  Serial.println(temperature, 2);
}
