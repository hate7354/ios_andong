#include "pch.h"
#include "lock.h"


//==================
// Critical Section
//==================
Cs::Cs() 
{ 	
	InitializeCriticalSection(&cs_);
}

Cs::~Cs() 
{ 
	DeleteCriticalSection(&cs_);
}

void Cs::lock(void) 
{ 
	EnterCriticalSection(&cs_);
}

void Cs::unLock(void) 
{ 
	LeaveCriticalSection(&cs_);
}

//=======
// Mutex
//=======
CommonMutex::CommonMutex(const char* name, bool open)
{		
	if (open)
		_mtx = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, name);
	else
		_mtx = CreateMutexA(nullptr, FALSE, name);
}

CommonMutex::~CommonMutex()
{
	if (_mtx != INVALID_HANDLE_VALUE)
		CloseHandle(_mtx);
}

void CommonMutex::lock(void)
{
	if (_mtx != INVALID_HANDLE_VALUE)
		::WaitForSingleObject(_mtx, INFINITE);
}

void CommonMutex::unLock(void)
{
	if (_mtx != INVALID_HANDLE_VALUE)
		ReleaseMutex(_mtx);
}
