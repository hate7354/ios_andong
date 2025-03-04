#pragma once

////////////////////////////////////////////////////////////////////////////////////////
class CThread
{
public:
	CThread();
	virtual ~CThread();

public:
	bool	isRunning()	{ return thr_ ? true : false; }

protected:
	UINT thrStart(int priority = THREAD_PRIORITY_NORMAL);
	void thrStop(DWORD timeout = INFINITE);
	void thrCloseHandle();

	bool&	thrRunning()	{ return thrRunning_;	}
	HANDLE	evtThread()		{ return evtThread_;	}

private:
	HANDLE	thr_;
	UINT	thrID_;	
	bool	thrRunning_;
	HANDLE	evtThread_;

private:
	static unsigned __stdcall runningThread(void* arg);
	virtual void threadProc() = 0;
};

////////////////////////////////////////////////////////////////////////////////////////
typedef unsigned (__stdcall *__threadProc)(void* arg);

HANDLE thr_create(__threadProc thrProc, void* arg, int priority = THREAD_PRIORITY_NORMAL);
void   thr_close(HANDLE& thr, DWORD timeout = INFINITE);