#include <math.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include "L3G.h"
#include "LSM303.h"
#include "sensor.c"
#include "gyro.h"
#include "GPIOpinsMotorConstroller.h"
#include "i2c-dev.h"
#include "encoders.c"

#define X   0
#define Y   1
#define Z   2
#define DT 0.035         // [s/loop] loop period
#define AA 0.98         // complementary filter constant

#define A_GAIN 0.0573      // [deg/LSB]
#define G_GAIN 0.07        // [deg/s/LSB]
#define MAX_RATE 180.0     // [deg/s]
#define RAD_TO_DEG 57.29578
#define M_PI 3.14159265358979323846

#define ON 1
#define OFF 0




void  INThandler(int sig)
{
	setMotorSpeeds(0,0,0,0,0);
	printf("STOP");
        signal(sig, SIG_IGN);
        shutBcmDown();
        exit(0);
}

int mymillis()
{
	struct timeval tv; 
	gettimeofday(&tv, NULL);
	return (tv.tv_sec) * 1000 + (tv.tv_usec)/1000;
}

int timeval_subtract(struct timeval *result, struct timeval *t2, struct timeval *t1)
{
    long int diff = (t2->tv_usec + 1000000 * t2->tv_sec) - (t1->tv_usec + 1000000 * t1->tv_sec);
    result->tv_sec = diff / 1000000;
    result->tv_usec = diff % 1000000;
    return (diff<0);
}

int main(int argc, char *argv[])
{


        float KP = 0;
        float KI = 0;
        float KD = 0;
        float KW = 0;
        signed int FIX = 0;
        float SS = 0;
        float CALIBRATE = 0;



	char *cvalue = NULL;
	int c;
	opterr = 0;
        while ((c = getopt (argc, argv, "x:s:c:h")) != -1)
        switch (c){
                case 'x':
                        FIX = atof(optarg);
                        break;
                case 's':
                        SS = atof(optarg);
                        break;
                case 'c':
                        CALIBRATE = atof(optarg);
                        break;

                case 'h':
			printf ("\nUsage;\t gyro [xsc] KP KI KD  \n-x X Accelerometer fix value\n-s Stand still vlaue\n-c Calibrate wheel value\n-h This help\n");
                        abort();

                case '?':
                        if (optopt == 'x'|| optopt == 's'||optopt == 'c'){
                                fprintf (stderr, "Option -%c: requires an argument.\n", optopt);
				printf ("\nUsage;\t gyro [xsc] KP KI KD  \n-x X Accelerometer fix value\n-s Stand still vlaue\n-c Calibrate wheel value\n-h This help\n");

                        }
                        else if (isprint (optopt)){
                                printf ("Unknown option `-%c'.\n", optopt);
                        }
                        else
                                fprintf (stderr,
                                "Unknown option character `\\x%x'.\n",
                                optopt);
                                return 1;
                default:
                        abort ();
                }
	if (argc-optind == 3){
        	KP = atof(argv[argc-3]);
	        KI = atof(argv[argc-2]);
	        KD = atof(argv[argc-1]);
	}
	else {
		printf("\n##number of required values not entered, use -h for help##\n");
	exit(1);
	}





	float rate_gyr_y = 0.0;   // [deg/s]
	float rate_gyr_x = 0.0;    // [deg/s]
	float rate_gyr_z = 0.0;     // [deg/s]

	signed int accel_x_zero = 0;
	signed int accel_y_zero = 0;
	signed int accel_z_zero = 0;
	signed int gyro_y_zero = 0;
	signed int gyro_x_zero = 0;
	signed int gyro_z_zero = 0;

	int  *Pacc_raw;
	int  *Pmag_raw;
	int  *Pgyr_raw;
	int  acc_raw[3];
	int  mag_raw[3];
	int  gyr_raw[3];
	Pacc_raw = acc_raw;
	Pmag_raw = mag_raw;
	Pgyr_raw = gyr_raw;

	int output = 0;

	float gyroXangle = 0.0;
	float gyroXangleLast = 0.0;
	float gyroYangle = 0.0;
	float gyroZangle = 0.0;
	float AccYangle = 0.0;
	float AccXangle = 0.0;
	float CFangleX = 0.0;
	float error = 0.0;
	float lastError = 0.0;
	float targetAngle = 0.0;
	float StandStillOffset = 0.0;	//Used to change the angle to force PiBBOT to balance on the spot.

	int startInt  = mymillis();
	struct  timeval tvBegin, tvEnd,tvDiff;


	signed int acc_y = 0;
	signed int acc_x = 0;
	signed int acc_z = 0;
	signed int gyr_x = 0;
	signed int gyr_y = 0;
	signed int gyr_z = 0;

	float pTerm = 0.0;
	float iTerm = 0.0;
	float dTerm = 0.0;
	float lastAngle = 0.0;

//	int wheelVelocity = 0;
	int wheelPosition = 0;
//	int lastWheelPosition = 0;
	int leftWheelVelocity = 0;
	int rightWheelVelocity = 0;
	int lastLeftEncoderValue = 0;
	int lastRightEncoderValue = 0;

	int motorPower = ON;
	int motorPowerTimer = mymillis();
	int turnTimer = mymillis();
	int turn = OFF;
	int moveTimer = mymillis();

        signal(SIGINT, INThandler);

	enableIMU();
	setupEncoders();
        SetPinsOut();
	setUpMotor();

	gettimeofday(&tvBegin, NULL);

	int LCDtimer = mymillis();
	int LCDFlashBlue = mymillis();
	int LCDFlashRF = mymillis();
	int i;
	//####################################################
	//####################################################
	//####################################################
	for (i = 0; i < 100000; i++)
	{

	startInt = mymillis();


	//read ACC and GYR data
	readMAG(Pmag_raw);
	readACC(Pacc_raw);
	readGYR(Pgyr_raw);

  	//Remove offset from raw values.  (im not sure if this is even needed)
  	acc_x = ( *acc_raw - accel_x_zero );
  	acc_y = (*(acc_raw+1) - accel_y_zero);
  	acc_z = (*(acc_raw+2) - accel_z_zero);

  	//Remove offset from raw values.  (im not sure if this is even needed)
	gyr_y = (*(gyr_raw+1) - gyro_y_zero);
  	gyr_x = (*gyr_raw - gyro_x_zero);
	gyr_z = (*(gyr_raw+2) - gyro_z_zero);

	//Convert Gyro raw to degrees per second
	rate_gyr_y = (float) gyr_y * G_GAIN;
	rate_gyr_x = (float) gyr_x * G_GAIN;
	rate_gyr_z = (float) gyr_z * G_GAIN;



	//Calculate the angles from the gyro
	gyroXangle+=rate_gyr_x*DT;
	gyroYangle+=rate_gyr_y*DT;
	gyroZangle+=rate_gyr_z*DT;




	//Convert Accelerometer values to degrees
	AccXangle = (float) (atan2(acc_y,acc_z)+M_PI)*RAD_TO_DEG;
	AccYangle = (float) (atan2(acc_x,acc_z)+M_PI)*RAD_TO_DEG;

	//Fix the X on the Accelerometer. This needs to be done if the IMU is not level on PiBBOT
	AccXangle += FIX;


	//Change the rotation value of the accelerometer to -/+ 180
	if (AccXangle >180)
	{
		AccXangle -= (float)360.0;
	}
	if (AccYangle >180)
		AccYangle -= (float)360.0;

	//Complementary filter used to combine the accelerometer and gyro values.
	CFangleX=AA*(CFangleX+rate_gyr_x*DT) +(1 - AA) * AccXangle;

	//We are not interested in tracking the wheels if PiBBOT is turning.
	if (turn){
                targetAngle = 0;
                leftEncoderValue = 1;
                rightEncoderValue = 1;
                lastLeftEncoderValue = 1;
                lastRightEncoderValue = 1;
	}
        else{
		leftWheelVelocity = lastLeftEncoderValue - leftEncoderValue;
		rightWheelVelocity = lastRightEncoderValue - rightEncoderValue;
		lastLeftEncoderValue = leftEncoderValue;
		lastRightEncoderValue = rightEncoderValue;
		wheelPosition = (leftEncoderValue + rightEncoderValue)/2;
	}
	printf("leftWheelV %d  rightWheelV %d  ",leftWheelVelocity,rightWheelVelocity);

	//manipulate the angle so that PiBBOT balance on the spot.
	//If PiBBOT is moving we are not worried about it standing still. 
	 if (targetAngle == 0){
		StandStillOffset = wheelPosition * SS;
	}
	else{
	StandStillOffset = 0;
	}





/*	if (digitalRead (RF1) == 1) KP += 1;
	if (digitalRead (RF2) == 1) KI += 1;
	if (digitalRead (RF3) == 1) KP -= 1;
	if (digitalRead (RF4) == 1) KI -= 1;*/
	if (digitalRead (RF1) == 1) targetAngle += 1;
	if (digitalRead (RF2) == 1) turn = ON;
	if (digitalRead (RF3) == 1) targetAngle -= 1;
//	if (digitalRead (RF4) == 1) KI -= 1;

	if (mymillis() - moveTimer < 1500){
		targetAngle = 0;
		leftEncoderValue = 1;
		rightEncoderValue = 1;
		lastLeftEncoderValue = 1;
		lastRightEncoderValue = 1;
	}

	//flash blue if button is pressed
	if (digitalRead (RF1) || digitalRead (RF2)|| digitalRead (RF3)  ){
                lcdColor(LCD_BLUE);
                LCDFlashRF = mymillis() ;
        }
        if  (mymillis() - LCDFlashRF > 300){
                if (motorPower == ON){
			lcdColor(LCD_GREEN);
                }
                else{

                        lcdColor(LCD_RED);
                }
        }




	if ((turn)&& (mymillis() - turnTimer > 1500)){
		turn = OFF;
		turnTimer = mymillis();
	}


	error =  CFangleX - (targetAngle + StandStillOffset);
	pTerm = KP * error;
	iTerm += KI * error;
	dTerm = KD * (error - lastError);
	lastError = error;
	output = pTerm + iTerm + dTerm;




	// Clip as float (to prevent wind-up).
	if(iTerm < -128.0) { iTerm = -128.0; } 
	if(iTerm > 128.0) { iTerm = 128.0; }
	if(output < -127.0) { output = -127.0; } 
	if(output > 127.0) { output = 127.0; }

//	Comment out the below line to view angles in the CLI
//	printf ("GyroX  %7.3f | AccXangle %7.3f | CFangleX %7.3f | output %4d | P %8.3f | I %8.3f | D %8.3f | KP %8.3f | KI %8.3f",gyroXangle,AccXangle,CFangleX,output, pTerm, iTerm, dTerm, KP, KI);

	if  (CFangleX > 25 || CFangleX <-25) {
		setMotorSpeeds(0,0,0,0,0);
		output = 0;
	}
        if (CFangleX < 0 && motorPower==ON){
		setMotorSpeeds(output,leftWheelVelocity,rightWheelVelocity,CALIBRATE,turn);
	}
	else if (CFangleX > 0 && motorPower==ON){
		setMotorSpeeds(output,leftWheelVelocity,rightWheelVelocity,CALIBRATE,turn);
        }



	if (digitalRead(RESET_GYRO)== HIGH){
		printf ("## Resetting Gyro ##");
		gyroXangle=0.0;
		iTerm = 0.0;
		lcdColor(LCD_BLUE);
		LCDFlashBlue = mymillis() ;
	}
	if  ((mymillis() - LCDFlashBlue > 500) && (mymillis() - LCDFlashRF > 300)){
		if (motorPower == ON){
			lcdColor(LCD_GREEN);
		}
		else{

			lcdColor(LCD_RED);
		}
	}

	if ((digitalRead(STOP_START)== HIGH) && ((mymillis() - motorPowerTimer) > 1000))
	{
		if (motorPower == ON)
		{
			printf ("## Motors OFF ##");
			motorPower = OFF;
			setMotorSpeeds(0,0,0,0,0);
			lcdColor(LCD_RED);

		}
		else
		{
			printf ("## Motors ON ##");
			motorPower = ON;
			lcdColor(LCD_GREEN);
		}
			motorPowerTimer = mymillis();


	}

	printf("\n");
	//Only update lcd every 150ms
	if (mymillis() - LCDtimer > 100){
                lcdPosition (lcd, 0, 0) ; lcdPrintf(lcd,"GyroX         %6.1f", gyroXangle);
		lcdPosition (lcd, 0, 1) ; lcdPrintf(lcd,"AcceX         %6.1f", AccXangle);
		lcdPosition (lcd, 0, 2) ; lcdPrintf(lcd,"CFilterX       %5.0f", CFangleX);
		lcdPosition (lcd, 0, 3) ; lcdPrintf(lcd,"Output           %d", output);
	        LCDtimer = mymillis();
	}


	//Each loop should be at least DTms.
        while(mymillis() - startInt < (DT * 1000))
        {
            usleep(100);
        }

	printf("Lp=%d ", mymillis()- startInt);
    }

	setMotorSpeeds(0,0,0,0,0);

	//end
	gettimeofday(&tvEnd, NULL);
	// diff
	timeval_subtract(&tvDiff, &tvEnd, &tvBegin);
	printf("Time seconds:%ld.%06ld\n", tvDiff.tv_sec, tvDiff.tv_usec);
}

