#pragma once

#include "thread.h"
#include "TCPSessionPool.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ITCPServerListener {
public:
	virtual void onServerSessionOpened(TCPSession* session) = 0;
	virtual void onServerSessionClosed(TCPSession* session) = 0;
	virtual void onRecvData(TCPSession* session, const uint8_t *data, int size) = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TCPServer
	: public ITCPSessionListener
	, public CThread
{
public:
	TCPServer(ITCPServerListener* listener);
	virtual ~TCPServer();

public:
	bool startup(int type = 0, int port = 0, const char* host = nullptr);
	void shutdown();
	bool send(TCPSession* session, const uint8_t *data, int size);

private:
	ITCPServerListener*	_listener;
	SOCKET						_sock;
	uint16_t					_port;
	TCPSessionPool				_sessionPool;
	int							_type = 0;

private:
	// ITCPSessionListener
	virtual void onRecvData(TCPSession* session, const uint8_t *data, int size) override;

	// CThread
	virtual void threadProc() override;
};
