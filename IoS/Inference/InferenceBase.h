#pragma once

#include <iostream>
#include <iomanip> 
#include <string>
#include <vector>
#include <queue>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <set>

#include "Inference_defines.h"

using namespace cv;
using namespace std;


class InferenceBase
{
public:
	InferenceBase(int nGPU, std::string cfg, std::string weights, int model);
	~InferenceBase();

public:
	bool runInference(int ch_seq, Mat& frame);


private:
	Detector* pDetector = NULL;
	int _model = 0;
	std::string  _cfg_file;
	std::string  _weights_file;
	int _gpuIdx = 0;
};

