/*
 * Spectrum Analyzer main sketch
 *
 * Under GPL
 *
 * Copyright 2014 - Ashhar Farhan
 */

/* CALIBERATION of Si570 :
1. Open Tools> Serial Monitor and send "f10000000" to Specan
2. This should set the Si570 to around 10 MHz. It up on 10 MHz band on a shortwave receiver. Note the exact frequency as M Hz
3. Calculate the actual new FREQ_XTAL = (old FREQ_XTAL * measured freq in Hz) / 10,000,000
4. Enter it nn the line below
*/
#define FREQ_XTAL   (114236576)

/* CALIBERATION of the Second Oscillator at 100 MHz:
  The 100 MHz oscillator is not exactly at 100 MHz. To set it up,
  1. Switch on a digital FM radio. Tune the radio to exactly 100 MHz
  2. Set the 2nd Oscillator trimmer until you can hear FM noise subsides with the 100 Mhz signal from the oscillator.
  3. At narrow band setting of the specan, scan crystal oscillator with a known frequency F1 Hz note the frequecey as F2 Hz
  4. Calculate new IF_FREQ = old IF_FREQ + F2 - F1
  5. Enter it on the line bellow
 */
#define IF_FREQ (111952000) // Spec-an IF offest is at 112 MHz


/* CALIBERATION of power readings
  1. switch to narrow back
  2. feed a -10dbm (100mv peak : NOT peak-to-peak!) signal to specan and note the reading.
  3. add the difference to the NB_POWER_CALIBERATION value. recompile, upload and ccheck that the reading is correct
  4. repeat for wide band caliberation as well
*/

#define NB_POWER_CALIBERATION (-116) /* set these for proper readings in narrow band mode, from -116 */
#define WB_POWER_CALIBERATION (-112) /* set these for proper readings in wide band mode  */


/*
 * Wire is only used from the Si570 module but we need to list it here so that
 * the Arduino environment knows we need it.
 */
#include <Wire.h>
#include <LiquidCrystal.h>

#include <avr/io.h>
#include <stdlib.h>

/* LiquidCrystal(rs, enable, d4, d5, d6, d7)  */
LiquidCrystal lcd(2, 3, 7, 6, 5, 4);
char printBuff[32], c[32], b[32];

/* manual tuning */
#define TUNING_POT (3)
#define LOG_AMP (2)
#define FN_BUTTON (A1)
#define NARROW_BAND A0
#define DEBOUNCE_TIME 0

int  dbm_reading = 100;
int power_caliberation = WB_POWER_CALIBERATION;
int tuningPosition;
unsigned long f_center=0, frequency=14150000, dco_freq=0, fromFrequency=14150000, toFrequency=30000000, stepSize=0, timePeriod=0;
unsigned long freq_xtal;
unsigned long tuningBase= 14000000;
unsigned long tuningSpeed = 5000;
unsigned long timeButtonDown = 0;
boolean isNarrowBand = false;
boolean isFineTuning = false;
boolean sweepBusy = false;
int btnState = HIGH;  

unsigned char si570_i2c_address = 0x55;
unsigned char dco_reg[13], dco_status='s';
unsigned long bitval[38];
unsigned int hs, n1;

/* serial port */
/* for reading and writing from serial port */
char serial_in[32];
unsigned char serial_in_count = 0;


/* display routines */
void printLine1(char const *c){
  if (strcmp(c, printBuff)){
    lcd.setCursor(0, 0);
    lcd.print(c);
    strcpy(printBuff, c);
  }
}

void printLine2(char const *c){
  lcd.setCursor(0, 1);
  lcd.print(c);
}

void updateDisplay(){
  
  sprintf(b, "%08ld", frequency);
  sprintf(c, "%.2s.%.4s %d.%ddbm  ",  b,  b+2, dbm_reading/10, abs(dbm_reading % 10));
  printLine2(c);
  if (isNarrowBand)
    strcpy(b, "RBW:1KHz   ");
  else
    strcpy(b, "RBW:300KHz ");
   if (isFineTuning)
     strcat(b, " FINE");
   else
     strcat(b, " WIDE");
   printLine1(b);
     
}


/* si570 routines */
/* 
IMPORTANT, the wire.h is modified so that the internal pull up resisters are not enabled. 
This is required to interface Arduino with the 3.3v Si570's I2C interface.
routines to interface with si570 via i2c (clock on analog pin 5, data on analog pin 4) */

void i2c_write (char slave_address,char reg_address, char data )  {
  int rdata = data;
  Wire.beginTransmission(slave_address);
  Wire.write(reg_address);
  Wire.write(rdata);
  Wire.endTransmission();
}

char i2c_read ( char slave_address, int reg_address ) {
  unsigned char rdata = 0xFF;
  Wire.beginTransmission(slave_address);
  Wire.write(reg_address);
  Wire.endTransmission();
  Wire.requestFrom(slave_address,1);
  if (Wire.available()) rdata = Wire.read();
  return rdata;
}

void read_si570(){
  //we have to read eight consecutive registers starting at register 5
  for (int i = 7; i <= 12; i++) 
    dco_reg[i] = i2c_read( si570_i2c_address, i);
}

void write_si570()
{
  int idco, i;
  
  // Freeze DCO
  idco = i2c_read( si570_i2c_address,137);
  i2c_write(si570_i2c_address, 137, idco | 0x10 );
  	
  i2c_write(si570_i2c_address, 7, dco_reg[7]);
  
  //Set Registers
  for( i=7; i <= 12; i++){
    i2c_write(si570_i2c_address, i, dco_reg[i]);
    idco = i2c_read( si570_i2c_address, i);
  }

  // Unfreeze DCO
  idco = i2c_read( si570_i2c_address, 137 );
  i2c_write (si570_i2c_address, 137, idco & 0xEF );
  
  // Set new freq
  i2c_write(si570_i2c_address,135,0x40);        
}

void qwrite_si570()
{   
  int i, idco;
  //Set Registers
  for( i=8; i <= 12; i++){
    i2c_write(si570_i2c_address, i, dco_reg[i]);
    idco = i2c_read( si570_i2c_address, i);
  }
}

void setBitvals(void){

	//set the rfreq values for each bit of the rfreq (integral)
	bitval[28] = (freq_xtal) / (hs * n1);
	bitval[29] = bitval[28] << 1;
	bitval[30] = bitval[29] << 1;
	bitval[31] = bitval[30] << 1;
	bitval[32] = bitval[31] << 1;
	bitval[33] = bitval[32] << 1;
	bitval[34] = bitval[33] << 1;
	bitval[35] = bitval[34] << 1;
	bitval[36] = bitval[35] << 1;
	bitval[37] = bitval[36] << 1;

	//set the rfreq values for each bit of the rfreq (integral)
	bitval[27] = bitval[28] >> 1;
	bitval[26] = bitval[27] >> 1;
	bitval[25] = bitval[26] >> 1;
	bitval[24] = bitval[25] >> 1;
	bitval[23] = bitval[24] >> 1;
	bitval[22] = bitval[23] >> 1;
	bitval[21] = bitval[22] >> 1;
	bitval[20] = bitval[21] >> 1;
	bitval[19] = bitval[20] >> 1;
	bitval[18] = bitval[19] >> 1;
	bitval[17] = bitval[18] >> 1;
	bitval[16] = bitval[17] >> 1;
	bitval[15] = bitval[16] >> 1;
	bitval[14] = bitval[15] >> 1;
	bitval[13] = bitval[14] >> 1;
	bitval[12] = bitval[13] >> 1;
	bitval[11] = bitval[12] >> 1;
	bitval[10] = bitval[11] >> 1;
	bitval[9] = bitval[10] >> 1;
	bitval[8] = bitval[9] >> 1;
	bitval[7] = bitval[8] >> 1;
	bitval[6] = bitval[7] >> 1;
	bitval[5] = bitval[6] >> 1;
	bitval[4] = bitval[5] >> 1;
	bitval[3] = bitval[4] >> 1;
	bitval[2] = bitval[3] >> 1;
	bitval[1] = bitval[2] >> 1;
	bitval[0] = bitval[1] >> 1;
}

//select reasonable dividers for a frequency
//in order to avoid overflow, the frequency is scaled by 10
void setDividers (unsigned long f){
  int i, j;
  unsigned long f_dco;
  
  for (i = 2; i <= 127; i+= 2)
    for (j = 4; j <= 11; j++){
      //skip 8 and 10 as unused
      if (j == 8 || j == 10)
        continue;
      f_dco = (f/10) * i * j;
      if (480000000L < f_dco && f_dco < 560000000L){
        if (hs != j || n1 != i){
          hs = j; n1 = i;
	  setBitvals();
        }
        //f_dco = fnew/10 * n1 * hs;
        return;
    }
  }
}

void setRfreq (unsigned long fnew){
  int i, bit, ireg, byte;
  unsigned long rfreq;

  //reset all the registers
  for (i = 7; i <= 12; i++)
    dco_reg[i] = 0;

  //set up HS
  dco_reg[7] = (hs - 4) << 5;
  dco_reg[7] = dco_reg[7] | ((n1 - 1) >> 2);
  dco_reg[8] = ((n1-1) & 0x3) << 6;

  ireg = 8; //registers go from 8 to 12 (five of them)
  bit = 5; //the bits keep walking down
  byte = 0;
  rfreq = 0;
  for (i = 37; i >= 0; i--){
    //skip if the bitvalue is set to zero, it means, we have hit the bottom of the bitval table
    if (bitval[i] == 0)
      break;

    if (fnew >= bitval[i]){
      fnew = fnew - bitval[i];
      byte = byte | (1 << bit);
    }
    //else{
    // putchar('0');
    //}

    bit--;
    if (bit < 0){
      bit = 7;
      //use OR instead of = as register[7] has N1 bits already set into it
      dco_reg[ireg] |= byte;
      byte = 0;
      ireg++;
    }
  }
}

void setDCO(unsigned long newfreq){
  
  if ((newfreq > f_center && newfreq - f_center < 50000L) ||
    (f_center > newfreq && f_center - newfreq < 50000L)){
    setRfreq(newfreq);
    dco_freq = newfreq;
    qwrite_si570();
//    dco_status = 's';
    return;
  }
  
  //else it is a big jump
  setDividers(newfreq);
  setRfreq(newfreq);
  f_center = dco_freq = newfreq;
  write_si570();
 
//  dco_status = 'b';
}

/*
void resetDividers(unsigned long newxtalfreq){
  setDividers(dco_freq);
  setRFreq(newfreq);
  write_si570();
}
*/

void setFrequency(unsigned long newfreq){
  frequency = newfreq;
}


char *readNumber(char *p, unsigned long *number){
  *number = 0;

  sprintf(c, "#%s", p);
  while (*p){
    char c = *p;
    if ('0' <= c && c <= '9')
      *number = (*number * 10) + c - '0';
    else 
      break;
     p++;
  }
  return p;
}

char *skipWhitespace(char *p){
  while (*p && (*p == ' ' || *p == ','))
    p++;
  return p;
} 


/* command 'h' */
void sendStatus(){
  Serial.write("helo v1\n");
  sprintf(c, "from %ld\n", fromFrequency);
  Serial.write(c);
   
  sprintf(c, "to %ld\n", toFrequency);
  Serial.write(c);

  sprintf(c, "step %ld\n", stepSize);
  Serial.write(c);

  sprintf(c, "period %ld\n", timePeriod);
 Serial.write(c);
}

void switchNarrowBand(boolean on){
  
    if (isNarrowBand == on)
      return;
      
    if (on == true){
      pinMode(NARROW_BAND, OUTPUT);
      digitalWrite(NARROW_BAND, HIGH);
      power_caliberation = NB_POWER_CALIBERATION;
      isNarrowBand = true;
    }
    else {
      pinMode(NARROW_BAND, OUTPUT);
      digitalWrite(NARROW_BAND, LOW);
      power_caliberation = WB_POWER_CALIBERATION;
      isNarrowBand = false;
    }
    /* debounce the relay */
    delay(200);
}

/* command 'g' to begin sweep 
  each response begins with an 'r' followed by the frequency and the raw reading from ad8703 via the adc */
void doSweep(){
  unsigned long x;
  int a;
  boolean oldFilter = isNarrowBand;

  sweepBusy = 1;
/*  if (stepSize <= 1000)
    switchNarrowBand(true);
  else 
    switchNarrowBand(false);
*/
  printLine1("*** Scanning ***");    
  Serial.write("begin\n");
  for (x = fromFrequency; x < toFrequency; x = x + stepSize){
    setDCO(x + IF_FREQ);
    delay(10);
    a = analogRead(LOG_AMP) * 2 + (power_caliberation * 10);
    //output in absolute 1/10th of a dbm
    sprintf(c, "r%ld:%d\n", x, a);
    Serial.write(c);
  }
  Serial.write("end\n");
  setDCO(fromFrequency);
  delay(10);
  doReading();
  updateDisplay();
  //chose the wideband again
  //switchNarrowBand(oldFilter);
  sweepBusy = 0;
}

/* command 'e' to end sweep */
void endSweep(){
  //to be done
}

void readDetector(){
  int i = analogRead(3);
  sprintf(c, "d%d\n", i);
  Serial.write(c);
}

void parseCommand(char *line){
  unsigned long param = 0;
  char *p = line;
  char command;

  while (*p){
    p = skipWhitespace(p);
    command = *p++;
    
    switch (command){
      case 'f' : //from - start frequency
        p = readNumber(p, &fromFrequency);
        setDCO(fromFrequency);
        sprintf(c, "*freq = %ld\n", f_center);
        Serial.write(c);
        break;
      case 'o':
        break;
      case 't':
        p = readNumber(p, &toFrequency);
        break;
      case 's':
        p = readNumber(p, &stepSize);
        break;
      case 'v':
        sendStatus();
        break;
      case 'g':
         doSweep();
         break;
      case 'r':
         readDetector();
         break;        
      case 'n':
         switchNarrowBand(true);
         break;
      case 'w':
         switchNarrowBand(false);
         break;
//      this is a one-time caliberation defined as FREQ_XTAL
//      case 'x':
//        p = readNumber(p, &freq_xtal);
//        break;
      case 'i': /* identifies itself */
        Serial.write("iSpecan 1.0\n");
        break;
    }
  } /* end of the while loop */
}

void acceptCommand(){
  int inbyte = 0;
  inbyte = Serial.read();
  
  if (inbyte == '\n'){
    parseCommand(serial_in);    
    serial_in_count = 0;    
    return;
  }
  
  if (serial_in_count < sizeof(serial_in)){
    serial_in[serial_in_count] = inbyte;
    serial_in_count++;
    serial_in[serial_in_count] = 0;
  }
}

boolean doButton(){
  int newState = digitalRead(FN_BUTTON);
  
  if (newState == btnState)
    return true;
    
  //if the new state is LOW
  if (btnState == HIGH && newState == LOW){
    timeButtonDown = millis();
    delay(50);
    //see if this was a debounce, then do nothing
    if (digitalRead(FN_BUTTON) == HIGH)
      return false;

    //so, now the button is still down, let's see what happens in 0.5 sec
    delay(400);
    //it was just a click
    if (digitalRead(FN_BUTTON) == HIGH)
    {
      if (isNarrowBand == false)
        switchNarrowBand(true);
      else 
        switchNarrowBand(false);
       updateDisplay();
      return false;    //don't tune!
    }  
    else { //if the button is still pressed
      if (isFineTuning == false)
        isFineTuning = true;
      else
        isFineTuning = false;
    }
  }
  //in other cases, update the button state
  btnState = newState;
  return true;
}

void doReading(){
  int new_reading = analogRead(LOG_AMP) * 2 + (power_caliberation * 10);
  if (new_reading != dbm_reading){
    dbm_reading = new_reading;
    updateDisplay();
  }
}

void doTuning(){
 unsigned long newFreq;
 
 int knob = analogRead(TUNING_POT)-10;
 unsigned long old_freq = frequency;
 
 //coarse tuning
 if (isFineTuning == false){
   tuningBase = 100000l * (unsigned long)knob;
   frequency = tuningBase;
 }
 //fine tuning
 else {
   frequency = tuningBase + 100l * (unsigned long)(knob);
 }

 //update only if needed
 if (frequency != old_freq){
   setDCO(frequency + IF_FREQ);
   doReading();
   updateDisplay();
 }
}


void setup(){
    // Initialize the Serial port so that we can use it for debugging
  Serial.begin(115200);
  Serial.println("*Specan v0.02\n");

  lcd.begin(16, 2);
  printBuff[0] = 0;
  printLine1("Specan v0.02");
  

  Serial.begin(9600); 
  Wire.begin();

  //configure the function button to use the internal pull-up
  pinMode(FN_BUTTON, INPUT);
  digitalWrite(FN_BUTTON, HIGH);
  
/*
  //switch to the 120 mhz fixed oscillator  
  pinMode(SI570_SWITCH, OUTPUT);
  digitalWrite(SI570_SWITCH, HIGH);
  delay(1000);
  
  // Force Si570 to reset to initial freq
  i2c_write(si570_i2c_address,135,0x01);
  delay(100);
  read_si570();  
*/
  Serial.write("*Fixed oscillator\n");
  sprintf(c, "%02x %02x %02x %02x %02x %02x\n", 
    dco_reg[7], dco_reg[8], dco_reg[9], dco_reg[10], dco_reg[11], dco_reg[12]);
  Serial.write(c);
  freq_xtal = FREQ_XTAL;
  setDCO(119978000L);

  //switch to the variable oscillator
 /* digitalWrite(SI570_SWITCH, LOW);
  delay(1000);
*/
  // Force Si570 to reset to initial freq
  i2c_write(si570_i2c_address,135,0x01);
  delay(100);
  read_si570();  

  Serial.write("*Variable oscillator\n");
  sprintf(c, "*regs: %02x %02x %02x %02x %02x %02x\n", 
    dco_reg[7], dco_reg[8], dco_reg[9], dco_reg[10], dco_reg[11], dco_reg[12]);
  Serial.write(c);

  setDCO(134200000L);
  delay(10);
}

void loop(){
  if (Serial.available()>0)
    acceptCommand();    
  else if (!sweepBusy) {
    //go to tuning only if the button is not being clicked
    if (doButton())
      doTuning();
    doReading();
    delay(100);
  }
}

