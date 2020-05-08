// air quality info => https://iotfactory.eu/the-importance-of-indoor-air-quality-iaq-for-business-performance-and-wellbeing/
// https://www.elecrow.com/wiki/images/3/3a/DSM501.pdf
// https://diymcblog.blogspot.com/2016/08/dsm501-1.html
// https://www.elecrow.com/wiki/index.php?title=Dust_Sensor-_DSM501A
// https://spacemath.gsfc.nasa.gov/earth/10Page105.pdf

// esp8266 to Wifi
// https://diyprojects.io/calculate-air-quality-index-iaq-iqa-dsm501-arduino-esp8266/#.XqHo45MzbhN
// https://github.com/opendata-stuttgart/sensors-software/blob/master/esp8266-arduino/archive/ppd42ns-wifi-dht-mqtt/ppd42ns-wifi-dht-mqtt.ino
// https://www.shadowandy.net/2015/06/arduino-dust-sensor-with-esp8266.htm

// arduino libs
#include <Wire.h>
#include <SPI.h>
// default esp8266 libs
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
// libs
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <SSD1306Wire.h>

//#include <Adafruit_SSD1306.h>
#include <Adafruit_CCS811.h>
#include <NTPClient.h>
#include <RTClib.h> // just for DateTime type
#include <GyverButton.h>

#define DSM501_PM10 12 //d6 red/blue Vout2 1.0
#define DSM501_PM25 14 //d5 yellow/green Vout1 2.5
#define PM10        0 // array indexes
#define PM25        1 // array indexes
#define PM10to25    2 // array indexes



// for 128x64 displays:
SSD1306Wire display(0x3c, SDA, SCL);

// bme280 sensor
#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME280 bme280; // I2C
// ccs811 sensor
//#define CCS811_RESET 16
Adafruit_CCS811 ccs811;
int8_t CCS811resetCount = 0;
int8_t CCS811differentDataCount = 0;
#define CCS811resetSteps 20
float ccs811_eco2_array[CCS811resetSteps];

#define GAS_PIN_A0 A0

// buttons
#define MODE_PIN     0
#define SET_UP_PIN   13
#define SET_DOWN_PIN 2
#define SAVE_PIN     15
GButton mode_button(MODE_PIN);
GButton set_up_button(SET_UP_PIN);
GButton set_down_button(SET_DOWN_PIN);
GButton save_button(SAVE_PIN);

int8_t mode = 0;
int8_t subMode = 0;
byte timeMode = 0; // 0-day, 1-hour

// WiFi
const char *ssid     = "MiHome";
const char *password = "mrVendy2019";

const long utcOffsetInSeconds = (3*3600); // +3hours Ukraine offset to UTC
// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", utcOffsetInSeconds);

#define LEAP_YEAR(Y)     ( (Y>0) && !(Y%4) && ( (Y%100) || !(Y%400) ) )
unsigned long epochTime = 0;

String AQIIndexes[] = {
	"0-50", "51-100", "101-150", "151-200", "201-300"
};

String AQIStatuses[] = {
	"Excellent", "Good", "Moderate", "Unhealthy", "Dangerous"
};

unsigned long startTime, displayStartTime, minuteStartTime;
unsigned long nowMicroSeconds;
unsigned int noiseOccupancyMicroSeconds = (10 * 1000); // valid data is from 10 to 90 miliSeconds if less its a noise
unsigned long sampleTimeMicroSeconds = (30 * 1000 * 1000); // one minute gathering
unsigned long trigger[] = {0, 0};
boolean value[] = {HIGH, HIGH};

unsigned long lowPulseOccupancyFiltered[] = {0, 0, 0}; // 10, 25, between 10 and 25
float ratio[] = {0, 0, 0};
float concentration[] = {0, 0, 0};

#define halfHourConcentrationsSteps 60 // 30 minutes
#define dayCalculationSteps 48 // 24h / 30minutes = 48
// indexes
uint16_t halfHourConcentrationsIndex = 0;
uint16_t minuteStep = 0;
uint16_t dayIndex = 0;
int8_t AQIIndex = 0;

// data
float halfHourConcentrations[halfHourConcentrationsSteps];
int chartData[dayCalculationSteps];

// day info
struct {
	float day25Concentration[dayCalculationSteps];
	String dayAQI25Index[dayCalculationSteps];
	String dayAQI25Status[dayCalculationSteps];
	float bme280_temp[dayCalculationSteps];
	float bmp280_press[dayCalculationSteps];
	float bme280_hum[dayCalculationSteps];
	float ccs811_eco2[dayCalculationSteps];
	float ccs811_tvoc[dayCalculationSteps];
	float gas_concentration[dayCalculationSteps];
	DateTime time[dayCalculationSteps];
} DayInfo;

struct {
	float day25Concentration[60];
	String dayAQI25Index[60];
	float bme280_temp[60];
	float bmp280_press[60];
	float bme280_hum[60];
	float ccs811_eco2[60];
	float ccs811_tvoc[60];
	float gas_concentration[60];
	int16_t minutes[60];
} HourInfo;


struct {
	DateTime time;
    String timeString;

    float bme280_temp = 0;
    float bmp280_press = 0;
    float bme280_hum = 0;

    float ccs811_eco2 = 0;
    float ccs811_prev_eco2 = 0;
    float ccs811_tvoc = 0;

    float gas_concentration = 0;

	float current25concentration = 0;
	String currentAQI25 = AQIIndexes[0];
	String currentAQI25Status = AQIStatuses[0] ;
} CurrentData;

void setup()
{
	Serial.begin(9600);
    Serial.println(" ");
	// start wifi
	WiFi.begin(ssid, password);
	while ( WiFi.status() != WL_CONNECTED ) {
		delay ( 500 );
		Serial.print ( "." );
	}
	timeClient.begin();
	delay ( 100 );
	timeClient.update();
	delay ( 100 );
	timeClient.update(); // 2 updates, sometimes first sync unsuccessful
	epochTime = timeClient.getEpochTime();
	//populateDateArray(epochTime);
	CurrentData.time = DateTime(timeClient.getEpochTime());
	// get time
	Serial.println(" ");
    Serial.println("time:" + timeClient.getFormattedTime());
    //Serial.println(timeClient.);

	// DSM501 sensor
	pinMode(DSM501_PM10, INPUT);
	pinMode(DSM501_PM25, INPUT);

	// buttons
	mode_button.setDebounce(50);
	mode_button.setTimeout(2000);
	mode_button.setType(HIGH_PULL);
    mode_button.setDirection(NORM_OPEN);

	save_button.setDebounce(50);
	save_button.setTimeout(2000);
	save_button.setType(LOW_PULL);
    save_button.setDirection(NORM_OPEN);

	set_up_button.setDebounce(50);
	set_up_button.setTimeout(2000);
	set_up_button.setType(HIGH_PULL);
    set_up_button.setDirection(NORM_OPEN);

	save_button.setDebounce(50);
	save_button.setTimeout(2000);
	save_button.setType(HIGH_PULL);
    save_button.setDirection(NORM_OPEN);

	// sensors
	bme280.begin();

	// Wait for the sensor ccs811 to be ready
	ccs811.begin();
    while(!ccs811.available());

	// screen
	display.init();
	display.setContrast(255);

	display.clear();

//	for (int i = 1; i <= 60; i++){
//        delay(1000);
//        Serial.println(String(i) + " s (wait 60 for DSM501 to warm up)");
//    }
	startTime = micros();
	displayStartTime = startTime;
	minuteStartTime = startTime;
}

void displayMainScreen() {
    display.clear();
	delay(5);
	//display.setFont(10);

	display.setFont(ArialMT_Plain_10);

    display.drawString(0, 0, CurrentData.timeString);

    display.drawString(0, 10, ("T:" + String(CurrentData.bme280_temp) + "*C" + " P:" + String(CurrentData.bmp280_press) + "hPa"));

    display.drawString(0, 20, ("Humidity:  " + String(CurrentData.bme280_hum) + " %"));

    display.drawString(0, 30, ("co2/tvoc:" + String((int)CurrentData.ccs811_eco2) + "/" + String((int)CurrentData.ccs811_tvoc))); // eCO2 ... ppm

	display.drawString(0, 40, "AQI:" + String(CurrentData.currentAQI25) + " " + String(CurrentData.currentAQI25Status));

	display.drawString(0, 50, ("2.5 concentr.:" + String(((int)halfHourConcentrations[(halfHourConcentrationsIndex > 0 ? (halfHourConcentrationsIndex-1) : 0 )])) + "ppm"));
	display.flipScreenVertically();
	display.display();
}

void buildChart(float * dataArray, int16_t collectedSteps, float currentParam, String legendText, String legendIndicator, int startHours, int startMinutes) {
	// map hour array to screen displaying 48 items, for first 24 minutes collect avery second item
	int16_t iterator = collectedSteps;
	if (startMinutes != -1) {
		int16_t j = 0;
		for(int16_t i = 0; i < iterator; j++) {
    	    dataArray[j] = dataArray[i];
    	    i = (i < 24) ? (i + 2) : (i + 1);
    	}
    	iterator = (j > 0) ? (j - 1) : 0;
	}

	long maxValue = dataArray[0];
	long minValue = iterator > 1 ? dataArray[0] : 0;
	for(int16_t i = 0; i < iterator; i++) {
		maxValue = (long)dataArray[i] > maxValue ? (long)dataArray[i] : maxValue;
		minValue = (long)dataArray[i] < minValue ? (long)dataArray[i] : minValue;
	}
	if (maxValue == minValue) {
		minValue = 0;
	}

	iterator = constrain(iterator, 0, 48);
	for(int16_t i = 0; i < iterator; i++) {
		chartData[i] = ((map((long)dataArray[i], (long)minValue, (long)maxValue, 0, 40)) * (-1)) + 52;
	}

	display.clear();
	delay(5);
	display.setFont(ArialMT_Plain_10);

    display.drawString(0, 0, String(legendText + ": " + String(currentParam) + " " + legendIndicator));
    display.drawString(0, 12, String(maxValue));
    display.drawString(0, 27, String(legendIndicator));
    display.drawString(0, 42, String(minValue));

    display.drawRect(32, 12, 98, 42);


	for(int8_t i = 0; i < iterator; i++) {
    	chartData[i];
    	display.drawRect((32 + (i * 2)), chartData[i], 2, (53-chartData[i]));
    }
    display.drawString(0, 52, String(startHours != -1 ? "hours:" : "mins.:"));
	int8_t time = startHours != -1 ? startHours : startMinutes;
	int8_t startPosX = 32;
	if (startHours != -1) {
		for(int8_t i = 0; i <= 6; i++){
        	display.drawString(startPosX, 52,  String(time < 24 ? time : time - 24));
        	startPosX += 16;
        	time += 4;
    	}
	}

	if (startMinutes != -1) {
		for(int8_t i = 0; i <= 6; i++){
        	display.drawString(startPosX, 52, String(time < 60 ? time : time - 60));
        	startPosX += 16;
        	time += 10;
    	}
	}
	display.flipScreenVertically();
	display.display();
}


void drawChart() {
	// day info
	if (timeMode == 0) {
		switch (subMode) {
            case 0:
                buildChart(DayInfo.day25Concentration, dayIndex, CurrentData.current25concentration, "PM2.5", "ppm", (int) (DayInfo.time[0].hour()), -1);
        	break;
            case 1:
                buildChart(DayInfo.bme280_temp, dayIndex, CurrentData.bme280_temp, "Temperature", "*C", (int) (DayInfo.time[0].hour()), -1);
        	break;
            case 2:
                buildChart(DayInfo.bmp280_press, dayIndex, CurrentData.bmp280_press, "Pressure", "hPa", (int) (DayInfo.time[0].hour()), -1);
        	break;
            case 3:
                buildChart(DayInfo.bme280_hum, dayIndex, CurrentData.bme280_hum, "Humidity", "%", (int) (DayInfo.time[0].hour()), -1);
        	break;
    		case 4:
                buildChart(DayInfo.ccs811_eco2, dayIndex, CurrentData.ccs811_eco2, "CO2", "ppm", (int) (DayInfo.time[0].hour()), -1);
        	break;
    		case 5:
                buildChart(DayInfo.ccs811_tvoc, dayIndex, CurrentData.ccs811_tvoc, "TVOC", "ppb", (int) (DayInfo.time[0].hour()), -1);
        	break;
    		case 6:
                buildChart(DayInfo.gas_concentration, dayIndex, CurrentData.gas_concentration, "gas", "ppm", (int) (DayInfo.time[0].hour()), -1);
        	break;
        }
	}
	// hour info
	if (timeMode == 1) {
		switch (subMode) {
            case 0:
                buildChart(HourInfo.day25Concentration, minuteStep, CurrentData.current25concentration, "PM2.5", "ppm", -1, (int) (HourInfo.minutes[0]));
        	break;
            case 1:
                buildChart(HourInfo.bme280_temp, minuteStep, CurrentData.bme280_temp, "Temperature", "*C", -1, (int) (HourInfo.minutes[0]));
        	break;
            case 2:
                buildChart(HourInfo.bmp280_press, minuteStep, CurrentData.bmp280_press, "Pressure", "hPa", -1, (int) (HourInfo.minutes[0]));
        	break;
            case 3:
                buildChart(HourInfo.bme280_hum, minuteStep, CurrentData.bme280_hum, "Humidity", "%", -1, (int) (HourInfo.minutes[0]));
        	break;
    		case 4:
                buildChart(HourInfo.ccs811_eco2, minuteStep, CurrentData.ccs811_eco2, "CO2", "ppm", -1, (int) (HourInfo.minutes[0]));
        	break;
    		case 5:
                buildChart(HourInfo.ccs811_tvoc, minuteStep, CurrentData.ccs811_tvoc, "TVOC", "ppb", -1, (int) (HourInfo.minutes[0]));
        	break;
    		case 6:
                buildChart(HourInfo.gas_concentration, minuteStep, CurrentData.gas_concentration, "gas", "ppm", -1, (int) (HourInfo.minutes[0]));
        	break;
        }
	}
}

void displayData() {
	switch (mode) {
        case 0:
			displayMainScreen();
		break;
		case 1:
			drawChart();
		break;
	}
}

void scanButtons() {
    mode_button.tick();
    set_up_button.tick();
    set_down_button.tick();
    save_button.tick();

//    if (mode_button.isClick()) Serial.println("Click");
//    if (mode_button.isSingle()) Serial.println("Single");
//    if (mode_button.isDouble()) Serial.println("Double");
//    if (mode_button.isTriple()) Serial.println("Triple");
//
//    if (mode_button.hasClicks())
//        Serial.println(mode_button.getClicks());
//    if (mode_button.isRelease()) {
//        Serial.println("Release");
//    }
//    if (mode_button.isHolded()) {
//        Serial.println("Holded");
//
//    }
//    if (mode_button.isHold()) Serial.println("Holding");

    if (mode_button.isPress()) {
        Serial.println("mode_button");
        mode++;
        if (mode > 1) {
            mode = 0;
        }
        Serial.println(mode);
    };         // нажатие на кнопку (+ дебаунс)
	if (set_up_button.isPress()) {
		switch (mode) {
	        case 1:
	            Serial.println("sub_mode_button");
	            subMode++;
	            if (subMode > 6) {
                    subMode = 0;
                }
                Serial.println(subMode);
	        break;
	    }
    };
	if (set_down_button.isPress()) {
		switch (mode) {
	        case 1:
	            timeMode = !timeMode;
	        break;
	    }
    };
	if (save_button.isPress()) {
        Serial.println("save_button");
    };
}

void scanSaveSensorsData (int8_t iterator) {
	CurrentData.bme280_temp = bme280.readTemperature();
	DayInfo.bme280_temp[iterator] = CurrentData.bme280_temp;
	HourInfo.bme280_temp[minuteStep] = CurrentData.bme280_temp;

	CurrentData.bmp280_press = bme280.readPressure() / 100.0F;
    DayInfo.bmp280_press[iterator] = CurrentData.bmp280_press;
    HourInfo.bmp280_press[minuteStep] = CurrentData.bmp280_press;

    CurrentData.bme280_hum = bme280.readHumidity();
    DayInfo.bme280_hum[iterator] = CurrentData.bme280_hum;
    HourInfo.bme280_hum[minuteStep] = CurrentData.bme280_hum;

	if(!ccs811.readData()){
		CurrentData.ccs811_tvoc = ccs811.getTVOC();
        DayInfo.ccs811_tvoc[iterator] = CurrentData.ccs811_tvoc;
        HourInfo.ccs811_tvoc[minuteStep] = CurrentData.ccs811_tvoc;

		CurrentData.ccs811_eco2 = ccs811.geteCO2();
		DayInfo.ccs811_eco2[iterator] = CurrentData.ccs811_eco2;
		HourInfo.ccs811_eco2[minuteStep] = CurrentData.ccs811_eco2;

		// collect 10 sec data
		if (CCS811resetCount < CCS811resetSteps)  {
			ccs811_eco2_array[CCS811resetCount] = CurrentData.ccs811_eco2;
			CCS811resetCount++;
		}
		// // check freezing if more than 10 sec same data -> reset sensor
		if (CCS811resetCount == CCS811resetSteps) {
			CCS811differentDataCount = 0;
			for (int8_t i = 0; i < CCS811resetSteps; i++) {
				if (ccs811_eco2_array[i] != CurrentData.ccs811_eco2) {
					CCS811differentDataCount++;
				}
			}
			// at least one different value -> sensor works
			if (CCS811differentDataCount == 0) {
				Serial.println("reset CCS811");
            	ccs811.begin();
                delay(100);
            	Serial.println("reset done CCS811");
			}
			Serial.println("CCS811 works fine");
			CCS811resetCount = 0;
		}
	}

	CurrentData.gas_concentration =  map(analogRead(GAS_PIN_A0), 0, 1023, 300, 10000);
	DayInfo.gas_concentration[iterator] = (float)CurrentData.gas_concentration;
	HourInfo.gas_concentration[minuteStep] = (float)CurrentData.gas_concentration;
}

String getTimeString (DateTime time) {
	return String(time.year()) + "/" +
           String(time.month()) + "/" +
           String(time.day()) + " " +
           String(time.hour()) + ":" +
           String(time.minute()) + ":" +
           String(time.second());
}

void calculateAQI() {
	value[PM10] = digitalRead(DSM501_PM10);
    value[PM25] = digitalRead(DSM501_PM25);
	// check start of low impulse
	if (value[PM25] == LOW && trigger[PM25] == 0) {
		trigger[PM25] = nowMicroSeconds;
	}

	// check finish of low impulse
	if (value[PM25] == HIGH && trigger[PM25] != 0) {
		lowPulseOccupancyFiltered[PM25] += (nowMicroSeconds - trigger[PM25]) >= noiseOccupancyMicroSeconds ? (nowMicroSeconds - trigger[PM25]) : 0;
		trigger[PM25] = 0;
	}
	// check start of low impulse
	if (value[PM10] == LOW && trigger[PM10] == 0) {
		trigger[PM10] = nowMicroSeconds;
	}
	// check finish of low impulse
	if (value[PM10] == HIGH && trigger[PM10] != 0) {
		lowPulseOccupancyFiltered[PM10] += (nowMicroSeconds - trigger[PM10]) >= noiseOccupancyMicroSeconds ? (nowMicroSeconds - trigger[PM10]) : 0;
		trigger[PM10] = 0;
	}

	if ((nowMicroSeconds - startTime) >= sampleTimeMicroSeconds) {
		lowPulseOccupancyFiltered[PM10to25] = (lowPulseOccupancyFiltered[PM10] - lowPulseOccupancyFiltered[PM25]) > 0
			? (lowPulseOccupancyFiltered[PM10] - lowPulseOccupancyFiltered[PM25]) : 0;

		// ratio 0-100%
		ratio[PM10to25] = 100 * (float) lowPulseOccupancyFiltered[PM10to25]/ (float) sampleTimeMicroSeconds;
		//concentration for PPD42NS sensor and not sure that it works correctly with DSN501 but any alternative solutions :(
		concentration[PM10to25] = (1.1*pow(ratio[PM10to25],3)-3.8*pow(ratio[PM10to25],2)+520*ratio[PM10to25]+0.62);
		// prevent wrong calculation
		concentration[PM10to25] = constrain(concentration[PM10to25], 0, 8000);
        //  lowPulseOccupancyFiltered -> Particle graph based on pcs/283ml DMS501; 1m^3 = (3533.5689 * 283ml) also 1ft^3 = 28316.85ml = 0.02831685m^3
        // AQI based on 2.5(1.0) mg/m^3
        // increase value from 283 ml to 1m^3
        concentration[PM10to25] = concentration[PM10to25] * 3534;
        Serial.println(concentration[PM10to25]);
        //  P2.5 weight = 1.8x10^-14kg = 1.8x10^-5 microGram = 0.000018 micro gram //https://spacemath.gsfc.nasa.gov/earth/10Page105.pdf page2
		concentration[PM10to25] = concentration[PM10to25] * 0.000018;
		Serial.println(concentration[PM10to25]);

		if (halfHourConcentrationsIndex < halfHourConcentrationsSteps) {

			Serial.println(" ");

			halfHourConcentrations[halfHourConcentrationsIndex] = concentration[PM10to25] > 0 ? concentration[PM10to25] : 0;
			HourInfo.day25Concentration[minuteStep] = halfHourConcentrations[halfHourConcentrationsIndex];
			CurrentData.current25concentration = halfHourConcentrations[halfHourConcentrationsIndex];

			halfHourConcentrationsIndex++;
		}
		if (halfHourConcentrationsIndex == halfHourConcentrationsSteps) {
			DayInfo.day25Concentration[dayIndex] = average(halfHourConcentrations, halfHourConcentrationsIndex);
			Serial.println("concentration ");
			Serial.println(DayInfo.day25Concentration[dayIndex]);
			AQIIndex = getAQI25Index(DayInfo.day25Concentration[dayIndex]);

			DayInfo.dayAQI25Index[dayIndex] = AQIIndexes[AQIIndex];
			DayInfo.dayAQI25Status[dayIndex] = AQIStatuses[AQIIndex];
			DayInfo.time[dayIndex] = CurrentData.time;

			CurrentData.currentAQI25 = AQIIndexes[AQIIndex];
            CurrentData.currentAQI25Status = AQIStatuses[AQIIndex];

			dayIndex++;

			Serial.println(" Day Array concentration ");
			for (int8_t i = 0 ; i < dayIndex ; i++) {
				Serial.print(DayInfo.day25Concentration[i]);
				Serial.print(" | ");
				Serial.print(DayInfo.dayAQI25Index[i]);
                Serial.print(" | ");
				Serial.print(DayInfo.dayAQI25Status[i]);
                Serial.print(" ; ");
			}
			Serial.println(" ");
		}

		halfHourConcentrationsIndex = halfHourConcentrationsIndex == halfHourConcentrationsSteps ? 0 : halfHourConcentrationsIndex;
		dayIndex = dayIndex == dayCalculationSteps ? 0 : dayIndex;

		lowPulseOccupancyFiltered[PM10] = 0;
        lowPulseOccupancyFiltered[PM25] = 0;
        lowPulseOccupancyFiltered[PM10to25] = 0;

        startTime = micros();
	}
}

float average (float * array, int8_t len)
{
	float sum = 0;
	for (int8_t i = 0 ; i < len ; i++) {
		sum += (int) array [i] > 0 ? array [i] : 0;
	}
	return  (sum / len) ;
}

int8_t getAQI25Index(float concentration) {
	int8_t AQI25Index = 0;
	if (concentration>= 0 && concentration <= 15.4) {
		AQI25Index = 0;
	} else if (concentration > 15.4 && concentration <= 40.4) {
		AQI25Index = 1;
	} else if (concentration > 40.4 && concentration <= 65.4) {
		AQI25Index = 2;
	} else if (concentration > 65.4 && concentration <= 150.4) {
		AQI25Index = 3;
	} else if (concentration > 150.4 && concentration <= 250.4) {
		AQI25Index = 4;
	} else {
		// calculation error return 0;
		AQI25Index = 0;
	}
	return AQI25Index;
}

void loop() {
	nowMicroSeconds = micros();
	calculateAQI();

	// scan data every second
	if ((nowMicroSeconds - displayStartTime) > 1000000) {
		CurrentData.time = DateTime((epochTime + (nowMicroSeconds/1000000)));
		CurrentData.timeString = getTimeString(CurrentData.time);

		scanSaveSensorsData(dayIndex);
		displayData();

		displayStartTime = micros();
	}
	if ((nowMicroSeconds - minuteStartTime) > (60* 1000000)) {
		HourInfo.minutes[minuteStep] = (int) (CurrentData.time.minute());
		minuteStep = (minuteStep < 59) ? (++minuteStep) : 0 ;
		minuteStartTime = micros();
	}

	//update screen every 100ms to reduce freezing
	if ((nowMicroSeconds - displayStartTime) > 100000) {
		displayData();
	}
	// scan buttons state
	scanButtons();
}
