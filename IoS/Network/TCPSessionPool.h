#pragma once

#include "TCPSession.h"
#include "lock.h"
#include <set>
#include <list>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TCPSessionPool
{
public:
	TCPSessionPool(int count = 128);
	virtual ~TCPSessionPool();

public:
	TCPSession*	createSession();
	bool		releaseSession(TCPSession* session);
	TCPSession*	getActiveSession();
	void		cleanup();

protected:
	const int MaxSessionCount;

protected:
	std::set<TCPSession*>	_activeSessions;
	std::list<TCPSession*>	_inactiveSessions;
	CMonitor				_lock;
};
