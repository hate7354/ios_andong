#pragma once

#include "pch.h"
#include <thread>
#include <mutex>
#include <shared_mutex>

#include "InferenceBase.h"


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class InferenceStream
{
public:
	InferenceStream(int index, int model);
	~InferenceStream();

private:
	void RunInfThread();

public:
	void pushFrame(int ch_seq, Mat& frame);
	void addCameraSeq(int ch_seq);
	void removeCameraSeq(int ch_seq);
	void addEventCamera(int ch_seq);
	void removeEventCamera(int ch_seq);
	bool checkCameraStream(int ch_seq);
	bool checkEventCamera(int ch_seq);
	bool checkEventCamera(int ch_seq, bool needLock);

private:
	bool			_bLoop;
	std::thread		_th;
	std::list<StreamData> _streamLists;

	int _index;
	int _model;
	InferenceBase* _pInfBase;
	std::mutex _mtx;
	std::condition_variable _cond;

	std::mutex _mtxDnn;
	std::condition_variable _condDnn;

	std::shared_mutex _rwMtxInf;
	std::vector<int> _cameraLists;
	std::map<int, bool> _bFirsts;
	std::map<int, std::chrono::time_point<std::chrono::steady_clock>> _tps;
	int _cameraCount = 0;

	std::shared_mutex _rwMtx_evt;
	std::vector<int> _evtLists;
};
