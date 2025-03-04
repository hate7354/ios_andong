#pragma once

#include "AutoIncreasingBuffer.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)

typedef struct __tcpPacketHeader {		// 16byte
	uint8_t		code[4];				// Magic Code
	uint32_t	contentLength;			// Content Length
	uint8_t		reserved[8];
} TTCPPacketHeader;

#pragma pack(pop)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ITCPSessionParserListener {
public:
	virtual void onRecvData(const uint8_t *data, int size) = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TCPSessionParser
{
	/////////////////////////////////////////////////////
	enum enState {
		enStateWaitingForFirst,
		enStateWaitingForData
	};

public:
	TCPSessionParser();
	virtual ~TCPSessionParser();

public:
	static char PACKET_MAGIC_CODE[4];

public:
	void open(ITCPSessionParserListener* listener);
	void close();
	int  feed(const uint8_t* buffer, int nbytes);
	
private:
	ITCPSessionParserListener*	_listener;
	enState						_state;
	AutoIncreasingBuffer		_contentBuffer;

private:
	int processMessage(const uint8_t* buffer, int nbytes);
	int findMagicCode(const uint8_t* bytes, int nbytes);
};
