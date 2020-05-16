#include <MinimumSerial.h>
#include <BlockDriver.h>
#include <FreeStack.h>
#include <SdFat.h>
#include <sdios.h>
#include <SysCall.h>
#include <SdFatConfig.h>

//#define DEBUG
#include <DebugMacros.h> // Example: DPRINTLN(x,HEX); 

#include <SPI.h>
#include <Controllino.h>
#include <SDISerial.h>
#include <avr/wdt.h>


const int8_t addressArray[6] = {0, 1, 2, 3, 4, 5};

const int16_t responseWaitMs = 1000;

char* response;
char tempMeasurement[50];        // temporary array for use when parsing

int8_t address = 0;
float dielectricPermittivity = 0.0;
float electricalConductivity = 0.0;
float temperature = 0.0;

#define DATALINE_PIN CONTROLLINO_D0
// Choose a pin that supports interupts
// Digital 0 on Controllino = pin 2 on Arduino Mega
#define INVERTED 1

SDISerial sdi_serial_connection(DATALINE_PIN, INVERTED);

SdFat sd1;
const uint8_t SD1_CS = 53;  // chip select for sd1

SdFile measurementfile1;
SdFile logfile1;

char logMsg[100];
char measurementfileHeader[100]; // space for YYYY-MM-DDThh:mm:ss,0-0,etc. plus the null char terminator
char dateAndTimeData[20]; // space for YYYY-MM-DDTHH-MM-SS, plus the null char terminator
char measurementfileName[10]; // space for MM-DD.csv, plus the null char terminator
char logfileName[13]; // space for MM-DDlog.csv, plus the null char terminator
char dirName[7]; // space for /YY-MM, plus the null char terminator

uint16_t thisYear;
int8_t thisMonth, thisDay, thisHour, thisMinute, thisSecond;

//------------------------------------------------------------------------------
// print error msg, any SD error codes, and halt.
// store messages in flash
#define errorExit(msg) errorHalt(F(msg))
#define initError(msg) initErrorHalt(F(msg))
//------------------------------------------------------------------------------


//------------------------------------------------------------------------------

void setup() {

  wdt_disable();  // Disable the watchdog and wait for more than 2 seconds
  delay(3000);  // With this the Arduino doesn't keep resetting infinitely in case of wrong configuration
  wdt_enable(WDTO_8S);

  sdi_serial_connection.begin();
  Serial.begin(9600);

  Controllino_RTC_init(0);
  // Controllino_SetTimeDate(16, 5, 5, 20, 20, 41, 00); // set initial values to the RTC chip
  // (Day of the month, Day of the week, Month, Year, Hour, Minute, Second)

  delay(500);

  DPRINTLN();
  DPRINTLN("------------------------------------------------------------");
  DPRINTLN("            Simple Soil Logger starting!");
  DPRINTLN("------------------------------------------------------------");
  DPRINTLN("           DEBUG PRINTING TO SERIAL IS ON!");
  DPRINTLN();

  delay(3000);
  // startup delay to allow sensor to powerup
  // and output its DDI serial string
}

//------------------------------------------------------------------------------

void loop() {

  wdt_reset();

  getDateAndTime();

  for (int8_t i = 0; i < sizeof addressArray / sizeof addressArray[0]; i++) {

    wdt_reset();

    int8_t sensorAddress = addressArray[i];

    DPRINTLN("------------------------------------------------------------");

    response = get_measurement(responseWaitMs, sensorAddress);

    DPRINT("Raw sensor response: ");
    DPRINTLN(response != NULL && response[0] != '\0' ? response : "No Response!");

    if (response != NULL && response[0] != '\0') {
      strcpy(tempMeasurement, response);
      // this temporary copy is necessary to protect the original data
      // because strtok() used in parseData() replaces the commas with \0

      parseData();
      showParsedData();
      delay(500);

    } else {
      dielectricPermittivity = 0.0; // clean previous measurements
      electricalConductivity = 0.0;
      temperature = 0.0;
    }

    DPRINTLN("------------------------------------------------------------");
    DPRINTLN();
    DPRINTLN();

    delay(500);
  }
}

//------------------------------------------------------------------------------

char* get_measurement(int16_t waitMs, int8_t address) {

  char service_request_query_M[10];
  sprintf(service_request_query_M, "%iM!10013", address);

  DPRINT("Sensor query M: ");
  DPRINTLN(service_request_query_M);

  char* service_request = sdi_serial_connection.sdi_query(service_request_query_M, waitMs);
  // the time  above is to wait for service_request_complete

  char* service_request_complete = sdi_serial_connection.wait_for_response(waitMs);
  // will return once it gets a response

  char service_request_query_D0[10];
  sprintf(service_request_query_D0, "%iD0!", address);

  DPRINT("Sensor query D0: ");
  DPRINTLN(service_request_query_D0);

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
  DPRINTLN();
  DPRINTLN("Sensor response in variables:");
  DPRINTLN("------------------------------");
  DPRINT("address = ");
  DPRINTLN(address);
  DPRINT("dielectricPermittivity = ");
  DPRINTLN(dielectricPermittivity, 3);
  DPRINT("electricalConductivity = ");
  DPRINTLN(electricalConductivity, 3);
  DPRINT("temperature = ");
  DPRINTLN(temperature, 2);


  //Serial.print(dielectricPermittivity);
  //Serial.print("\t");

}

//------------------------------------------------------------------------------

void getDateAndTime() {

  thisYear = Controllino_GetYear(); thisYear = thisYear + 2000;
  thisMonth = Controllino_GetMonth();
  thisDay = Controllino_GetDay();
  thisHour = Controllino_GetHour();
  thisMinute = Controllino_GetMinute();
  thisSecond = Controllino_GetSecond();

  sprintf(dateAndTimeData, ("%04d-%02d-%02dT%02d:%02d:%02d"), thisYear, thisMonth, thisDay, thisHour, thisMinute, thisSecond);
  sprintf(measurementfileName, ("%02d-%02d.csv"), thisMonth, thisDay);
  sprintf(logfileName, ("%02d-%02dlog.csv"), thisMonth, thisDay);
  sprintf(dirName, ("/%02d-%02d"), thisYear, thisMonth);

  Serial.println(dateAndTimeData);
}
