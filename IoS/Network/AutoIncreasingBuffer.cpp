#include "pch.h"
#include "AutoIncreasingBuffer.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
AutoIncreasingBuffer::AutoIncreasingBuffer(int limit)
	: _limit(limit)
	, _position(0)
{
}

AutoIncreasingBuffer::~AutoIncreasingBuffer()
{
	_buffer.clear();
}

bool AutoIncreasingBuffer::put(const void* data, int size)
{
	if (_limit != 0 && _position + size > _limit)
		return false;

	int len = (int)_buffer.size() - _position;
	if (len > 0)
	{
		if (size < len)
			len = size;

		memcpy(&_buffer[_position], data, len);
		_position += len;
	}

	for (int i = 0; i < size - len; i++) 
	{
		_buffer.push_back(((uint8_t*)data)[len + i]);
		++_position;
	}

	return true;
}

int	AutoIncreasingBuffer::position()
{
	return _position;
}

bool AutoIncreasingBuffer::seek(int position)
{
	if (position >= _buffer.size() || position < 0)
		return false;

	_position = position;
	return true;
}

void* AutoIncreasingBuffer::bufferAt(int position)
{
	if (position >= _buffer.size() || position < 0)
		return nullptr;

	return &_buffer[position];
}

void AutoIncreasingBuffer::reset()
{
	_buffer.assign(_buffer.size(), 0);
	_position = 0;
}
