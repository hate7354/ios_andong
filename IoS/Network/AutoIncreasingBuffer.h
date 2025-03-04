#pragma once

#include <vector>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class AutoIncreasingBuffer
{
public:
	AutoIncreasingBuffer(int limit = 0);
	virtual ~AutoIncreasingBuffer();

public:
	bool	put(const void* data, int size);
	int		position();
	bool	seek(int position);	
	void*	bufferAt(int position);
	void	reset();

private:
	std::vector<uint8_t>	_buffer;
	int						_limit;
	int						_position;	
};
