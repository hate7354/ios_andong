#pragma once

#include <thread>
#include <mutex>
#include <vector>
#include <list>

#include "DBPostgre.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class EventManager
{
public:
	EventManager(CDBPostgre* pPostDB, std::string evtIP);
	~EventManager();

private:
	void EventProcessThread();

public:
	void pushEventCmd(std::vector<CString>& sCmds);
	bool Connect(SOCKET sock, char* host, int port, int timeout);

private:
	bool			_bLoop;
	std::thread		_th;
	std::list<std::vector<CString>> _sCmdLists;

	std::mutex _mtx;
	std::condition_variable _cond;
	CDBPostgre* _pPostDB = nullptr;
	std::string _evtIP;
	SOCKET _sock = INVALID_SOCKET;
	bool _bConnected = false;
};
