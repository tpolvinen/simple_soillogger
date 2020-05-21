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
#include <Statistic.h>


const int16_t measurementRoundNumber = 20;

#define DATALINE_PIN CONTROLLINO_D0
// Choose a pin that supports interupts
// Digital 0 on Controllino = pin 2 on Arduino Mega
#define INVERTED 1

SDISerial sdi_serial_connection(DATALINE_PIN, INVERTED);

char dateAndTimeData[20]; // space for YYYY-MM-DDTHH-MM-SS, plus the null char terminator

SdFat sdFat;
const uint8_t SD_CS = 53;  // chip select for SD card

SdFile sdMeasurementFile;
SdFile sdLogFile;

// ToDo: attach interrupt to SD card eject and stop everything until the card is back

bool headerLine = false;


char sdMeasurementFileName[10]; // space for MM-DD.csv, plus the null char terminator
char sdLogFileName[13]; // space for MM-DDlog.csv, plus the null char terminator

char sdMeasurementDirName[10]; // space for /YY-MM, plus the null char terminator
char sdLogDirName[4] = {"log"};
char sdDataLine[304]; // 20 + 6 * 44 + 20 = 304

char delimiter[2] = {','};


//------------------------------------------------------------------------------
// print error msg, any SD error codes, and halt.
// store messages in flash
#define errorExit(msg) errorHalt(F(msg))
#define initError(msg) initErrorHalt(F(msg))
//------------------------------------------------------------------------------


//------------------------------------------------------------------------------
// Sensor object for making single measurement readings (responses)
// and to keep track of consecutive readings in measurement rounds.
// Statistics are used for the measurement round end results.
//------------------------------------------------------------------------------

class Sensor {
    int8_t sensorAddress;
    int16_t responseWaitMs = 1000;

  public:

    Sensor(int8_t addressInt) {
      sensorAddress = addressInt;
    }
    char* response;
    char* measurementBuffer = (char*) malloc (44); // [44];

    void sensorGetReading() { // to get single sensor response
      wdt_reset();
      sensorGetResponse();
      if (response != NULL && response[0] != '\0') {
        strcpy(parserBuffer, response);
        //        DPRINT("parserBuffer: ");
        //        DPRINTLN(parserBuffer);
        sensorParseData();
        sensorStatistics();
      }
    }

    char * sensorGetMeasurement() { // to get a calculated measurement from a number of sensor responses
      wdt_reset();

      /*
        ToDo: put the statistic product variables to an array and make this neat!
                    dielectricPermittivityMean,
                    dielectricPermittivitypop_stdev,
                    electricalConductivityMean,
                    electricalConductivitypop_stdev,
                    temperatureMean,
                    temperaturepop_stdev
      */

      char integerBuffer[10];
      char measurementValueBuffer[6];

      itoa(sensorAddress, integerBuffer, 10);
      strcpy(measurementBuffer, integerBuffer);
      strcat(measurementBuffer, delimiter);

      dtostrf(dielectricPermittivityMean, 5, 2, measurementValueBuffer);
      strcat(measurementBuffer, measurementValueBuffer);
      strcat(measurementBuffer, delimiter);

      dtostrf(dielectricPermittivitypop_stdev, 5, 2, measurementValueBuffer);
      strcat(measurementBuffer, measurementValueBuffer);
      strcat(measurementBuffer, delimiter);

      dtostrf(electricalConductivityMean, 5, 2, measurementValueBuffer);
      strcat(measurementBuffer, measurementValueBuffer);
      strcat(measurementBuffer, delimiter);

      dtostrf(electricalConductivitypop_stdev, 5, 2, measurementValueBuffer);
      strcat(measurementBuffer, measurementValueBuffer);
      strcat(measurementBuffer, delimiter);

      dtostrf(temperatureMean, 5, 2, measurementValueBuffer);
      strcat(measurementBuffer, measurementValueBuffer);
      strcat(measurementBuffer, delimiter);

      dtostrf(temperaturepop_stdev, 5, 2, measurementValueBuffer);
      strcat(measurementBuffer, measurementValueBuffer);
      strcat(measurementBuffer, delimiter);

      itoa(readingCount, integerBuffer, 10);
      strcat(measurementBuffer, integerBuffer);


      //      DPRINT("measurementBuffer: ");
      //      DPRINTLN(measurementBuffer);

      dielectricPermittivityStat.clear();
      electricalConductivityStat.clear();
      temperatureStat.clear();
      readingCount = 0;

      return measurementBuffer;
    }

  private:

    int8_t responseAddress;
    float dielectricPermittivity;
    float electricalConductivity;
    float temperature;
    char parserBuffer[20];
    float dielectricPermittivityMean;
    float dielectricPermittivitypop_stdev;
    float electricalConductivityMean;
    float electricalConductivitypop_stdev;
    float temperatureMean;
    float temperaturepop_stdev;
    Statistic dielectricPermittivityStat;
    Statistic electricalConductivityStat;
    Statistic temperatureStat;
    uint32_t readingCount;

    void sensorGetResponse() {

      wdt_reset();

      char service_request_query_M[10];
      sprintf(service_request_query_M, "%iM!10013", sensorAddress);

      //      DPRINT("Sensor query M: ");
      //      DPRINTLN(service_request_query_M);

      char* service_request = sdi_serial_connection.sdi_query(service_request_query_M, responseWaitMs);
      // the time  above is to wait for service_request_complete

      char* service_request_complete = sdi_serial_connection.wait_for_response(responseWaitMs);
      // will return once it gets a response

      char service_request_query_D0[10];
      sprintf(service_request_query_D0, "%iD0!", sensorAddress);

      //      DPRINT("Sensor query D0: ");
      //      DPRINTLN(service_request_query_D0);

      response = sdi_serial_connection.sdi_query(service_request_query_D0, responseWaitMs);

      //      DPRINT("Sensor response: ");
      //      DPRINTLN(response);
    }

    void sensorParseData() {
      char * strtokIndx; // this is used by strtok() as an index

      strtokIndx = strtok(parserBuffer, "+");     // get the first part - the address
      responseAddress = atoi(strtokIndx);      // convert this part to an integer

      //      DPRINT("responseAddress: ");
      //      DPRINTLN(responseAddress);

      strtokIndx = strtok(NULL, "+"); // this continues where the previous call left off
      dielectricPermittivity = atof(strtokIndx);     // convert this part to a float

      //      DPRINT("dielectricPermittivity: ");
      //      DPRINTLN(dielectricPermittivity);

      strtokIndx = strtok(NULL, "+");
      electricalConductivity = atof(strtokIndx);     // convert this part to a float

      //      DPRINT("electricalConductivity: ");
      //      DPRINTLN(electricalConductivity);

      strtokIndx = strtok(NULL, "+-");
      temperature = atof(strtokIndx);     // convert this part to a float

      //      DPRINT("temperature: ");
      //      DPRINTLN(temperature);
    }

    void sensorStatistics() {
      dielectricPermittivityStat.add(dielectricPermittivity);
      electricalConductivityStat.add(electricalConductivity);
      temperatureStat.add(temperature);

      dielectricPermittivityMean = dielectricPermittivityStat.average();
      //      DPRINT("dielectricPermittivityMean: ");
      //      DPRINTLN(dielectricPermittivityMean);

      electricalConductivityMean = electricalConductivityStat.average();
      //      DPRINT("electricalConductivityMean: ");
      //      DPRINTLN(electricalConductivityMean);

      temperatureMean = temperatureStat.average();
      //      DPRINT("temperatureMean: ");
      //      DPRINTLN(temperatureMean);


      dielectricPermittivitypop_stdev = dielectricPermittivityStat.pop_stdev();
      electricalConductivitypop_stdev = electricalConductivityStat.pop_stdev();
      temperaturepop_stdev = temperatureStat.pop_stdev();

      readingCount = dielectricPermittivityStat.count();
      //      DPRINT("readingCount: ");
      //      DPRINTLN(readingCount);

    }
};

Sensor sensor0(0);
Sensor sensor1(1);
Sensor sensor2(2);
Sensor sensor3(3);
Sensor sensor4(4);
Sensor sensor5(5);

Sensor sensorArray[6] = {sensor0, sensor1, sensor2, sensor3, sensor4, sensor5};

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
/*
  void sdWrite(SdFat sd, char* dirName, SdFile sdFile, char* fileName, char* data, bool header) {

  // void sdWrite(SdFat sd, char* dirName, SdFile sdFile, char* fileName, char* timeData, char* data, bool header)

  wdt_reset();

  DPRINTLN("begin sdWrite()");

  if (!sd.begin(SD_CS)) {
    sd.errorExit("sd.begin(SD_CS)");
    DPRINTLN("sdWrite(): SD not found!");
  }

  DPRINTLN(dirName);
  if (!sd.exists(dirName)) {
    if (!sd.mkdir(dirName)) {
      sd.errorExit("sd.mkdir(dirName)");
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
*/
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
  // Controllino_SetTimeDate(21, 4, 5, 20, 23, 15, 00); // set initial values to the RTC chip
  // (Day of the month, Day of the week, Month, Year, Hour, Minute, Second)

  getDateAndTime();


  DPRINTLN();
  DPRINTLN("------------------------------------------------------------");
  DPRINTLN("            Simple Soil Logger starting!");
  DPRINTLN("------------------------------------------------------------");
  DPRINTLN("           DEBUG PRINTING TO SERIAL IS ON!");
  DPRINT("                 ");
  DPRINTLN(dateAndTimeData);


  //----------------------------------------
  // writing header line to data file
  //----------------------------------------
  /*
    sprintf(sdDataLine, "Start time,End time");
    for (int8_t i = 0; i < sizeof sensorArray / sizeof sensorArray[0]; i++) {
      char headers[80] = {"Address,DeP Mean,Dep StDev,EC Mean,EC StDev,Temp Mean, Temp StDev, Measurements"};
      strcat(sdDataLine, headers);
      if (i < (sizeof sensorArray / sizeof sensorArray[0] - 1)) {
        strcat(sdDataLine, delimiter);
      }
    }
    headerLine = true;
    sdWrite(sdFat, sdMeasurementDirName, sdMeasurementFile, sdMeasurementFileName, sdDataLine, headerLine);

    //----------------------------------------

    //----------------------------------------
    // writing system start line to log file
    //----------------------------------------

    strcpy(sdDataLine, dateAndTimeData);
    strcat(sdDataLine, delimiter);
    strcat(sdDataLine, "Hello world. I start now.");


      for (int8_t i = 0; i < sizeof addressArray / sizeof addressArray[0]; i++) {
      char charAddress[2];
      itoa(addressArray[i], charAddress, 10);
      strcat(sdDataLine, charAddress);
      if (i < (sizeof addressArray / sizeof addressArray[0] - 1)) {
        strcat(sdDataLine, delimiterArr);
      }
      }


    DPRINTLN(sdDataLine);

    headerLine = false;
    sdWrite(sdFat, sdLogDirName, sdLogFile, sdLogFileName, sdDataLine, headerLine);
    //----------------------------------------
  */
  delay(3000);
  // startup delay to allow sensors to powerup
  // and output DDI serial strings

}



//------------------------------------------------------------------------------

void loop() {

  wdt_reset();

  char beginRoundTime[20];
  char endRoundTime[20];
  int8_t rtcMinute;

  rtcMinute = Controllino_GetMinute();

  if (rtcMinute == 0 || rtcMinute == 30) {
    getDateAndTime();
    strcpy(beginRoundTime, dateAndTimeData);

    for (int16_t repeats = 0; repeats < 40; repeats++) {
      sensor0.sensorGetReading();
      sensor1.sensorGetReading();
      sensor2.sensorGetReading();
      sensor3.sensorGetReading();
      sensor4.sensorGetReading();
      sensor5.sensorGetReading();
    }

    getDateAndTime();

    strcpy(endRoundTime, dateAndTimeData);

    strcpy(sdDataLine, beginRoundTime);
    strcat(sdDataLine, delimiter);
    strcat(sdDataLine, endRoundTime);
    strcat(sdDataLine, delimiter);

    char * measurementDataBuffer;
    measurementDataBuffer = sensor0.sensorGetMeasurement();
    strcat(sdDataLine, measurementDataBuffer);
    strcat(sdDataLine, delimiter);

    measurementDataBuffer = sensor1.sensorGetMeasurement();
    strcat(sdDataLine, measurementDataBuffer);
    strcat(sdDataLine, delimiter);

    measurementDataBuffer = sensor2.sensorGetMeasurement();
    strcat(sdDataLine, measurementDataBuffer);
    strcat(sdDataLine, delimiter);

    measurementDataBuffer = sensor3.sensorGetMeasurement();
    strcat(sdDataLine, measurementDataBuffer);
    strcat(sdDataLine, delimiter);

    measurementDataBuffer = sensor4.sensorGetMeasurement();
    strcat(sdDataLine, measurementDataBuffer);
    strcat(sdDataLine, delimiter);

    measurementDataBuffer = sensor5.sensorGetMeasurement();
    strcat(sdDataLine, measurementDataBuffer);
    free (measurementDataBuffer);

    DPRINTLN(sdDataLine);


    if (!sdFat.begin(SD_CS)) {
      sdFat.errorExit("sd.begin(SD_CS)");
    }

    if (!sdFat.exists(sdMeasurementDirName)) {
      if (!sdFat.mkdir(sdMeasurementDirName)) {
        sdFat.errorExit("sd.mkdir");
      }
    }

    // make /dirName the default directory for sd
    if (!sdFat.chdir(sdMeasurementDirName)) {
      sdFat.errorExit("sd.chdir");
    }

    //open file within Folder
    if (!sdMeasurementFile.open(sdMeasurementFileName, O_RDWR | O_CREAT | O_AT_END)) {
      sdFat.errorExit("sdFile.open");
    }

    if (! (sdMeasurementFile.println(sdDataLine)) ) {
      sdFat.errorExit("println");
    }

    sdMeasurementFile.close();

    for (int8_t i = 0; i < 12; i++) {
      wdt_reset();
      delay(5000);
    }
  }
}
