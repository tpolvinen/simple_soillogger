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


const int16_t measurementsInRound = 5;
int8_t latestRoundStartMinute;
int8_t latestRoundStartSecond;

#define SDERRORLED CONTROLLINO_D23

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

/*
   ToDo: attach interrupt to SD card eject and stop everything until the card is back
   Currently the watchdog keeps restarting the code until SD card operations succeed.
   On the other hand, reading digital pin before opening file should be enough,
   especially because interrupts are disabled during sensor readings.
   ToDo: make sdDataLine[1000] smaller.
*/


bool headerLine = false;

char sdMeasurementFileName[10]; // space for MM-DD.csv, plus the null char terminator
char sdLogFileName[13]; // space for MM-DDlog.csv, plus the null char terminator

char sdMeasurementDirName[10]; // space for /YY-MM, plus the null char terminator
char sdLogDirName[4] = {"log"};
char sdDataLine[1000]; // Could be smaller, but we've got memory. For now... //304]; // 20 + 6 * 44 + 20 = 304

char delimiter[2] = {','};

char beginRoundTime[20];
char endRoundTime[20];
int8_t rtcMinute;
int8_t rtcSecond;

//------------------------------------------------------------------------------
// SD card errors:
// print error msg, any SD error codes, and halt.
// store messages in flash
#define errorExit(msg) errorHalt(F(msg))
#define initError(msg) initErrorHalt(F(msg))
//------------------------------------------------------------------------------

/*------------------------------------------------------------------------------
   Sensor object that makes single measurement readings (responses),
   calculates final measurement round values,
   and concatenates values to char array for saving.
  ------------------------------------------------------------------------------
*/
class Sensor {
    int8_t sensorAddress;
    int16_t responseWaitMs = 1000;

  public:

    Sensor(int8_t addressInt) {
      sensorAddress = addressInt;
    }
    char* response;
    char* measurementBuffer = (char*) malloc (44);

    /*------------------------------------------------------------------------------
      Function calls for single sensor reading, data parsing, and
      statistic calculation.
      This works towards the final measurement data.
      ------------------------------------------------------------------------------
    */
    bool sensorGetReading() {
      wdt_reset();

      sensorGetResponse();
      if (response != NULL && response[0] != '\0') {
        strcpy(parserBuffer, response);
        //        DPRINT("parserBuffer: ");
        //        DPRINTLN(parserBuffer);
        sensorParseData();
        sensorStatistics();
        return true;
      } else {
        return false;
      }
    }

    /*------------------------------------------------------------------------------
      Returns a final measurement calculated from a number of sensor responses
      in a character array, w/ calculated averages, standard deviations, and
      number of single readings used for calculations.
      ------------------------------------------------------------------------------
    */
    char * sensorGetMeasurement() { //
      wdt_reset();

      /*
         ToDo: put the final statistic variables to an array and make this neat!
                     dielectricPermittivityMean,
                     dielectricPermittivitypop_stdev,
                     electricalConductivityMean,
                     electricalConductivitypop_stdev,
                     temperatureMean,
                     temperaturepop_stdev
      */

      char integerBuffer[10];
      char measurementValueBuffer[10];//6];
      char noValueBuffer[4] = {"N/A"};

      itoa(sensorAddress, integerBuffer, 10);
      strcpy(measurementBuffer, integerBuffer);
      strcat(measurementBuffer, delimiter);

      if (readingCount == 0) {

        strcat(measurementBuffer, noValueBuffer);
        strcat(measurementBuffer, delimiter);

        strcat(measurementBuffer, noValueBuffer);
        strcat(measurementBuffer, delimiter);

        strcat(measurementBuffer, noValueBuffer);
        strcat(measurementBuffer, delimiter);

        strcat(measurementBuffer, noValueBuffer);
        strcat(measurementBuffer, delimiter);

        strcat(measurementBuffer, noValueBuffer);
        strcat(measurementBuffer, delimiter);

        strcat(measurementBuffer, noValueBuffer);
        strcat(measurementBuffer, delimiter);

      } else {

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

      }

      itoa(readingCount, integerBuffer, 10);
      strcat(measurementBuffer, integerBuffer);


      DPRINT("measurementBuffer: ");
      DPRINTLN(measurementBuffer);

      dielectricPermittivityStat.clear();
      electricalConductivityStat.clear();
      temperatureStat.clear();
      readingCount = 0;

      return measurementBuffer;
    }

    //------------------------------------------------------------------------------

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

    /*------------------------------------------------------------------------------
      Writes single response from one sensor to a char array.
      ------------------------------------------------------------------------------
    */
    void sensorGetResponse() {
      wdt_reset();

      char service_request_query_M[10];
      sprintf(service_request_query_M, "%iM!10013", sensorAddress);

      //      DPRINT("Sensor query M: ");
      //      DPRINTLN(service_request_query_M);

      char* service_request = sdi_serial_connection.sdi_query(service_request_query_M, responseWaitMs);
      // the responseWaitMs is to wait for service_request_complete

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

    /*------------------------------------------------------------------------------
      Parses single response from a char array to numeric variables.
      ------------------------------------------------------------------------------
    */
    void sensorParseData() {
      wdt_reset();

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

    /*------------------------------------------------------------------------------
      Adds numeric variables from a single response to statistics objects,
      and calculates a running average, standard deviation,
      and number of responses.
      ------------------------------------------------------------------------------
    */
    void sensorStatistics() {
      wdt_reset();

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

/*------------------------------------------------------------------------------
  Gets RTC time and date and puts them to char arrays,
  for file and folder naming, and
  for concatenation with sensor data.
  ------------------------------------------------------------------------------
*/
void getDateAndTime() {
  wdt_reset();

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

/*------------------------------------------------------------------------------
   Writes a data char array to SD card.
  ------------------------------------------------------------------------------
*/

void sdWrite(SdFat sd, char* dirName, SdFile sdFile, char* fileName, char* data, bool header) {
  wdt_reset();

  DPRINTLN("Begin sdWrite()");

  if (!sd.begin(SD_CS)) {
    errorBlink(SDERRORLED);
    sd.errorExit("sd.begin(SD_CS)");
  }

  DPRINT("DIR: "); DPRINTLN(dirName);
  if (!sd.exists(dirName)) {
    if (!sd.mkdir(dirName)) {
      errorBlink(SDERRORLED);
      sd.errorExit("sd.mkdir(dirName)");
    }
  }

  // make /dirName the default directory for sd
  if (!sd.chdir(dirName)) {
    errorBlink(SDERRORLED);
    sd.errorExit("sd.chdir(dirName)");
  }
  DPRINT("FILE: "); DPRINTLN(fileName);
  //open file within Folder
  if (header) {
    if (!sdFile.open(fileName, O_RDWR | O_CREAT)) {
      errorBlink(SDERRORLED);
      sd.errorExit("sdFile.open");
    }
  } else {
    if (!sdFile.open(fileName, O_RDWR | O_CREAT | O_AT_END)) {
      errorBlink(SDERRORLED);
      sd.errorExit("sdFile.open");
    }
  }
  DPRINT("DATA: "); DPRINTLN(data);
  if (! (sdFile.println(data)) ) {
    errorBlink(SDERRORLED);
    sd.errorExit("println(data)");
  }

  sdFile.close();
}

/*------------------------------------------------------------------------------
   Blinks D23 led 10 times if SD card fails.
  ------------------------------------------------------------------------------
*/
void errorBlink(byte led) {
  int8_t blinkCount = 0;
  while (blinkCount < 10) {
    wdt_reset();
    digitalWrite(led, HIGH);
    delay(500);
    digitalWrite(led, LOW);
    delay(500);
    blinkCount++;
  }
}


/*------------------------------------------------------------------------------
  Sets watchdog timer, begins serial connection, RTC intialisation,
  and begins data and log files, writing a start message and a header line.
  ------------------------------------------------------------------------------
*/
void setup() {

  /*
     ToDo: startup protocol:
      connect sensors one by one:
      connect, delay for 3000 (power up and send out DDI serial strings)
      check sensor with measurement commands , if response OK, light up led or something
      next one!
  */

  wdt_disable();  // Disable the watchdog and wait for more than 2 seconds
  delay(3000);  // With this the Arduino doesn't keep resetting infinitely in case of wrong configuration
  wdt_enable(WDTO_8S);

  pinMode(SDERRORLED, OUTPUT);

  sdi_serial_connection.begin();
  Serial.begin(9600);
  while (!Serial) {
    delay(10);
  }

  wdt_reset();
  delay(3000);
  /* startup delay to allow sensors to powerup, and
      output DDI serial strings
  */

  /*
      ToDo: Few rounds of single measurements to check that all sensors are OK.
  */

  Controllino_RTC_init(0);
  Controllino_SetTimeDate(1, 1, 1, 0, 00, 00, 00); // set initial values to the RTC chip
  // (Day of the month, Day of the week, Month, Year, Hour, Minute, Second)
  delay(500);
  getDateAndTime();

  /*
    ToDo: perhaps iterate over sensorArray[] to go through these?
  */

  int8_t respondingSensors = 0;
  if (sensor0.sensorGetReading()) respondingSensors++;
  if (sensor1.sensorGetReading()) respondingSensors++;
  if (sensor2.sensorGetReading()) respondingSensors++;
  if (sensor3.sensorGetReading()) respondingSensors++;
  if (sensor4.sensorGetReading()) respondingSensors++;
  if (sensor5.sensorGetReading()) respondingSensors++;

  DPRINTLN();
  DPRINTLN("------------------------------------------------------------");
  DPRINTLN("            Simple Soil Logger starting!");
  DPRINTLN("------------------------------------------------------------");
  DPRINTLN("           DEBUG PRINTING TO SERIAL IS ON!");
  DPRINT("                 ");
  DPRINTLN(dateAndTimeData);
  DPRINT("Sensors in program:  ");
  DPRINTLN(sizeof sensorArray / sizeof sensorArray[0]);
  DPRINT("Sensors responding:  ");
  DPRINTLN(respondingSensors);
  DPRINT("sdMeasurementDirName:  ");
  DPRINTLN(sdMeasurementDirName);
  DPRINT("sdMeasurementFileName: ");
  DPRINTLN(sdMeasurementFileName);
  DPRINT("sdLogDirName:          ");
  DPRINTLN(sdLogDirName);
  DPRINT("sdLogFileName:         ");
  DPRINTLN(sdLogFileName);
  DPRINTLN("------------------------------------------------------------");

  //----------------------------------------
  // writing header line to data file
  //----------------------------------------

  wdt_reset();
  sprintf(sdDataLine, "Start time,End time,");
  for (int8_t i = 0; i < sizeof sensorArray / sizeof sensorArray[0]; i++) {
    strcat(sdDataLine, "Address,DeP Mean,Dep StDev,EC Mean,EC StDev,Temp Mean, Temp StDev, Measurements");
    if (i < (sizeof sensorArray / sizeof sensorArray[0] - 1)) {
      strcat(sdDataLine, delimiter);
    }
  }

  headerLine = true;
  sdWrite(sdFat, sdMeasurementDirName, sdMeasurementFile, sdMeasurementFileName, sdDataLine, headerLine);

  //----------------------------------------
  // writing system start line to log file
  //----------------------------------------
  wdt_reset();
  strcpy(sdDataLine, dateAndTimeData);
  strcat(sdDataLine, delimiter);
  strcat(sdDataLine, "Hello world. I start now. Number of sensors in program:");
  strcat(sdDataLine, delimiter);
  int8_t i;
  char sensorNumber[4];
  for (i = 1; i < sizeof sensorArray / sizeof sensorArray[0]; i++);
  sprintf(sensorNumber, "%d", i);
  strcat(sdDataLine, sensorNumber);
  strcat(sdDataLine, delimiter);
  strcat(sdDataLine, "Responding sensors at start:");
  strcat(sdDataLine, delimiter);
  sprintf(sensorNumber, "%d", respondingSensors);
  strcat(sdDataLine, sensorNumber);

  headerLine = false;
  sdWrite(sdFat, sdLogDirName, sdLogFile, sdLogFileName, sdDataLine, headerLine);

  //----------------------------------------
  // getting measurements from all 6 sensors
  // writing data line to data file
  //----------------------------------------

  getDateAndTime();

  rtcMinute = Controllino_GetMinute();
  rtcSecond = Controllino_GetSecond();

  strcpy(beginRoundTime, dateAndTimeData);

  for (int16_t repeats = 0; repeats < measurementsInRound; repeats++) {
    wdt_reset();

    sensor0.sensorGetReading();
    sensor1.sensorGetReading();
    sensor2.sensorGetReading();
    sensor3.sensorGetReading();
    sensor4.sensorGetReading();
    sensor5.sensorGetReading();
  }

  wdt_reset();
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

  headerLine = false;
  sdWrite(sdFat, sdMeasurementDirName, sdMeasurementFile, sdMeasurementFileName, sdDataLine, headerLine);

}

/*------------------------------------------------------------------------------
   Checks for RTC for set minutes to start measurement round,
   gets readings from sensors for set repeats per round,
   concatenates round's start and end time, calculated measurement values, and
   calls sdwrite() to save measurement round's data to SD card.
  ------------------------------------------------------------------------------
*/
void loop() {
  wdt_reset();

  rtcMinute = Controllino_GetMinute();
  rtcSecond = Controllino_GetSecond();

  /*
     ToDo: bool minuteChecker(rtcMinute) function that iterates over array of int8_t minutes,
     returns TRUE if any match to current rtcMinute. Then put minuteChecker(rtcMinute) function
     call to if() below here.
  */

  // if (latestRoundStartMinute != rtcMinute && ( rtcMinute == 0 || rtcMinute == 30 )) { // rtcMinute == 10 || rtcMinute == 20 || rtcMinute == 30 || rtcMinute == 40 || rtcMinute == 50)) {
  if (latestRoundStartSecond != rtcSecond && ( rtcSecond == 0 || rtcSecond == 10 || rtcSecond == 20 || rtcSecond == 30 || rtcSecond == 40 || rtcSecond == 50 )) {
    latestRoundStartMinute = rtcMinute;
    latestRoundStartSecond = rtcSecond;
    getDateAndTime();
    strcpy(beginRoundTime, dateAndTimeData);

    for (int16_t repeats = 0; repeats < measurementsInRound; repeats++) {
      wdt_reset();

      /*
         ToDo: perhaps iterate over sensorArray[] to go through these?
      */

      sensor0.sensorGetReading();
      sensor1.sensorGetReading();
      sensor2.sensorGetReading();
      sensor3.sensorGetReading();
      sensor4.sensorGetReading();
      sensor5.sensorGetReading();
    }

    wdt_reset();
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

    headerLine = false;
    sdWrite(sdFat, sdMeasurementDirName, sdMeasurementFile, sdMeasurementFileName, sdDataLine, headerLine);

  }
}
