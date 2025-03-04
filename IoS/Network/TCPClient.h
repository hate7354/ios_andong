#pragma once

#include "TCPSession.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ITCPClientListener {
public:
	virtual void onDisconnected() = 0;
	virtual void onRecvData(const uint8_t* data, int size) = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TCPClient
	: public ITCPSessionListener
{
public:
	TCPClient();
	virtual ~TCPClient();

public:
	void setListener(ITCPClientListener* listener);
	bool connect(const char* host, int port, int timeout = 3);
	void disconnect();
	bool send(const uint8_t* data, int size);

private:
	ITCPClientListener*	_listener;
	TCPSession					_session;

private:
	// ITCPSessionListener
	virtual void onRecvData(TCPSession* session, const uint8_t* data, int size) override;
	virtual void onDisconnected(TCPSession* session) override;
};
