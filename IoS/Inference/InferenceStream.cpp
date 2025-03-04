#include "pch.h"

#include "define.h"
#include "InferenceStream.h"

/////////////////////////////////////////////////////////////////////////////
// DNN FILE
/////////////////////////////////////////////////////////////////////////////

#define DNN_OBJECT_CONFIG	"obj.cfg"
#define DNN_OBJECT_WEIGHT	"obj.weights"

#define DNN_FS_CONFIG		"fs.cfg"
#define DNN_FS_WEIGHT		"fs.weights"

InferenceStream::InferenceStream(int index, int model)
	: _index(index)
	, _model(model)
{
	int offset = 1;
#if (MAX_INF_GPU_COUNT == 1)
	offset = 0;
#endif

	try {
		if (model == static_cast<int>(InferenceType::INF_OBJ)) {
			CString sCfg(DNN_OBJECT_CONFIG);
			CString sWeight(DNN_OBJECT_WEIGHT);

			if (GetFileAttributes(sCfg) != INVALID_FILE_ATTRIBUTES && GetFileAttributes(sWeight) != INVALID_FILE_ATTRIBUTES) {
				_pInfBase = new InferenceBase(index % MAX_INF_GPU_COUNT + offset, std::string(CT2CA(sCfg)), std::string(CT2CA(sWeight)), model);
				//_pInfBase = new InferenceBase(1, std::string(CT2CA(sCfg)), std::string(CT2CA(sWeight)), model);
			}
		}
		else if (model == static_cast<int>(InferenceType::INF_FS)) {
			CString sCfg(DNN_FS_CONFIG);
			CString sWeight(DNN_FS_WEIGHT);

			if (GetFileAttributes(sCfg) != INVALID_FILE_ATTRIBUTES && GetFileAttributes(sWeight) != INVALID_FILE_ATTRIBUTES) {
				_pInfBase = new InferenceBase(index % MAX_INF_GPU_COUNT + offset, std::string(CT2CA(sCfg)), std::string(CT2CA(sWeight)), model);
				//_pInfBase = new InferenceBase(1, std::string(CT2CA(sCfg)), std::string(CT2CA(sWeight)), model);
			}
		}
	}
	catch (std::exception& e) {
		std::cerr << "exception: " << e.what() << "\n";
	}
	catch (...) {
		std::cerr << "unknown exception \n";
	}

	_bLoop = true;
	_th = std::thread{ &InferenceStream::RunInfThread, this };
}

InferenceStream::~InferenceStream()
{
	_bLoop = false;
	_cond.notify_one();
	_condDnn.notify_one();
	_th.join();

	_streamLists.clear();

	if (_pInfBase) {
		delete _pInfBase;
		_pInfBase = nullptr;
	}
}

void InferenceStream::pushFrame(int ch_seq, Mat& frame)
{
	if (_pInfBase) {
		bool bNotify = false;
		std::unique_lock<std::mutex> lock(_mtx);
		if (_streamLists.empty())
			bNotify = true;

		_streamLists.emplace_back(StreamData(ch_seq, frame));

		if (bNotify)
			_cond.notify_one();
	}
}

void InferenceStream::RunInfThread()
{
	while (_bLoop) {
		std::unique_lock<std::mutex> lock(_mtx);
		if (_streamLists.empty())
			_cond.wait(lock);

		if (_streamLists.size() > 0) { 
			int length = _streamLists.size();
			Mat frame = _streamLists.front()._Frame;// .clone();
			int ch_seq = _streamLists.front()._ch_seq;
			_streamLists.pop_front();
			lock.unlock();

			std::unique_lock<std::mutex> lock2(_mtxDnn);
			std::chrono::time_point<std::chrono::steady_clock> tp = chrono::steady_clock::now();
			if (_bFirsts[ch_seq]) {
				_bFirsts[ch_seq] = false;
			}
			else {
				std::chrono::duration<double> spent = tp - _tps[ch_seq];
				if (spent.count() >= 1 && length > 5) {
					_tps[ch_seq] = tp;
					continue;
				}
			}
			_tps[ch_seq] = tp;

			if (_pInfBase) {
				bool ret = _pInfBase->runInference(ch_seq,frame);
				if (ret == true) {
					_rwMtxInf.lock_shared();
					if (checkCameraStream(ch_seq) == true)
						addEventCamera(ch_seq);

					_rwMtxInf.unlock_shared();
				}
			}

		}
	}
}

void InferenceStream::addCameraSeq(int ch_seq)
{
	_rwMtxInf.lock();
	_cameraLists.push_back(ch_seq);
	_bFirsts[ch_seq] = true;
	_cameraCount++;
	_rwMtxInf.unlock();
}

void InferenceStream::removeCameraSeq(int ch_seq)
{
	_rwMtxInf.lock();
	int idx = 0;
	for (; idx < _cameraCount; idx++) {
		if (_cameraLists[idx] == ch_seq)
			break;
	}

	if (idx < _cameraCount) {
		_cameraLists.erase(_cameraLists.begin() + idx);
		_cameraCount--;
	}
	_bFirsts.erase(_bFirsts.find(ch_seq));
	_rwMtxInf.unlock();
}

bool InferenceStream::checkCameraStream(int ch_seq)
{
	for (int i = 0; i < _cameraCount; i++) {
		if (_cameraLists[i] == ch_seq)
			return true;
	}

	return false;
}

void InferenceStream::addEventCamera(int ch_seq)
{
	_rwMtx_evt.lock();
	if (!checkEventCamera(ch_seq)) {
		_evtLists.push_back(ch_seq);
	}
	_rwMtx_evt.unlock();
}

void InferenceStream::removeEventCamera(int ch_seq)
{
	_rwMtx_evt.lock();
	for (int i = 0; i < _evtLists.size(); i++) {
		if (_evtLists[i] == ch_seq) {
			_evtLists.erase(_evtLists.begin() + i);
			break;
		}
	}
	_rwMtx_evt.unlock();
}

bool InferenceStream::checkEventCamera(int ch_seq)
{
	bool bFound = false;
	for (int i = 0; i < _evtLists.size(); i++) {
		if (_evtLists[i] == ch_seq) {
			bFound = true;
			break;
		}
	}

	return bFound;
}

bool InferenceStream::checkEventCamera(int ch_seq, bool needLock)
{
	bool bFound = false;
	_rwMtx_evt.lock_shared();
	for (int i = 0; i < _evtLists.size(); i++) {
		if (_evtLists[i] == ch_seq) {
			bFound = true;
			break;
		}
	}
	_rwMtx_evt.unlock_shared();

	return bFound;
}
