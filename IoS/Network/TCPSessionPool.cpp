#include "pch.h"
#include "TCPSessionPool.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TCPSessionPool::TCPSessionPool(int count)
	: MaxSessionCount(count)
{
}

TCPSessionPool::~TCPSessionPool()
{
	cleanup();
}

TCPSession*	TCPSessionPool::createSession()
{
	CMonitor::Owner lock(_lock);

	TCPSession* session = nullptr;

	if (!_inactiveSessions.empty())
	{
		session = _inactiveSessions.back();
		_inactiveSessions.pop_back();
	}

	if (!session) {
		session = new TCPSession();
	}

	if (session) {
		_activeSessions.insert(session);
	}

	return session;
}

bool TCPSessionPool::releaseSession(TCPSession* session)
{
	CMonitor::Owner lock(_lock);

	std::set<TCPSession*>::iterator it_active = _activeSessions.find(session);
	if (it_active == _activeSessions.end())
		return false;

	_activeSessions.erase(session);
	_inactiveSessions.push_back(session);
	return true;
}

TCPSession*	TCPSessionPool::getActiveSession()
{
	CMonitor::Owner lock(_lock);

	if (!_activeSessions.empty())
		return *_activeSessions.begin();

	return nullptr;
}

void TCPSessionPool::cleanup()
{
	CMonitor::Owner lock(_lock);

	std::set<TCPSession*>::iterator it_active = _activeSessions.begin();
	while (it_active != _activeSessions.end())
	{
		delete (*it_active);
		_activeSessions.erase(it_active++);
	}

	std::list<TCPSession*>::iterator it_inactive = _inactiveSessions.begin();
	while (it_inactive != _inactiveSessions.end())
	{
		delete (*it_inactive);
		_inactiveSessions.erase(it_inactive++);
	}
}
