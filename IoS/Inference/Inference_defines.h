#pragma once

#include "yolo_v2_class.hpp"
#pragma comment( lib, "yolo_cpp_dll.lib" )

#include <opencv2/core.hpp>
#include "opencv2/imgproc.hpp"
#include <opencv2/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/core/cuda_types.hpp>
#include <opencv2/core/cuda.inl.hpp>
#include <opencv2/dnn.hpp>

using namespace std;
using namespace cv;

class StreamData
{
public:
	StreamData(int ch_seq, Mat& Frame)
	{
		_ch_seq = ch_seq;
		_Frame = Frame.clone();
	}
public:
	int _ch_seq;
	Mat _Frame;
};
