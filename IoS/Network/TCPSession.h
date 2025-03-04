#pragma once

#include "TCPSesssionParser.h"
#include "thread.h"
#include "lock.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TCPSession;
class ITCPSessionListener {
public:
	virtual void onRecvData(TCPSession* session, const uint8_t *data, int size) = 0;
	virtual void onDisconnected(TCPSession* session) {}
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TCPSession
	: public ITCPSessionParserListener
	, public CThread
{
	///////////////////////////////////////////////////////////////////
	static const int RECV_BUFFER_SIZE = (1280 * 720 * 3);	// (16 * 1024);

public:
	TCPSession();
	virtual ~TCPSession();

public:
	void open(SOCKET sock, SOCKADDR_IN& addr, ITCPSessionListener* listener = nullptr);
	void close();
	bool send(const uint8_t *data, int size);

	SOCKADDR_IN*	sockaddr_in()	{ return &_addr;	}

private:
	SOCKET					_sock;
	SOCKADDR_IN				_addr;
	TCPSessionParser		_parser;
	ITCPSessionListener*	_listener;
	CMonitor				_lock;

private:
	bool sendData(const uint8_t *data, int size);

private:
	// ITCPSessionParserListener
	virtual void onRecvData(const uint8_t *data, int size) override;

	// CThread
	virtual void threadProc() override;
	bool UpdateIOSState(string IoSIP, bool bState);
	bool IoSsetcheck(string IoSIP);

public:
	int	_type = -1;
};
