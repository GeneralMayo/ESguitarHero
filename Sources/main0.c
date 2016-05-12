/*
Group: A5                                                         
Group Members and andrew id: Thomas Mayo-Smith tmayosmi/ Yiru Yao yiruy                                
Lab/Prelab: Lab 11 
File Name: main0.c
CPU/ Compiler: MC9S12C128 using HC12 compiler

Summary of contents:  
This program tests the users reaction speed by determining how accurate the
user's pressed a button given an LED indicator over many trials at varying
speeds. 

Last Modified: 5/6/2015
*/

#include <hidef.h>         /* common macros and defines */
#include <mc9s12c128.h>    /* derivative information */
#pragma LINK_INFO DERIVATIVE "mc9s12c128"

#include <stdio.h>
#include "lcd_lib.h"       //used for lcd display
#include "modclock.h"      //used to modify clk speed to 8 MHz

//define boolean variables
typedef char bool;
#define TRUE  1
#define FALSE 0
#define ON    1
#define OFF   0 

//shorten data type names
typedef   signed char   int8;
typedef unsigned char  uint8;
typedef   signed int   int16;
typedef unsigned int  uint16;
typedef   signed long  int32;
typedef unsigned long uint32;

// serial communication constants
#define RESPONSE_MAX_LEN 32 //maximum chars able to be recieved from serial input
#define BAUDINDEX 8 //38400 Baud rate 

// tasker/scheduler constants and typedefs
#define NTASKS  6
#define PERIOD0     50     // schedule tasks every .05s
#define dfltPERIOD1 1000   // default time to sroll LED's is 1sf
#define PERIOD2   1000     // display time every 1s
#define PERIOD3   10000    // set the bonus flag every 10s 
#define PERIOD5   900000   // the game loop runs "forever"   
#define PERIOD4   100      // the watchdog checking period is .1s 
typedef void (*Ptr2Function)(void); //for i 
// Process control block for tracking task status
typedef struct PCBstruct
{ Ptr2Function TaskPtr;  // points to start of task code
  bool   ReadyToLaunch;  // true when task is eligible to launched (i.e., called at initial entry point)
  bool   Running;        // true when task is actually running (post-launch)
  uint8 * SPsave;
  uint32 Period;         // period in msec
  uint32 NextTime;       // next start time in msec
} PCBstruct;

//game constants
#define BONUSBITS  3       //amount of individual reaction tests user will get
                           //bonus points for if he/she reacts correctly to the
                           //LED being on or off (each bit of data will determine
                           //weather the LED is on or off which is why it is called
                           //"bonusBits")
#define GAMETIME  120000   //the game will be 2 mins long
#define MAXSCROLLPERIOD 1000  //max time user has to react to an LED being on

// tasker/scheduler globals
uint8 CurrentTask = NTASKS; // current task executing is initialized to be main loop
PCBstruct PCB[NTASKS];   // allocate one extra entry for main loop SP save
// Reserve a separate stack area for each task
#define STACKSIZE  256            //  size is in bytes for each stack
uint8 STACK[NTASKS][STACKSIZE];   // Don't need a stack for main loop; already have one by default

//timer glbals
uint32 TimeInMsec;           // 32 bit integer msec of current time since system boot
                             // CAUTION -- this code doesn't handle rollover!  
uint32 CurrentTime;          // 8.24 fixed point clock ticks; integer portion is in msec

//watchdog globals
uint16 watch_flag;

//game globals
unsigned char speed;         //AD0 value determining scrolling speed
                             //of LED's
char timeOut;                //set when game is over
char started;                //indicates whether the game is started or not, used in timer interrupt    
int scrollPeriod;            //current LED scroll period (chosen by user) 
unsigned long int data;      //bits of "data" will be displayed on the LED's
                             //user is supposed to press button if the bit
                             //displayed on the left most LED is 1
uint shiftCounter;           //amount of times data has been shifted 
unsigned long int dataCOPY;  //used for "smooth" data reset
uint score;                   //holds users score during the game
uint32 gameStartTime;        //starting time of the current game
bool bonus = FALSE;          //flag indicating game is in bonus mode
bool timeIntervalNotUp;      //set to FALSE when user is out of time to react to
                             //an LED
char bonusBitCounter;        // amount bonus bits the user has reacted to (successfully/
                             // unsuccessfully)
char timeBuff[17];           // used to display current time
char strBuff[17];            // used to display everyting else
char buttonVal;              //1 when button was pressed by user 0 otherwise


//utilities
uint32 TimeNow(void);        // CAUTION -- enables interrupts as a side effect
void WasteMsec(unsigned int WaitTime);
void clearStrBuff(char *strBuff);

// serial communication stuff
void doSerialComm( void );
void serialSetup(char baudIndex);

//watchdog related functions
void Alive(uint16);
void kick(void);

//tasks and scheduler related functions
void Task0(void);    
void Task1(void);
void Task2(void);
void Task3(void);
void Task4(void);
void Taskw(void);
void InitPCB(void);
void interrupt 16 TimerHandler(void);
uint8 AddrHi(Ptr2Function Ptr);
uint8 AddrLo(Ptr2Function Ptr);
void TaskTerminate(void);
void CreateLaunchStack(uint8 Task);  
uint8 AddrHi(Ptr2Function Ptr);
uint8 AddrLo(Ptr2Function Ptr);
void TaskTerminate(void);
void CreateLaunchStack(uint8 Task);

void main(void);

//SCHEDULER
// Schedules task if it has been long enough since the last time it was
// ReadyToLaunch
void Task0(void){
	uint8 ThisTask;
	uint32 TempTime;
	
	TempTime = TimeNow();

   // for all tasks, launch task if time has come
   // Launch when period length has been satisfied

   // We don't protect access to the PCB since this is the highest priority task that should be messing with it
   // except for the tasker.  The tasker will ignore the PCB until the Running flag is set, so do that last   
   // For all tasks, launch task if time has come
   for (ThisTask = 1; ThisTask < NTASKS;  ThisTask++) 
   { 
     if( !PCB[ThisTask].Running                    // not already running
         && !PCB[ThisTask].ReadyToLaunch              // not already waiting to launch (don't double-launch)
         &&  PCB[ThisTask].NextTime <= TempTime       // and it's been at least one more period
       ) 
     { 
       PCB[ThisTask].ReadyToLaunch = TRUE;  // In case we catch a task switch while this is running
       PCB[ThisTask].NextTime += PCB[ThisTask].Period;

       // Set up fresh stack image for executing the newly launched task
       // Current stack already has RTI information saved for restarting old task; don't worry about it
       CreateLaunchStack(ThisTask);   // sets Running flag

       // Next time this task is run, tasker will use this saved stack info to do an RTI and launch task
     }
   }
   Alive(0x1);               //indicate function is working
}

//updates bits to be displayed on LED's
//Note: period this task gets called = amount of
//time user has to react to an LED
void Task1(void){
  //TRANSITION: Going --> UPDATE-DATA
  
  //STATE = Update
  //Data Update Block
  //Note: 7bits must always be displayed on the LED's  
  if (shiftCounter<25){       
    data = data >>1;         //update data
    shiftCounter+=1;         
  } 
  else {                     
   data |= (dataCOPY&0x1FFFFFF)<<7; //reset the data "smoothly"
   data = data >>1;                 //update data 
   shiftCounter = 0;                //reset shiftCounter
  }         
  
  timeIntervalNotUp = FALSE;        //time user has to react is up! 
                                                            
  PTT = (char) (~((data & 0x7F)<<1) - 1);   //leds will display updated bits  
                                            //Note: only last 7 bits of PTT are used
                                            //Note: data needs to be inverted as
                                            //result of circut setup               
                             
  Alive(0x2);                //indicate function is working
}


//display the time elapsed
void Task2(){
  uint32 timeElapsed;
  uint32 timeNow;
    
  timeNow = TimeNow();
  
  //check if there is no more time left in the game  
  timeElapsed = timeNow - gameStartTime;
  if(timeElapsed >= GAMETIME){
    timeOut =1;
  }
  
  //display time
  if(timeOut){
    sprintf(timeBuff,"Time Out");
  } else {                     
    sprintf(timeBuff,"T:%d",(int)(timeElapsed/1000));
  }
  DisableInterrupts;    
  lcdWriteLine(1,timeBuff);
  EnableInterrupts;
  
  Alive(0x4);                //indicate function is working
}

//Sets bonus flag every 10 seconds
//Note: bonus is a simple enough function such
//that watchdog does not need to "watch out for it"
void Task3(){
  bonus=1;  
}

//task 4 updates the state machine 
void Task4(){ 
  bool scored;
 
  started = 1;    //indicates the game program has been started
                  //Note: once it is set to 1 it will always be 1
                
  //GAME LOOP - this is where the game is run
  for(;;) {
    //disable timer interrupt while choosing speed
    //otherwise speed and score/timeElapsed will overwrite each other.
    TSCR2 &= 0x7F;
    
    //initialize/reset variables 
    scrollPeriod = 1000;   //default LED scroll period = 1sec  
    PCB[1].Period = 1000;                   
    score = 0;             
    bonusBitCounter = 0;   
    speed = 0;             
    timeOut = 0;           
    
    ATDCTL2 |= 0x2;          //enable analog interrupts so speed can be chosen
    ATDCTL5 =0x30;           //left justifed, unsigned, continuous conversion, multichannel,
                             //CC,CB,CA start at 0 so ATDDRH0 is 1st result register
                             //to hold conversion
                             
    //STATE = chooseSettings
    //Wait for user to choose speed/brightness and press start button.
    //ATDInterrupt will be called and will set scrolling speed
    for(;;){
      //wiring: push button 8 on the project board with port A bit 0
      if(!PORTA_BIT0){       //user pressed start
        ATDCTL2 &=0xFD;      //disable analog interrupts after speed is chosen
        break;  
      }
      
      //display speed user has chosen
      clearStrBuff(strBuff);
      sprintf(strBuff,"SP:0x%01X",speed);
      DisableInterrupts;
      lcdWriteLine(1,strBuff);    
      EnableInterrupts;
      
      //display the PWM value
      clearStrBuff(strBuff);
      sprintf(strBuff,"PWM:0x%01X",(PORTA & 0xF0)>>4);
      DisableInterrupts;
      lcdWriteLine(2,strBuff);    
      EnableInterrupts;
      
      kick();                //kick watch dog directly
                             //Note: can't use Alive(0x10) because timer is disabled      
    }
    
    PWMDTY0 = 3*(PORTA & 0xF0)>>4;  // set the PWM
    gameStartTime = TimeNow();      // get start time so elapsed time can be
                                    // later calculated
    PTT = (char) (~((data & 0x7F)<<1) - 1); //initialize the PTT with data
                                            //Note: data inverted due to circut
                                            //setup
                                            //Note: only using 1st 7bits of PTT
    //display initial score
    clearStrBuff(strBuff);
    sprintf(strBuff,"S:%d",score);
    DisableInterrupts;    
    lcdWriteLine(2,strBuff);
    EnableInterrupts;
                                            
    //TRANSITION: chooseSetting --> GOING      
    
    //STATE = GOING
    //The user is tested for response time/accuracy in this loop 
    for(;;){
      //enable timer interrupts so score and timeElapsed can be displayed
      TSCR2 |=0x80;
      
      //after (maxPeriod/speed) timeUp is changed to 1 indicating the user
      //doesn't have any more time to decide if the LED is on or off
      buttonVal = 0;         //initilize fbuttonVal to 0
      timeIntervalNotUp = TRUE; //initialize timeIntervalNotUp to 0
      scored = FALSE;
      
      while(timeIntervalNotUp){
        if(!PORTA_BIT2){   //if user thought LED was on
          //wiring: PORTA_BIT2 connects with push button 1
          buttonVal=1; 
        } 
        if(PORTA_BIT1){   //if user pressed reset
          //wiring: PORTA_BIT1 connects with SW1 bit 1
          break;
        }
        
        //TRANSITION: GOING --> SCORE  
        
        //STATE = SCORE
        //Score Update Block
        if((buttonVal) && (data&0x1) && !scored){
          if(bonus){
            score+=2;
            bonusBitCounter+=1; //update bonus bits counter
            
            //reset bonusBitCounter if bonus has ended
            if(bonusBitCounter == BONUSBITS){
              bonusBitCounter=0;
              bonus = FALSE;
            }
          }else{
            score+=1;
          }
          scored = TRUE;
          
          //display new score
          clearStrBuff(strBuff);
          sprintf(strBuff,"S:%d",score);
          DisableInterrupts;    
          lcdWriteLine(2,strBuff);
          EnableInterrupts;
        }    
      }
  
      //if the game is over
      if(timeOut){
        //TRANSITION: GOING --> waitForReset 
        TSCR2 &=0x7F; 
        break;
      }
    }//"GOING state loop" end
    
    //STATE = waitForReset
    for(;;){
      if(PORTA_BIT1){
        //TRANSITION: waitForReset --> chooseSettings
        break;
      }
      
      kick();                //kick watch dog directly
                             //Note: can't use Alive(0x10) because timer is disabled
    }
  }//GAME loop end 
}

//Kick Watchdog if all tasks are functioning.
void Taskw(void){
  if (watch_flag == 0x7) {
    kick();
    watch_flag = 0;
  }
}

//Modiy bit of watch_flag.
//INPUT: x is an unsighned integer with a single bit set
//indicating a particular function which is functioning
//properly
void Alive(uint16 x) {
  DisableInterrupts;
  watch_flag |= x;
  EnableInterrupts;
}

//kick the watch dog.
void kick(void){
  ARMCOP = 0x55;
  ARMCOP = 0xAA;
}

//clears the string buffer used for LCD writes
void clearStrBuff(char *strBuff){
  int i;
  for(i=0;i<17;i++){      //Note: max chars which can be written to LCD = 16
    strBuff[i]=0;
  }
}

/*
// "WasteMsec()" waits specified number of milliseconds based on time-wasting loop
// loop must be hand-tuned for compiler settings and hardware (currently 8 MHz CPU clock)
// set to slightly faster rather than slower than 1 msec to ensure schedulability
// for example:  msec(50)  waits slightly less than 50 msec
*/
void WasteMsec(unsigned int WaitTime)
{ volatile unsigned int i;   // i is volatile to force wasting time updating memory location
  while (WaitTime > 0)
  { WaitTime--;
    for (i = 0; i < 443; i++)  //28 = 443/16   
    { asm NOP;
      asm NOP;
      asm NOP;
      asm NOP;
    }
  }
}

// Initialize PCB entries for the tasks  (Note that PCB[NTASKS] doesn't need initialization)
void InitPCB(void){
  // Set up task pointers and initial run status
  PCB[0].TaskPtr = &Task0; 
  PCB[1].TaskPtr = &Task1;  
  PCB[2].TaskPtr = &Task2;
  PCB[3].TaskPtr = &Task3;
  PCB[4].TaskPtr = &Taskw;
  PCB[5].TaskPtr = &Task4;

  // Set up task periods
  // Period is in msec
  PCB[0].Period = PERIOD0; 
  PCB[1].Period = dfltPERIOD1; 
  PCB[2].Period = PERIOD2; 
  PCB[3].Period = PERIOD3;
  PCB[4].Period = PERIOD4;
  PCB[5].Period = PERIOD5;
  
  // Set task running status flags
  PCB[0].ReadyToLaunch = TRUE;     PCB[0].Running = FALSE;   // Always launch scheduler
  PCB[1].ReadyToLaunch = FALSE;    PCB[1].Running = FALSE;   
  PCB[2].ReadyToLaunch = FALSE;    PCB[2].Running = FALSE;   
  PCB[3].ReadyToLaunch = FALSE;    PCB[3].Running = FALSE;   
  PCB[4].ReadyToLaunch = FALSE;    PCB[4].Running = FALSE;   
  PCB[5].ReadyToLaunch = FALSE;    PCB[5].Running = FALSE;   

  // Set all NextTime values with same current time to initialize task timing
  PCB[0].NextTime = 0;
  PCB[1].NextTime = 0;
  PCB[2].NextTime = 0;
  PCB[3].NextTime = 0;
  PCB[4].NextTime = 0;
  PCB[5].NextTime = 0;
}


//get a clean copy of current time and return it
//OUTPUT: TempTime is the current time in ms 
uint32 TimeNow(void) 
{ uint32 TempTime;
  
  DisableInterrupts;
  TempTime = TimeInMsec;
  EnableInterrupts;
  
  return(TempTime);  
}

//Configure PWM, A/D, I/O Ports, Serial Communications and Timer.
//Initialize various globals.
//Wait to be interrupted by the timer.
void main(void) {
  static uint8 * SPtemp;     // makes it easier to in-line assembly to save SP value
  
  clockSetup();              
  lcdSetup();

   
  /************************************
   *       PWM CONGIFURATION          *
   ************************************/
  DDRP = 0xFF;             //port p = output
  MODRR = 0x1;             //bit 0 of port p is analog output
  DDRT = 0xFF;             //port t = output
  RDRT = 0x0;              //full drive strength output
  //default port output
  PTP = 0x0;               //ensures data bus is connected at start
  PTT = 0xFF;              //LED's are off
 
  PWME = 0x1;              //enable channel 1 PWM
  PWMPRCLK = 0x4;          //set clock rate to 500kHz 
  PWMCTL = 0x0;            //no concatenation
  PWMCAE &= 0xFE;          //left aligned
  PWMPER0 = 0xFF;          //Period = 0xFF
  PWMDTY0 = 0x80;          //Default duty cycle = 0x80
  
  /************************************
   *       A/D CONGIFURATION          *
   ************************************/
  ATDDIEN = 0xFE;          //AD0 = andalog; AD1-7 = digital
  DDRAD = 0x0;             //Port AD = input
  
  ATDCTL2 |=0x82;           //Enable interrupt on conversion completion
  ATDCTL3 = 0x12;           //sequence len = 2, non-FIFO, freeze after current conversion
  ATDCTL4 = 0x85;           //8bit resolution, phase 2 sample time = 2 ATD tics, 
                            //ATD freq = 666666... PRS = (8e6*.5/666666)-1 = 5  
  ATDCTL5 =0x30;            //left justifed, unsigned, continuous conversion, multichannel,
                            //CC,CB,CA start at 0 so ATDDRH0 is 1st result register
                            //to hold conversion
                            
  /************************************
   *       port A CONGIFURATION          *
   ************************************/
   DDRA = 0x00;              //port A is input

   /************************************
   *       Serial Communication        *
   ************************************/
  //I/0 for the serial communication; 
  //serial communication setup
  //call serial setup to configure control registers
  serialSetup(BAUDINDEX);    //confiures registers
                     
  /************************************
   *       TIMER CONGIFURATION        *
   ************************************/                           
  TSCR1 |= 0xE0;           //stop in freez and wait, timer enable 
  TSCR2 = 0x0;             //clock rate = 8 MHz
  
  //watchdog configureation
  COPCTL = 0x47;           //enable debugging mode
                           //needs to be kicked every 4sec (about)
  
  //initialize globals
  data = 0;
  doSerialComm();            //set "data"
  dataCOPY = data;           //copy is used to make scrolling
                             //smooth
  shiftCounter = 0; 
  started = 0;         
  
  //initialize task data which is used by scheduler/tasker
	InitPCB();

  EnableInterrupts;
  
  { asm    STS  SPtemp;  }   // Save main SP
  PCB[NTASKS].SPsave = SPtemp;  //initialize task info
  
  TSCR2 |=0x80;            //timer interrupt will launch game and all
                           //tasks required for its function
                             
  //wait to be interrupted by timer
  for(;;){
  }
}


/*
ATDInterrupt: converts user analog "potentiometer input" to a digital speed.
              then uses this speed to calculate the LED scrolling period
*/
void interrupt 22 ATDInterrupt( void ){
  //clear sequence complete flag
  ATDSTAT0 = 0x80;
 
  if((ATDSTAT0&0x7)== 0x0){
    speed = ATDDR0H/16;
    
    //zero speed will result in div 0 error
    if(speed==0){
      scrollPeriod = dfltPERIOD1;
    //set speed normally
    } else{
      scrollPeriod = (int)(MAXSCROLLPERIOD/speed);
    }
    PCB[1].Period = scrollPeriod;
  }
}


// Timer ISR
// Increment the timer counter and update current time in msec when it increments
// Timer ISR is also the tasker
#define TIMEINCR 0x083126E9   // 137438953  => just over 8 msec per TCNT rollover   8 MHz  divider of 1

void interrupt 16 TimerHandler(void) 
{ 
    //preemptive tasks
   static uint8 * SPtemp;  // makes it easier to in-line assembly to save SP value
   static uint8 i;  // temp var, but make it static to avoid stack complications
  
   TFLG2 = 0x80;  // Clear TOF; acknowledge interrupt
  
   CurrentTime += TIMEINCR;  // Increment time using 8.24 fixed point  (i.e., integer scaled to 2**-24 secs)
   TimeInMsec += (CurrentTime >> 24) & 0xFF;  // add integer portion to msec time count
   CurrentTime &= 0x00FFFFFF;                 // strip off integer portion, since that went into TimeInMsec
   
   
   // This is the tasker in preemptive mode; switch to highest priority running task
   
   // Save current SP
   { asm    STS  SPtemp;  }
   PCB[CurrentTask].SPsave = SPtemp;

   // Set SP to main loop stack during scheduling to avoid crash when
   // scheduling Task 0
   SPtemp = PCB[NTASKS].SPsave;
   { asm    LDS  SPtemp; } 

   
   // Check to see if it is time to run the scheduler  
   if(    !PCB[0].Running                   // it's not already running
       &&  PCB[0].NextTime <= TimeInMsec    // and it's been a period since last launch
       ) 
     { 
       PCB[0].NextTime += PCB[0].Period;
       if(PCB[0].NextTime <= TimeInMsec)
       { PCB[0].NextTime = TimeInMsec + PCB[0].Period;  // Catch up if behind
       }

       // Create a launch image on the scheduler's stack
       CreateLaunchStack(0);            
     } 
   if(  PCB[0].NextTime <= CurrentTime    //it's been at least one more period
       ) 
   { 
     PCB[0].TaskPtr();
     PCB[0].ReadyToLaunch = FALSE;      // In case we catch a task switch while this is running
     PCB[0].NextTime += PCB[0].Period;  //catch up if behind
   }

   if (started == 0){
    CurrentTask = 4;  //manually start the game loop first for user to select speed and press start
   }else{
    CurrentTask = 1;  //task starts at 1 instead of 0
   }
   
   // Execute tasks in prioritized order
   for (i = NTASKS; i>0; i--)  // iterate enough times to check all tasks
   { 
     if (PCB[CurrentTask].Running) { break; } // found next active task
     CurrentTask++;   // advance to next task based on their priority
   }  // end EITHER when we've found a running task or have checked all tasks
   
   if (!PCB[CurrentTask].Running) 
   { CurrentTask = NTASKS;  // if no tasks are running, default to main loop
   }  
   
   // Have found the highest priority running task OR have defaulted to main loop task
   // (this is the only place we use the main loop PCB entry, but it saves having special code for that case)
   SPtemp = PCB[CurrentTask].SPsave;
   
   // Restore SP for this newly selected task (or same task that was just interrupted, depending on situation)
   { asm    LDS  SPtemp; }
   
   // RTI at end of this ISR starts up the newly selected task 
}



// Helper functions to extract low and high byte of function addresses to save on return stack
uint8 AddrHi(Ptr2Function Ptr)
{ return ( ((uint16)(Ptr)>>8) & 0xFF); }

uint8 AddrLo(Ptr2Function Ptr)
{ return ( (uint8)(Ptr) & 0xFF ); }


// Call when task terminates after running
// The idea here is that when a finite-duration task completes, it does an RTS to somewhere
// but that RTS isn't to the main loop, because it was never actually called from the main loop.
// So we fake it by letting the RTS actually be a "goto" to TaskTerminate which cleans up the
// task and just loops forever.  At the end of the current time slice TaskTerminate never
// gets restarted since the Running flag for that task has been set to false.
void TaskTerminate(void)
{ PCB[CurrentTask].Running = FALSE;
  
  for(;;){}   // there is no return address for us, so loop until tasker takes CPU away from us
}

// Create a task launch image onto the task's stack
//    Launch stack consists of:    TOP:  ->  return address to start TaskTerminate
//                                       ->  return address to restart suspended task
//                                       ->  Dummy values to restore registers if an RTI is executed
// This needs to be called every time you launch a stack to set up a clean stack image for that task
// (Note that the pointer to TaskTerminate probably never gets altered, but the launch address
// is overwritten as soon as the RTI to launch the task is executed the first time.)
//
void CreateLaunchStack(uint8 Task) 
{ static uint16 StackIdx; 
  
  // Set up fresh stack image for executing the newly launched task
  // Current stack already has RTI information for restarting old task; don't worry about it
  StackIdx = STACKSIZE;
  STACK[Task][--StackIdx] = AddrLo(&TaskTerminate);   // return to termination routine when completed
  STACK[Task][--StackIdx] = AddrHi(&TaskTerminate); 
  //address of task to launch
  STACK[Task][--StackIdx] = AddrLo(PCB[Task].TaskPtr);
  STACK[Task][--StackIdx] = AddrHi(PCB[Task].TaskPtr);
  //Dummy values for registers RTI would be restoring
  STACK[Task][--StackIdx] = 0;
  STACK[Task][--StackIdx] = 0;
  STACK[Task][--StackIdx] = 0;
  STACK[Task][--StackIdx] = 0;
  STACK[Task][--StackIdx] = 0;
  STACK[Task][--StackIdx] = 0;
  STACK[Task][--StackIdx] = 0;
   
  // Tag task for launch by putting info into the task's PCB entry
  //
  PCB[Task].SPsave = &STACK[Task][StackIdx];  // points to newly created stack data
  PCB[Task].ReadyToLaunch = FALSE;            // we just launched it; don't relaunch right away
  PCB[Task].Running = TRUE;                   // setting this TRUE tells tasker it is fair game to run

  // That's it; the task will start running at its initial entry point as soon as the tasker sees it
}


/*
    This function initilizes the SCI control registers.  Baud rate
    is determined by looking up the baudIndex parameter in a switch statement.

*/
void serialSetup(char baudIndex) {
  //case 0 - 300 baud
  //case 1 - 600 baud
  //case 2 - 1200 baud
  //case 3 - 2400 baud
  //case 4 - 4800 baud
  //case 5 - 9600 baud
  //case 6 - 14400 baud
  //case 7 - 19200 baud
  //case 8 - 38400 baud
  //case 9 - 56000 baud
  //case 10 - 115200 baud
  //default - 2400 buad
  
  //formula used to set SCIBDH/L 
  //SCIBDH/L = (module clk)/16*(baud rate)  
  switch(baudIndex){
    case 0 :
      //1667 = 0x683
      SCIBDL = 0x83;
      SCIBDH = 0x6;
      break;
    case 1 :
    //833 = 0x341...
      SCIBDL = 0x41;
      SCIBDH = 0x3;
    break;
    case 2 :
      SCIBDL = 0xA1;
      SCIBDH = 0x1;
      break;
    case 3 :
      SCIBDL = 0xD0;
      SCIBDH = 0;
      break;
    case 4 :
      SCIBDL = 0x68;
      SCIBDH = 0;
      break;
    case 5 :
      SCIBDL = 0x34;
      SCIBDH = 0;
      break;
    case 6 :
      SCIBDL = 0x23;
      SCIBDH = 0;
      break;
    case 7 :
      SCIBDL = 0x1A;
      SCIBDH = 0;
      break;
    case 8 :
      SCIBDL = 0xD;
      SCIBDH = 0;
      break;
    case 9 :
      SCIBDL = 9;
      SCIBDH = 0;
      break;
    case 10 :
      SCIBDL = 4;
      SCIBDH = 0;
      break;
    default :
      SCIBDL = 0xD0;
      SCIBDH = 0;
      break;
  }
  
  // configure SCICR1 and SCICR2 for 8 bit, one stop bit, no parity
  // set to default
  SCICR1 = 0x0;
  // enable the transmitter
  SCICR2 = 0xC;
}


//Receive bytes and copy them into 'response' string until
//the '9' character is received or the RESPONSE_MAX_LEN
//is reached.
void doSerialComm(void ) {
  char response[RESPONSE_MAX_LEN+1]; //string to hold response value
  char i,j;                          //counters  

  //set both transmitter and receiver
  SCICR2 = 0xC;
  j=0;
  
  //get response from serial input
  while(j<=RESPONSE_MAX_LEN){
    if(SCISR1_RDRF){
      //read from the SCIDRL
      response[j] = SCIDRL;
      if(response[j]=='9'){  //reached end of response
        i=j;                 //save length of response
        j=RESPONSE_MAX_LEN;  //break out of loop
      }
      j = j+1;
    }
  }
  
  response[i] = '\0';        //set null byte
  j = 0;
  //convert response to unsigned long int type
  while(j<i){
    if (response[j] == '1'){
      data+=(unsigned long int)2^j;
    }
    j++;
  }
}


