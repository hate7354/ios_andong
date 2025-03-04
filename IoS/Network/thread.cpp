#include "pch.h"
#include "thread.h"
#include <process.h>

////////////////////////////////////////////////////////////////////////////////////////
CThread::CThread()
	: thrRunning_(false)
	, thr_(0)
	, thrID_(0)
	, evtThread_(0)
{
	evtThread_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

CThread::~CThread()
{
	if(evtThread_)
		CloseHandle(evtThread_);
}

UINT CThread::thrStart(int priority)
{	
	if(!thr_)
	{
		thrRunning_	= true;
		ResetEvent(evtThread_);

		thr_ = (HANDLE)_beginthreadex(nullptr, 0, &runningThread, this, 0, &thrID_);
		SetThreadPriority(thr_, priority);		
	}

	return thrID_;
}

void CThread::thrStop(DWORD timeout)
{
	thrRunning_ = false;
	SetEvent(evtThread_);

	if(thr_)
	{
		if(WaitForSingleObject(thr_, timeout) == WAIT_TIMEOUT)
		{
			SuspendThread(thr_);
			TerminateThread(thr_, 0);
			CloseHandle(thr_);
			thr_ = 0;
		}
	}
}

void CThread::thrCloseHandle()
{
	if (thr_)
	{
		CloseHandle(thr_);
		thr_ = nullptr;
	}
}

unsigned __stdcall CThread::runningThread(void* arg)
{	
	CThread* pThread = reinterpret_cast<CThread*>(arg);
	pThread->threadProc();
	pThread->thrCloseHandle();
	_endthreadex(0);
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////
HANDLE thr_create(__threadProc thrProc, void* arg, int priority)
{
	HANDLE thr = (HANDLE)_beginthreadex(nullptr, 0, thrProc, arg, 0, nullptr);
	SetThreadPriority(thr, priority);
	return thr;
}

void thr_close(HANDLE& thr, DWORD timeout)
{
	if(thr)
	{
		if(WaitForSingleObject(thr, timeout) == WAIT_TIMEOUT)
			TerminateThread(thr, 0);

		CloseHandle(thr);
		thr = nullptr;
	}
}