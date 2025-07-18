/*
 Battery.cpp - Battery library
 Copyright (c) 2014 Roberto Lo Giacco.

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as 
 published by the Free Software Foundation, either version 3 of the 
 License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Battery.h"
#include <Arduino.h>

Battery::Battery(uint16_t minVoltage, uint16_t maxVoltage, uint8_t sensePin, uint8_t adcBits) : adc(0x01 << adcBits) {
	this->sensePin = sensePin;
	this->activationPin = 0xFF;
	this->minVoltage = minVoltage;
	this->maxVoltage = maxVoltage;
	this->ischarge = 0;
	analogReadResolution(adcBits);
}

void Battery::begin(uint16_t refVoltage, float dividerRatio, mapFn_t mapFunction) {
	this->refVoltage = refVoltage;
	this->dividerRatio = dividerRatio;
	pinMode(this->sensePin, INPUT);
	analogSetPinAttenuation(this->sensePin, ADC_2_5db);	// set attenuation to 2.5db
	this->mapFunction = mapFunction ? mapFunction : &linear;
}

void Battery::onDemand(uint8_t activationPin, uint8_t activationMode) {
	this->activationPin = activationPin;
	if (activationPin < 0xFF) {	
		this->activationMode = activationMode;
		pinMode(this->activationPin, OUTPUT);
		digitalWrite(activationPin, !activationMode);
	}
}

uint8_t Battery::level() {
	return this->level(this->voltage());
}

uint8_t Battery::level(uint16_t voltage) {
	if (voltage <= minVoltage) {
		return 0;
	} else if (voltage >= maxVoltage) {
		if (4300 < voltage) {
			this->ischarge = true;
		}
		else {
			this->ischarge = false;
		}
		
		return 100;
	} else {
		return (*mapFunction)(voltage, minVoltage, maxVoltage);
	}
}

uint8_t Battery::getischarge(void) {
	return this->ischarge;
}

uint16_t Battery::voltage(uint8_t msDelay) {
	if (activationPin != 0xFF) {
		digitalWrite(activationPin, activationMode);
		delayMicroseconds(10); // copes with slow switching activation circuits
	}
	analogReadMilliVolts(sensePin);
	delay(msDelay); // allow the ADC to stabilize
	uint16_t reading = analogReadMilliVolts(sensePin) * dividerRatio;
	if (activationPin != 0xFF) {
		digitalWrite(activationPin, !activationMode);
	}
	return reading;
}