#include <SPI.h>
#include <Controllino.h>
#include <SDISerial.h>


/*
Some useful commands from 5TE Integrator Guide, page 6:

aI! 
where a stands for address, I is for Info
e.g. 2I! - returns sensor info from sensor with address 2 
returns, for example: 213DECAGON 5TE   400

aAB!
where a stands for old address, A is for Address, and B stands for new address
e.g. 2A4! changes address from 2 to 4
returns new sensor address: 4

?!
query for address - if there is more than one sensor this causes bus contention and corrupts data line
returns address, for example: 4
*/

//in order to recieve data you must choose a pin that supports interupts
#define DATALINE_PIN CONTROLLINO_D0
#define INVERTED 1
#define MAX_TIMEOUT 1000
//see:   http://arduino.cc/en/Reference/attachInterrupt
//for pins that support interupts (2 or 3 typically)


SDISerial connection(DATALINE_PIN, INVERTED);
char b_in[125];
char output_buffer[255];
char tmp_buffer[10];


void wait_for_message(char* buffer,char terminal){
   Serial.println("Waiting For input...");
   int i=0;
   while( true){
     if(!Serial.available())delay(500);
     else{
        buffer[i] = Serial.read();
        if(buffer[i] == terminal){
          buffer[i+1] = '\0';
          return;
        }
        i+=(buffer[i] >= 32 && buffer[i] <= 127);
     } 
   }
   
}

void setup(){
   Serial.begin(9600);
   connection.begin();
   delay(1000);
   Serial.println("Initialization Complete");
   
}
void loop(){
   
   wait_for_message(b_in,'!');
   sprintf(output_buffer,"[out]:%s",b_in?b_in:"No Output");
   Serial.println(output_buffer);
   char *response =connection.sdi_query(b_in,MAX_TIMEOUT);
   sprintf(output_buffer,"[in]:%s",response?response:"No Response");
   Serial.println(output_buffer);
   Serial.flush();
   delay(1000);
   
}
