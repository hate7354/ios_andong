#pragma once

#include <cmath>
#include <iostream>
#include <string>



class RoiPointDataSet
{
public:
	RoiPointDataSet() {};
	~RoiPointDataSet() {};

private:
	int roi_seq;
	int pt_seq;
	int pt_x;
	int pt_y;

public:
	void setRoi_seq(int var) { roi_seq = var; }
	void setPt_seq(int var) { pt_seq = var; }
	void setPt_x(int var) { pt_x = (var < 0) ? 0 : ((var > 639) ? 639 : var); }
	void setPt_y(int var) { pt_y = (var < 0) ? 0 : ((var > 479) ? 479 : var); }


public:
	int getRoi_seq() { return roi_seq; }
	int getPt_seq() { return pt_seq; }
	int getPt_x() { return pt_x; }
	int getPt_y() { return pt_y; }

};
