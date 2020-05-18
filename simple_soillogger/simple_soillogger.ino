#define DEBUG
#include <DebugMacros.h> // Example: DPRINTLN(x,HEX);

#include <MinimumSerial.h>
#include <BlockDriver.h>
#include <FreeStack.h>
#include <SdFat.h>
#include <sdios.h>
#include <SysCall.h>
#include <SdFatConfig.h>

#include <SPI.h>
#include <Controllino.h>
#include <SDISerial.h>
#include <avr/wdt.h>

const int8_t addressArray[6] = {0, 1, 2, 3, 4, 5};
const int16_t responseWaitMs = 1000;
char* response;
char tempMeasurement[19];        // temporary array for use when parsing

int8_t address = 0;
float dielectricPermittivity = 0.0;
float electricalConductivity = 0.0;
float temperature = 0.0;
char addressArr[2]; // space for single digit address, plus the null char terminator
char delimiterArr[2] = {','};
char dielectricPermittivityArr[6]; // space for 12.34, plus the null char terminator
char electricalConductivityArr[6]; // space for 12.34, plus the null char terminator
char temperatureArr[6]; // space for +12.3, plus the null char terminator

#define DATALINE_PIN CONTROLLINO_D0
// Choose a pin that supports interupts
// Digital 0 on Controllino = pin 2 on Arduino Mega
#define INVERTED 1

SDISerial sdi_serial_connection(DATALINE_PIN, INVERTED);

char dateAndTimeData[20]; // space for YYYY-MM-DDTHH-MM-SS, plus the null char terminator

SdFat sdFat;
const uint8_t SD_CS = 53;  // chip select for sd1

//SdFile sdFile;
SdFile sdMeasurementFile;
SdFile sdLogFile;

unsigned long startsdCardInitializeDelay = 0; // to mark the start of current sdCardInitializeDelay
const int16_t sdCardInitializeDelay = 200; // in milliseconds, interval between attempts to read sd card if removed - remember watchdog timer settings!

bool headerLine = false;

char sdDataLine[160];

char sdMeasurementFileName[10]; // space for MM-DD.csv, plus the null char terminator
char sdLogFileName[13]; // space for MM-DDlog.csv, plus the null char terminator

char sdMeasurementDirName[7]; // space for /YY-MM, plus the null char terminator
char sdLogDirName[4] = {"log"};



//------------------------------------------------------------------------------
// print error msg, any SD error codes, and halt.
// store messages in flash
#define errorExit(msg) errorHalt(F(msg))
#define initError(msg) initErrorHalt(F(msg))
//------------------------------------------------------------------------------

void getDateAndTime() {

  uint16_t thisYear;
  int8_t thisMonth, thisDay, thisHour, thisMinute, thisSecond;

  thisYear = Controllino_GetYear(); thisYear = thisYear + 2000;
  thisMonth = Controllino_GetMonth();
  thisDay = Controllino_GetDay();
  thisHour = Controllino_GetHour();
  thisMinute = Controllino_GetMinute();
  thisSecond = Controllino_GetSecond();

  sprintf(dateAndTimeData, ("%04d-%02d-%02dT%02d:%02d:%02d"), thisYear, thisMonth, thisDay, thisHour, thisMinute, thisSecond);
  sprintf(sdMeasurementFileName, ("%02d-%02d.csv"), thisMonth, thisDay);
  sprintf(sdLogFileName, ("%02d-%02dlog.csv"), thisMonth, thisDay);
  sprintf(sdMeasurementDirName, ("/%02d-%02d"), thisYear, thisMonth);

}

//------------------------------------------------------------------------------

void setup() {

  wdt_disable();  // Disable the watchdog and wait for more than 2 seconds
  delay(3000);  // With this the Arduino doesn't keep resetting infinitely in case of wrong configuration
  wdt_enable(WDTO_8S);

  sdi_serial_connection.begin();
  Serial.begin(9600);
  while (!Serial) {
    delay(10);
  }

  Controllino_RTC_init(0);
  // Controllino_SetTimeDate(16, 5, 5, 20, 20, 41, 00); // set initial values to the RTC chip
  // (Day of the month, Day of the week, Month, Year, Hour, Minute, Second)

  delay(500);

  getDateAndTime();


  DPRINTLN();
  DPRINTLN("------------------------------------------------------------");
  DPRINTLN("            Simple Soil Logger starting!");
  DPRINTLN("------------------------------------------------------------");
  DPRINTLN("           DEBUG PRINTING TO SERIAL IS ON!");
  DPRINT("                 ");
  DPRINTLN(dateAndTimeData);

  // writing header line to data file
  sprintf(sdDataLine, "Date and Time,");
  for (int8_t i = 0; i < sizeof addressArray / sizeof addressArray[0]; i++) {
    char headers[20] = {"address,DeP,EC,temp"};
    strcat(sdDataLine, headers);
    if (i < (sizeof addressArray / sizeof addressArray[0] - 1)) {
      strcat(sdDataLine, delimiterArr);
    }
  }
  headerLine = true;
  sdWrite(sdFat, sdMeasurementDirName, sdMeasurementFile, sdMeasurementFileName, dateAndTimeData, sdDataLine, headerLine);

  strcpy(sdDataLine, dateAndTimeData);
  strcat(sdDataLine, delimiterArr);
  strcat(sdDataLine, "Hello world. I start now,Sensor addresses are,");

  for (int8_t i = 0; i < sizeof addressArray / sizeof addressArray[0]; i++) {
    char charAddress[2];
    itoa(addressArray[i], charAddress, 10);
    strcat(sdDataLine, charAddress);
    if (i < (sizeof addressArray / sizeof addressArray[0] - 1)) {
      strcat(sdDataLine, delimiterArr);
    }
  }

  DPRINTLN(sdDataLine);
  //sdwriteLog();

  headerLine = false;
  sdWrite(sdFat, sdLogDirName, sdLogFile, sdLogFileName, dateAndTimeData, sdDataLine, headerLine);

  delay(3000);
  // startup delay to allow sensor to powerup
  // and output its DDI serial string
}

//------------------------------------------------------------------------------

void loop() {

  wdt_reset();

  getDateAndTime();

  strcpy(sdDataLine, dateAndTimeData);
  strcat(sdDataLine, delimiterArr);

  for (int8_t i = 0; i < sizeof addressArray / sizeof addressArray[0]; i++) {

    wdt_reset();

    DPRINTLN("------------------------------------------------------------");

    int8_t sensorAddress = addressArray[i];

    response = get_measurement(responseWaitMs, sensorAddress);

    DPRINT("Raw sensor response: ");
    DPRINTLN(response != NULL && response[0] != '\0' ? response : "No Response!");

    if (response != NULL && response[0] != '\0') {
      //      strcpy(tempMeasurement, response);
      // this temporary copy is necessary to protect the original data
      // because strtok() used in parseDataNum() replaces the commas with \0
      //      parseDataNum();

      strcpy(tempMeasurement, response);
      // make a fresh disposable working copy for parseDataChar() to take apart
      parseDataChar();

      concatDataChar();

      // showParseDataNum();
      // showParseDataChar();

      delay(500);

    } else {

      // clean previous measurements
      dielectricPermittivity = 0.0;
      electricalConductivity = 0.0;
      temperature = 0.0;
      parseDataNum();

      // Mark data points as not there
      strcpy(addressArr, "N/A");
      strcpy(dielectricPermittivityArr, "N/A");
      strcpy(electricalConductivityArr, "N/A");
      strcpy(temperatureArr, "N/A");
      concatDataChar();

    }

    DPRINTLN("------------------------------------------------------------");
    DPRINTLN();
    DPRINTLN();

    delay(500);
  }

  headerLine = false;
  sdWrite(sdFat, sdMeasurementDirName, sdMeasurementFile, sdMeasurementFileName, dateAndTimeData, sdDataLine, headerLine);

  Serial.println(sdDataLine);
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

void parseDataNum() {      // split the data into numbers

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

void parseDataChar() {      // split the data into char arrays

  char * strtokIndx; // this is used by strtok() as an index

  strtokIndx = strtok(tempMeasurement, "+");     // get the first part - the address
  strcpy(addressArr, strtokIndx); // copy it to char array

  strtokIndx = strtok(NULL, "+"); // this continues where the previous call left off
  strcpy(dielectricPermittivityArr, strtokIndx); // copy it to char array

  strtokIndx = strtok(NULL, "+");
  strcpy(electricalConductivityArr, strtokIndx); // copy it to char array

  strtokIndx = strtok(NULL, "+-");
  strcpy(temperatureArr, strtokIndx); // copy it to char array

}

//------------------------------------------------------------------------------

void concatDataChar() {

  strcat(sdDataLine, addressArr);
  strcat(sdDataLine, delimiterArr);
  strcat(sdDataLine, dielectricPermittivityArr);
  strcat(sdDataLine, delimiterArr);
  strcat(sdDataLine, electricalConductivityArr);
  strcat(sdDataLine, delimiterArr);
  strcat(sdDataLine, temperatureArr);
  strcat(sdDataLine, delimiterArr);
  DPRINTLN();
  DPRINTLN("Sensor response in concatenated array");
  DPRINTLN("-------------------------------------");
  DPRINTLN(sdDataLine);
  DPRINTLN("-------------------------------------");
  DPRINTLN();
}

//------------------------------------------------------------------------------

void showParseDataNum() {
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
  DPRINTLN("------------------------------");

  // for serial plotter:
  // Serial.print(dielectricPermittivity);
  // Serial.print("\t");
}

//------------------------------------------------------------------------------

void showParseDataChar() {
  DPRINTLN();
  DPRINTLN("Sensor response in char arrays:");
  DPRINTLN("------------------------------");
  DPRINT("address = ");
  DPRINTLN(addressArr);
  DPRINT("dielectricPermittivity = ");
  DPRINTLN(dielectricPermittivityArr);
  DPRINT("electricalConductivity = ");
  DPRINTLN(electricalConductivityArr);
  DPRINT("temperature = ");
  DPRINTLN(temperatureArr);
  DPRINTLN("------------------------------");
}

//------------------------------------------------------------------------------

void sdWrite(SdFat sd, char* dirName, SdFile sdFile, char* fileName, char* timeData, char* data, bool header) {

  wdt_reset();

  DPRINTLN("begin sdWrite()");

  for (; !sd.begin(SD_CS);) {  // This for loop for some reason stops Controllino's RTC until card is inserted!!!!!!

    wdt_reset();

    DPRINTLN("sdWrite(): SD not found!");

    if (millis() > startsdCardInitializeDelay + sdCardInitializeDelay) {
      sd.begin(SD_CS);
      startsdCardInitializeDelay = millis();
    }
  }

  if (!sd.exists(dirName)) {
    if (!sd.mkdir(dirName)) {
      sd.errorExit("sd.mkdir(sdLogDirName)");
    }
  }

  // make /dirName the default directory for sd
  if (!sd.chdir(dirName)) {
    sd.errorExit("sd.chdir(dirName)");
  }

  //open file within Folder
  if (header) {
    if (!sdFile.open(fileName, O_RDWR | O_CREAT)) {
      sd.errorExit("sdFile.open");
    }
  } else {
    if (!sdFile.open(fileName, O_RDWR | O_CREAT | O_AT_END)) {
      sd.errorExit("sdFile.open");
    }
  }

  if (! (sdFile.println(data)) ) {
    sd.errorExit("println(data)");
  }

  sdFile.close();
}
