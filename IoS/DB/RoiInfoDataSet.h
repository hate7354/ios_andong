#pragma once

#include <cmath>
#include <iostream>
#include <string>

class RoiInfoDataSet
{
public:
	RoiInfoDataSet() {};
	~RoiInfoDataSet() {};

//variable
private:
	int roi_seq;
	int ch_seq;
	uint32_t alg_mask;
	int canvas_w;
	int canvas_h;
	int roi_idx;
	int angle;

//setter
public:
	void setRoi_seq(int var) { roi_seq = var; }
	void setCamera_seq(int var) { ch_seq = var; }
	void setAlg_mask(uint32_t var) { alg_mask = var; }
	void setCanvas_w(int var) { canvas_w = var; }
	void setCanvas_h(int var) { canvas_h = var; }
	void setRoi_idx(int var) { roi_idx = var; }
	void setAngle(int var) { angle = var; }

//getter
public:
	int getRoi_seq() { return roi_seq; }
	int getCamera_seq() { return ch_seq; }
	uint32_t getAlg_mask() { return alg_mask; }
	int getCanvas_w() { return canvas_w; }
	int getCanvas_h() { return canvas_h; }
	int getRoi_idx() { return roi_idx; }
	int getAngle() { return angle; }
};