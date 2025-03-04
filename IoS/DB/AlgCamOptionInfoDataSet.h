#pragma once

#include <cmath>
#include <iostream>
#include <string>

class AlgCamOptionInfoDataSet
{
public:
	AlgCamOptionInfoDataSet() {};
	~AlgCamOptionInfoDataSet() {};

	//variable
private:
	int ch_seq;
	int daynight;
	int object_min;
	int object_max;
	int threshold_min;
	int threshold_max;
	int chg_rate_max;

	//setter
public:
	void setCamera_seq(int var) { ch_seq = var; }
	void setDaynight(int var) { daynight = var; }
	void setObject_min(int var) { object_min = var; }
	void setObject_max(int var) { object_max = var; }
	void setThreshold_min(int var) { threshold_min = var; }
	void setThreshold_max(int var) { threshold_max = var; }
	void setChg_Rate_max(int var) { chg_rate_max = var; }

	//getter
public:
	int getCamera_seq() { return ch_seq; }
	int getDaynight() { return daynight; }
	int getObject_min() { return object_min; }
	int getObject_max() { return object_max; }
	int getThreshold_min() { return threshold_min; }
	int getThreshold_max() { return threshold_max; }
	int getChg_Rate_max() { return chg_rate_max; }
};

