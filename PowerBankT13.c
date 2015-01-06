#define F_CPU 8000000UL //8Mhz internal RC oscillator for Tiny45

/*

Program Name: Powerbank_TINY
Author : NeutroN StrikeR aka N.Srinivas
Date : 01-09-2014
Email id:striker.dbz[at]hotmail.com

The software is provided with The MIT License (MIT)

Copyright (c) 2014 N.Srinivas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/


/* TO DO: 1.Check why when the device is off and i press button consecutively 2 or
3 times it shows poweroff animation.

The answer to above problem is that when poweroff is executed the device is in sleep.
But when we press many times the button for very small intervals the button isn't held long
enough to wake the device from sleep and execute the related ISR.
instead it goes into main loop.
But the poweron instruction is executed only via ISR so. The MOSFET is not turned on and
as a result the ADC is not able to get the battery voltage reading. So according to the(low power)
condition the device is turned off.

		  2.Check if we can reduce delay in shift1 system because it works properly
		  with original delay settings in 6.4Mhz i.e. 4/5 times of 8Mhz.

		  3.See if we can also show the battery level during charging. so that
		  battery increases from the current charge level.

		  This will require that we read the battery voltage during charging but we will not
		  get an proper reading and most of the time we will get a constant reading. Even
		  when the battery is at low voltage.

		  And since we don't have any extra pins available in the microcontroller. So we can't
		  control the charging input to the battery. Otherwise we could temporarily disconnect
		  the charger and measure the battery voltage.

*/

#include<avr/io.h>
#include<util/delay.h>
#include<avr/interrupt.h>


#define SHIFTPIN 3
#define guardtime(x) _delay_us(x)

#define SHIFT_PORT B

#define SHIFT_CONCAT(a, b)   a ## b
#define SHIFT_D(x) SHIFT_CONCAT(DDR,x)
#define SHIFT_P(x) SHIFT_CONCAT(PORT,x)

#define SHIFT     SHIFT_P(SHIFT_PORT)
#define SHIFTDDR  SHIFT_D(SHIFT_PORT)



void show_batt_level();
unsigned int adc_read();
unsigned char batterystatus();
void power_off();
void power_on();
void sleep();


void shift1()
{
	SHIFT &= ~(1<<SHIFTPIN);
	_delay_us(2);
	SHIFT |= (1<<SHIFTPIN);
	_delay_us(20);
}

//note that 74hc595 has more bandwidth then c4094  (i.e 4094 has a typical speed
//of 6Mhz where as hc595 has 100Mhz so its quiet fast, so is very sensitive to
//the pulse durations so i had to increase the delay period by nearly 10 times
//because i was using 10nF instead of required 2.2nF, well i could change it
//to just 5 times and it would still work but i just made 10 for just in case.

//i changed it again 5/4 times of original because the original worked with 6.4Mhz
void shift0()
{
	SHIFT &= ~(1<<SHIFTPIN);
	_delay_us(20);
	SHIFT |= (1<<SHIFTPIN);
	_delay_us(40);
}

void initshift1()
{
	SHIFTDDR |= (1<<SHIFTPIN);
	SHIFT |= (1<<SHIFTPIN);
}

void shift1_send(unsigned char data)
{

	uint8_t val=0;

	val = data<<1;// this is done because the QA output is not used instead output starts from
	//QB and 6 consecutive pins.

	unsigned char i;
	for(i=0;i<8;i++)
	{
		if(val & (1<<7))
		shift1();
		else
		shift0();
		val = val<<1;
		//guardtime(100);
	}


	shift0();	//this trick is required because i by mistake designed
	//the board with 4094 design when i actually had 74hc595. In 4094 if we keep
	//the stb high then data directly passes to the output stage, but in 75hc595
	//this doesn't happen the same way. We need to clock the ST_CP once data has
	//be sent so that data will appear at the output stage. but i didn't have
	//another pin to do that so i combined its ST_CP and SH_CP clocks.
	//well this solves the problem but the last bit is lost because SH_CP
	//is always one clock ahead of ST_CP. As a result the last bit never re
	//reaches the output stage. However the last bit is already present
	//in the shift register so i am just sending another clock actually
	//along with a zero bit so that the last bit is also shown in output
	//stage.

}


void start_timer()
{
	TCCR0B = (1<<CS02) | (1<<CS00);// prescaler set to clk/1024
	TCNT0 = 0;
}

void pOnAnim()
{
	uint8_t k,var=0;
	for(k=0;k<3;k++)
	for(uint8_t i=0;i<3;i++)
	{
		var = (1<<(2-i)) | (1<<(3+i));
		shift1_send(var);
		_delay_ms(200);
	}
	shift1_send(0);
}

void pOffAnim()
{
	uint8_t k,var=0;
	for(k=0;k<3;k++)
	for(uint8_t i=0;i<3;i++)
	{
		var = (1<<i) | (1<<(5-i));
		shift1_send(var);
		_delay_ms(200);
	}
	shift1_send(0);
}


unsigned char batt_level=0; //global battery level store

#define on 	1
#define off 0

unsigned char power_status=on;

#define BOOSTER  0 //PB0 is connected to a MOSFET to turn on DC-DC converter
//and also connected to R1 of voltage divider, so that when device is off power can
//saved that is dissipated by voltage dividing resistors

#define CHARGER  1 //PB2 for tiny13 PB1 for tiny45 is connected to Input charger positive supply
//however to wake the device from sleep mode, charger negative pin is also used.
//charger negative pins is connected to INT0.

//there is a diode between charger negative pin and battery

//charger detect pin will be pulled down either via external or internal pulldown

void power_on()
{

	/* return to active mode instuctions here,
	i.e. turn on all required peripherals */

	PORTB |= (1<<BOOSTER);

	batt_level = batterystatus();//since R1 is connected between BOOSTER pin and ADC2 so only when
								//booster pin is set high and output battery voltage
								//can be read
	pOnAnim();

	power_status = on;

	show_batt_level();



	if(batt_level <= 27)
	{
		 //disable all peripheral and power down instructions
		power_off();
	}



}

void power_off()
{
	//if(power_status) //don't run the power sequence again and again this line is
	//{			//not necessary once power down and sleep instructions are written
		pOffAnim();
		PORTB &= ~(1<<BOOSTER);
		power_status = off;

		/* power down instructions here */
		MCUCR = (1<<SE);
		MCUCR |= (1<<SM1);

//sleepagain:

		sei(); //if the powerdown is called from with in an interrupt and we don't call sei()
		//then the device will never wake up because -> reason explained below.
		//while within the interrupt 'I' bit in SREG is cleared by the cpu until
		//once ISR cycle is complete to prevent deadlocks. so i set the global interrupt again
		//so that once the sleep instruction is executed an INT0 is our only chance to wake the
		//device except external reset and all other resets.

		asm volatile("sleep"::);
		cli();

//		MCUCR =0;
//		goto sleepagain; //we need that on only interrupt routine should be executed
						//so by mistake a small trigger on INT0 pin wakes the device
	//}	//but isn't long enough to register an interrupt then go back to sleep again
}

void show_warning()
{
	shift1_send(0x1);//blink last led
	_delay_ms(200);
	shift1_send(0x0);
	_delay_ms(200);

}

void sleep()
{
	shift1_send(0);//clear display
	/* here rest of the sleep commands and interrupt setups */
}



void chargingAnimation()
{
	uint8_t i,var=0;
	for(i=0;i<6;i++)
	{
		var |= (1<<i);
		shift1_send(var);
		_delay_ms(300);
	}
}



int main()
{

	initshift1();//initialise shift1_system

/* testing shift1_System */ //to be removed in final product
//	shift1_send(0);
//	_delay_ms(1000);
//	shift1_send(0xAA);
//	_delay_ms(1000);
//	shift1_send(0x55);
//	_delay_ms(1000);

/* DC_DC converter Pin setup part */

	DDRB |= (1<<BOOSTER);

/* Charger connection setup */
	DDRB &= ~(1<<CHARGER);
	PORTB &= ~(1<<CHARGER); //however put an external pulldown of 10k on this pin
							//just to be sure
/* ADC setup Part */

	ADCSRA = (1<<ADEN) | (1<<ADPS0) | (1<<ADPS1); //ADC clk prescaler set to clk/64
	ADMUX = (1<<REFS1) | (1<<MUX1);//Reference set to Internal BandGap voltage Vbg of 1.1v
							//channel 2 has been selected.


	DIDR0 |= (1<<ADC2D);//disable Digital input buffer on ADC2 pin so that power
						//could be saved that is wasted due to digital input buffer
						//however once we do this we will no longer be able to read
						//digital pin status use PINx register for this particular pin
						//it will alwasys be 0

/* INT0 setup section */
	DDRB &= ~(1<<PB2);
	PORTB |= (1<<2);//pull up interrupt pin int0 PB1 for tiny13 PB2 for tiny45
	GIMSK |= (1<<INT0) | (1<<PCIE);//enable int0 and PCINT1
	PCMSK = (1<<PCINT1);
	sei();


/* start up sequence */


	power_on();
	sleep();

	while(1)
	{
			sei();//set global interrupt

			batt_level = batterystatus();


			if(batt_level < 27) //i.e battery voltage is less than 2.7v
			{
				cli();
				_delay_ms(1000);//allow the vcc to stabilze, because the booster is in heavy load
					//it will create a power surge and taking an adc reading in such
					//situation will not yeild good results
				if(batt_level < 27)
				power_off();		//before critical batt voltage was 2.9v and low batt was 3.1v
									//however since ADC values are taken from a IOpin not the battery
									//directly i have tried to compensate them by reducing the values
									//here in the program.
			}
			else if(batt_level < 29)//i.e. battery voltage is less than 2.9v
			{
				//	cli();
					show_warning();//blink the last led, low capacity warning
			}
	}

	return 0;
}


ISR(PCINT0_vect)
{
	//charger detect function
	if((PINB & (1<<CHARGER)))
	{
	//	Charging animation here and also
	//	once charger is connected i think we should power_off the device
		if(power_status)
		{
			power_status = off;
			PORTB &= ~(1<<BOOSTER);
			pOffAnim();
		}



		//_delay_ms(100);
		while((PINB & (1<<CHARGER)))
		{
			chargingAnimation();
		}
		sleep();//clear display and sleep
		MCUCR |= (1<<SE);
		MCUCR |= (1<<SM1);
		sei();
		asm volatile("sleep"::);
		cli();

	}

}

ISR(INT0_vect)
{


	unsigned char count=0;

		start_timer();

		while(!(PINB &	(1<<PB2)) && count < 100 ) //break this loop if button is pressed for more than 3 seconds and execute the function
		{										//i.e. take action even if button is pressed and more than 3 seconds passed
			while(!(TIFR & (1<<TOV0)));
			count++;
			TIFR |= (1<<TOV0);	//in tiny45 it is TIFR in tiny13a it is TIFR0
		}

		if(count > 5 && count <= 70) //if count is 30 then 1 second has passed according to clk/1024 prescaler and 8Mhz clk
		{
			//execute this if button has been pressed and released for less than a 1.5

			if(power_status==on)
			{
				show_batt_level();

			}
			else if(power_status==off)
			{
				MCUCR = (1<<SE);
				MCUCR |= (1<<SM1);
				sei();
				asm volatile("sleep"::);
				cli();
				return;



			}
		}

		else if(count >= 95 )//and this if more than 2.5 seconds has passed
		{
			if(power_status)
			{
				power_off();

			}
			else
			{
				power_on();

			}
		}


		sleep();


}


unsigned int adc_read()
{
//for stability and accuracy we take 3 readings and average them

//to decrease memory usage don't take multiple reading just take 1
//But since i switched from tiny13 to tiny45 i don't need to worry for memory
//saving

	unsigned int value=0;

	for(uint8_t i=0;i<3;i++)
	{
		_delay_ms(5);//for stablility between consecutive readings
		ADCSRA |= (1<<ADSC);
		while((ADCSRA & (1<<ADIF)) == 0);
		ADCSRA |= (1<<ADIF);
		value = value +  ADC;
	}
//	return ADC;
	return value/3;
}

unsigned char batterystatus()
{

//divider = 12.2/2.2; voltage divier (R1+R2)/R2
//so divider = 5.54 , but taking float values in here
//takes a lot of memory and overflows it
//so let us multiply this value by 100 and divide it later
	#define divider 554UL

	unsigned int adcvalue;
	unsigned long int orgvoltage;
	unsigned char voltage;


		adcvalue = adc_read();

		orgvoltage = ((adcvalue * 1100UL) * divider)/1024UL;
		//the UL's are important as without them the AVR GCC compiler
		//doesn't honour an unsigned long and acts very wierdly, it took
		//me hours before i recognised this problem

		orgvoltage /= 10000;//adjust this value to increase accuracy level
		//suppose i need 2 digit accuracy after point then divide by 1000
		//for 1 digit accuracy divide by 10000
		//more this accuracy cannot be achieved

		voltage = orgvoltage;//if the variable voltage in here is uchar
		//then maximum it can display 255 but our output can go from
		//0 to 650 so we should divide orgvoltage by 10000 to get output
		//between 0 and 65. If our variable voltage is Integer type then
		//we can go for 2 digit accuracy and divide orgvoltage by 1000 instead of 10000.

		return voltage; //so here if voltage value is 34 then it is actually 3.4
		//if it is 45 then actual voltage is 4.5 and so on.

}



void show_batt_level()
{
	//here we have to write algorithm for battery percentage indication using leds

	uint8_t display_level,data=0;

	if(batt_level > 41)
		batt_level = 41;

	display_level = (41 - batt_level)/2;

	if(display_level>6)
		display_level = 6;

	display_level = 6 - display_level;

	for(uint8_t i=0;i<display_level;i++)
		data |= (1<<i);

	shift1_send(data);

	_delay_ms(2000);
}
