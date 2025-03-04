#include "pch.h"
#include "StreamDataQ.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
StreamDataQ::StreamDataQ()
	: _pool()
	, _countData(0)
	, _maxCount(50)
{
}

StreamDataQ::~StreamDataQ()
{
	clear();
}

void StreamDataQ::push(VideoStreamData* stream, uint8_t* pVolData, unsigned int volSize)
{
	bool bNotify = false;

	std::unique_lock<std::mutex> lock(_mtx);

	uint8_t* buffer = (uint8_t*)_pool.alloc(sizeof(VideoStreamData) + stream->streamDataSize + volSize);

	if (buffer)
	{
		if (pVolData)
			memcpy(buffer + sizeof(VideoStreamData), pVolData, volSize);

		memcpy(buffer + sizeof(VideoStreamData) + volSize, (void*)stream->streamData, stream->streamDataSize);
		stream->streamDataSize += volSize;
		memcpy(buffer, stream, sizeof(VideoStreamData));

		((VideoStreamData*)buffer)->streamData = buffer + sizeof(VideoStreamData);

		_frameQ.push_back(sQueueData(buffer, sizeof(VideoStreamData) + stream->streamDataSize));
		if (_countData == 0)
			bNotify = true;

		_countData++;
	}

	if (bNotify)
		_cond.notify_one();
}

VideoStreamData* StreamDataQ::front()
{
	std::unique_lock<std::mutex> lock(_mtx);

	if (_countData == 0)
		_cond.wait_for(lock, std::chrono::milliseconds(100));

	if (_countData > 0)
		return (VideoStreamData*)_frameQ.front().data;

	return nullptr;
}

void StreamDataQ::pop()
{
	void* data = front();
	if (data)
	{
		std::unique_lock<std::mutex> lock(_mtx);
		_countData--;
		_frameQ.pop_front();
		_pool.dealloc(data);
	}
}

void StreamDataQ::clear()
{
	std::unique_lock<std::mutex> lock(_mtx);

	_countData = 0;

	QContainer::iterator it = _frameQ.begin();
	for (; it != _frameQ.end(); it++)
		_pool.dealloc((*it).data);

	_frameQ.clear();
	_pool.clear();
}

size_t StreamDataQ::count()
{
	return _countData;
}

bool StreamDataQ::isEmpty()
{
	return (_countData == 0);
}
