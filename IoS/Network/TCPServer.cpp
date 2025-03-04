#include "pch.h"
#include "TCPServer.h"
#include <WS2tcpip.h>

extern bool g_bMaster;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TCPServer::TCPServer(ITCPServerListener* listener)
	: _listener(listener)
	, _sock(INVALID_SOCKET)
	, _port(0)
{
	TCPSessionParser::PACKET_MAGIC_CODE[0] = 'T';
	TCPSessionParser::PACKET_MAGIC_CODE[1] = 'C';
	TCPSessionParser::PACKET_MAGIC_CODE[2] = 'R';
	TCPSessionParser::PACKET_MAGIC_CODE[3] = 'S';
}

TCPServer::~TCPServer()
{
	shutdown();
}

bool TCPServer::startup(int type, int port, const char* host)
{
	SOCKADDR_IN addr;
	int size_sin = sizeof(struct sockaddr);

	_sock = socket(AF_INET, SOCK_STREAM, 0);

	long optval = 1;
	int optlen = sizeof(optval);
	setsockopt(_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, optlen);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
#if 0
	addr.sin_addr.s_addr = host ? inet_addr(host) : htonl(INADDR_ANY);
#else
	if (host) {
		inet_pton(AF_INET, host, &addr.sin_addr);
	}
	else {
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	}
#endif
	addr.sin_port = htons(port);

	if (::bind(_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
		return false;

	if (listen(_sock, SOMAXCONN) == SOCKET_ERROR)
	{
		closesocket(_sock);
		_sock = INVALID_SOCKET;
		return false;
	}

	_port = port;
	{
		SOCKADDR_IN addr;
		socklen_t len = sizeof(sockaddr_in);

		if (::getsockname(_port, (struct sockaddr*)&addr, &len) != SOCKET_ERROR)
			_port = ntohs(addr.sin_port);
	}

	_type = type;
	
	thrStart();
	return true;
}

void TCPServer::shutdown()
{
	if (_sock != INVALID_SOCKET)
	{
		closesocket(_sock);
		_sock = INVALID_SOCKET;
	}

	thrStop();
}

bool TCPServer::send(TCPSession* session, const uint8_t *data, int size)
{
	return session->send(data, size);
}

void TCPServer::onRecvData(TCPSession* session, const uint8_t *data, int size)
{
	if (_listener)
		_listener->onRecvData(session, data, size);
}

void TCPServer::threadProc()
{
	SOCKADDR_IN addr;
	int size_sin = sizeof(struct sockaddr);

	std::list<TCPSession*>	activeSessions;

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 1;

	ULONG nonBlk = TRUE;
	ioctlsocket(_sock, FIONBIO, &nonBlk);

	while (CThread::thrRunning()) {
		fd_set fdset;
		FD_ZERO(&fdset);
		FD_SET(_sock, &fdset);

		if (select(0, &fdset, nullptr, nullptr, &tv) != SOCKET_ERROR) {
			SOCKET s = accept(_sock, (struct sockaddr*)&addr, &size_sin);

			if (s != INVALID_SOCKET) {
				if (_type == 1) {
					if (g_bMaster == false) {
						closesocket(s);
					}
					else {
						TCPSession* session = _sessionPool.createSession();
						if (session) {
							session->_type = _type;
							session->open(s, addr, this);
							activeSessions.push_back(session);

							if (_listener)
								_listener->onServerSessionOpened(session);
						}
						else {
							closesocket(s);
						}
					}
				}
				else {
					TCPSession* session = _sessionPool.createSession();
					if (session) {
						session->_type = _type;
						session->open(s, addr, this);
						activeSessions.push_back(session);

						if (_listener)
							_listener->onServerSessionOpened(session);
					}
					else {
						closesocket(s);
					}
				}
			}
		}

		auto it = activeSessions.begin();
		while (it != activeSessions.end())
		{
			TCPSession* session = *it;
			if (_type == 1) {
				if (g_bMaster == false)
					session->close();
			}

			if (!session->isRunning())
			{
				if (_listener)
					_listener->onServerSessionClosed(session);

				_sessionPool.releaseSession(session);
				activeSessions.erase(it++);
			}
			else
			{
				it++;
			}
		}
	}

	activeSessions.clear();
}
