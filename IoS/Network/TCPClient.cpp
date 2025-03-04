#include "pch.h"
#include "TCPClient.h"
#include <WS2tcpip.h>
#include <iostream>
#include <string>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TCPClient::TCPClient() : _listener(NULL)
{
	TCPSessionParser::PACKET_MAGIC_CODE[0] = 'T';
	TCPSessionParser::PACKET_MAGIC_CODE[1] = 'C';
	TCPSessionParser::PACKET_MAGIC_CODE[2] = 'R';
	TCPSessionParser::PACKET_MAGIC_CODE[3] = 'S';
}

TCPClient::~TCPClient()
{
}

void TCPClient::setListener(ITCPClientListener* listener)
{
	_listener = listener;
}

bool TCPClient::connect(const char* host, int port, int timeout)
{
	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, host, &addr.sin_addr);
//	addr.sin_addr.s_addr = inet_addr(host);

	SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
		return false;

	ULONG nonBlk = TRUE;
	if (ioctlsocket(sock, FIONBIO, &nonBlk) == SOCKET_ERROR) {
		closesocket(sock);
		return false;
	}

	int nRet = ::connect(sock, (const struct sockaddr*)&addr, sizeof(addr));
	if (nRet == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
		closesocket(sock);
		return false;
	}

	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET((unsigned int)sock, &fdset);

	struct timeval tv;
	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	if (select(0, NULL, &fdset, NULL, &tv) == SOCKET_ERROR) {
		closesocket(sock);
		return false;
	}

	if (!FD_ISSET(sock, &fdset)) {
		closesocket(sock);
		return false;
	}

	_session.open(sock, addr, this);

	return true;
}

void TCPClient::disconnect()
{
	_session.close();
}

bool TCPClient::send(const uint8_t* data, int size)
{
	return _session.send(data, size);
}

void TCPClient::onRecvData(TCPSession* session, const uint8_t* data, int size)
{
	if (_listener)
		_listener->onRecvData(data, size);
}

void TCPClient::onDisconnected(TCPSession* session)
{
	if (_listener)
		_listener->onDisconnected();
}
