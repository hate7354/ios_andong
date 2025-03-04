#include "pch.h"
#include "TCPSesssionParser.h"
#include <assert.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
char TCPSessionParser::PACKET_MAGIC_CODE[4] = { '*', '*', '*', '*' };

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TCPSessionParser::TCPSessionParser()
	: _listener(nullptr)
	, _state(enStateWaitingForFirst)
{
}

TCPSessionParser::~TCPSessionParser()
{
}

void TCPSessionParser::open(ITCPSessionParserListener* listener)
{
	_listener = listener;
}

void TCPSessionParser::close()
{
	_listener = nullptr;
}

int TCPSessionParser::feed(const uint8_t* buffer, int nbytes)
{
	if (nbytes == 0)
		return 0;

	int remain = 0;
	switch (_state)
	{
		case enStateWaitingForFirst:
		{
			if (nbytes < sizeof(TTCPPacketHeader))
				return nbytes;

			int offset = findMagicCode(buffer, nbytes);

			if (offset != -1)
			{
				if (nbytes - offset < sizeof(TTCPPacketHeader))
					return nbytes;

				_state = enStateWaitingForData;
				remain = processMessage(buffer + offset, nbytes - offset);
			}

			break;
		}

		case enStateWaitingForData:
			remain = processMessage(buffer, nbytes);
			break;
	}

	return remain;
}

int TCPSessionParser::processMessage(const uint8_t* buffer, int nbytes)
{
	const void* buf = (_contentBuffer.position() == 0) ? buffer : _contentBuffer.bufferAt(0);
	TTCPPacketHeader* header = (TTCPPacketHeader*)buf;

	int dataSize = sizeof(TTCPPacketHeader) + header->contentLength;
	int putSize = dataSize - _contentBuffer.position();

	if (nbytes < putSize)
		putSize = nbytes;

	if (putSize > 0)
		_contentBuffer.put(buffer, putSize);

	if (_contentBuffer.position() == dataSize)
	{
		header = (TTCPPacketHeader*)_contentBuffer.bufferAt(0);
		uint8_t *data = (header->contentLength > 0) ? (uint8_t *)_contentBuffer.bufferAt(sizeof(TTCPPacketHeader)) : nullptr;

		if (_listener)
			_listener->onRecvData(data, header->contentLength);

		_state = enStateWaitingForFirst;
		_contentBuffer.seek(0);
	}

	if (putSize < nbytes)
		return feed(buffer + putSize, nbytes - putSize);

	return 0;
}

int TCPSessionParser::findMagicCode(const uint8_t* bytes, int nbytes)
{
	assert(nbytes > 0);

	for (int i = 0; i < nbytes - 3; i++)
	{
		if (strncmp((char*)(bytes + i), PACKET_MAGIC_CODE, 4) == 0)
			return i;
	}

	return -1;
}
