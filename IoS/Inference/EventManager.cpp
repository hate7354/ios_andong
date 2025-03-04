#include "pch.h"
#include <WS2tcpip.h>

#include "EventManager.h"

EventManager::EventManager(CDBPostgre* pPostDB, std::string evtIP)
	: _pPostDB(pPostDB)
	, _evtIP(evtIP)
{
	_bLoop = true;
	_th = std::thread{ &EventManager::EventProcessThread, this };
}

EventManager::~EventManager()
{
	_bLoop = false;
	_cond.notify_one();
	_th.join();

	_sCmdLists.clear();
}

void EventManager::pushEventCmd(std::vector<CString>& sCmds)
{
	bool bNotify = false;
	std::unique_lock<std::mutex> lock(_mtx);
	if (_sCmdLists.empty())
		bNotify = true;

	_sCmdLists.emplace_back(sCmds);

	if (bNotify)
		_cond.notify_one();
}

void EventManager::EventProcessThread()
{
	while (_bLoop) {
		std::unique_lock<std::mutex> lock(_mtx);
		if (_sCmdLists.empty())
			_cond.wait(lock);

		if (_sCmdLists.size() > 0) {
			std::vector<CString> sCmds = _sCmdLists.front();
			_sCmdLists.pop_front();
			lock.unlock();

#ifdef _DIRECT_DB
			if (_pPostDB) {
				std::string query = std::string("select nextval('seq_curation_alarms')");
				CString seq_curation_alarms = _pPostDB->dbSelect(0, query);
				CString strDiv, sAlarmSeq;
				::AfxExtractSubString(strDiv, seq_curation_alarms, 1, '#');
				::AfxExtractSubString(sAlarmSeq, strDiv, 0, '$');
				int nAlarmSeq = _ttoi(sAlarmSeq);

				for (int i = 0; i < sCmds.size(); i++) {
					CString sCmd;
					sCmd.Format(sCmds[i], nAlarmSeq);
					_pPostDB->dbInsert(std::string(CT2CA(sCmd)));
				}
			}
#else
			std::string cmd = std::string(CT2CA(sCmds[0]));
//			std::cout << "cmd = " << cmd << std::endl;

			if (_sock == INVALID_SOCKET)
				_sock = socket(AF_INET, SOCK_STREAM, 0);

			if (_sock != INVALID_SOCKET) {
				if (_bConnected == false) {
					if (Connect(_sock, (char*)_evtIP.c_str(), 9080, 5) == false) {
						closesocket(_sock);
						_sock = INVALID_SOCKET;
					}
					else {
						_bConnected = true;
					}
				}

				if (_bConnected) {
					cmd += "\n";
					send(_sock, cmd.c_str(), cmd.length(), 0);
				}
			}
#endif
		}
	}
}

bool EventManager::Connect(SOCKET sock, char* host, int port, int timeout)
{
	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, host, &addr.sin_addr);

	ULONG nonBlk = TRUE;
	if (ioctlsocket(sock, FIONBIO, &nonBlk) == SOCKET_ERROR) {
		return false;
	}

	int nRet = ::connect(sock, (const struct sockaddr*)&addr, sizeof(addr));
	if (nRet == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
		return false;
	}

	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET((unsigned int)sock, &fdset);

	struct timeval tv;
	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	if (select(0, NULL, &fdset, NULL, &tv) == SOCKET_ERROR) {
		return false;
	}

	if (!FD_ISSET(sock, &fdset)) {
		return false;
	}

	nonBlk = FALSE;
	if (ioctlsocket(sock, FIONBIO, &nonBlk) == SOCKET_ERROR) {
		return false;
	}

	return true;
}
