#pragma once

#include "streampool.h"
#include <condition_variable>
#include <mutex>
#include <deque>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct VideoStreamData
{
	int streamDataType;
	int streamDataSize;
	uint8_t* streamData;
	int frameType;
	SYSTEMTIME streamTime;
};

class StreamDataQ
{
	////////////////////////////////////////////////////////
	struct sQueueData {
		void*	data;
		int		size;

		sQueueData(void* p, int s) : data(p), size(s) {}
	};

	typedef std::deque<sQueueData>	QContainer;

public:
	StreamDataQ();
	virtual ~StreamDataQ();

public:
	void					push(VideoStreamData* stream, uint8_t* pVolData = nullptr, unsigned int volSize = 0);
	VideoStreamData*		front();
	void					pop();
	void					clear();
	size_t					count();
	bool					isEmpty();

private:
	QContainer				_frameQ;
	StreamPool::Allocator	_pool;
	std::mutex				_mtx;
	std::condition_variable _cond;
	long					_countData = 0;
	int						_maxCount;
};
