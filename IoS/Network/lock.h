#pragma once

//===========
// CMonitor
//===========
class CMonitor
{
public:
	class Owner
	{
		CRITICAL_SECTION* m_pLock;

	public:
		Owner(CMonitor& Monitor) : m_pLock(&Monitor.m_CriticalSection)
		{
			EnterCriticalSection(m_pLock);
		}

		Owner(CRITICAL_SECTION *pLock) : m_pLock(pLock)
		{
			EnterCriticalSection(m_pLock);
		}

		virtual ~Owner()
		{
			LeaveCriticalSection(m_pLock);
		}
	};

private:
	friend class Owner;
	CRITICAL_SECTION m_CriticalSection;

public:
	CMonitor()
	{
		InitializeCriticalSection(&m_CriticalSection);
	}

	virtual ~CMonitor()
	{
		DeleteCriticalSection(&m_CriticalSection);
	}
};

//==================
// Critical Section
//==================
class Cs
{
private:
	CRITICAL_SECTION  cs_;

public:
	Cs();
	~Cs();

	void lock(void);
	void unLock(void);	
};

//=======
// Mutex
//=======
class CommonMutex
{
private:
	HANDLE	_mtx;

public:
	CommonMutex(const char* name, bool open = false);
	~CommonMutex();

	void lock(void);
	void unLock(void);
};



template<class T> class __AutoLock
{
public:
	__AutoLock(T* _p, BOOL _bLock = TRUE) 
	{
		p_ = _p; 

		if (_bLock) 
			p_->lock(); 
	}

	~__AutoLock() 
	{ 
		p_->unLock(); 
	}

private:
	__AutoLock();

protected:
	T*			p_;
};


typedef __AutoLock<Cs>			CsLock;
typedef __AutoLock<CommonMutex> MtxLock;

#define CSLOCK(p)		CsLock		__cslock_0__(p)
#define CSLOCK1(p)		CsLock		__cslock_1__(p)
#define CSLOCK2(p)		CsLock		__cslock_2__(p)
#define CSLOCK3(p)		CsLock		__cslock_3__(p)
#define CSLOCK4(p)		CsLock		__cslock_4__(p)
#define CSLOCK5(p)		CsLock		__cslock_5__(p)
#define CSLOCK6(p)		CsLock		__cslock_6__(p)
#define CSLOCK7(p)		CsLock		__cslock_7__(p)
#define CSLOCK8(p)		CsLock		__cslock_8__(p)
#define CSLOCK9(p)		CsLock		__cslock_9__(p)

#define MTXLOCK(p)		MtxLock		__mtxlock_0__(p)
#define MTXLOCK1(p)		MtxLock		__mtxlock_1__(p)
#define MTXLOCK2(p)		MtxLock		__mtxlock_2__(p)
#define MTXLOCK3(p)		MtxLock		__mtxlock_3__(p)
#define MTXLOCK4(p)		MtxLock		__mtxlock_4__(p)
#define MTXLOCK5(p)		MtxLock		__mtxlock_5__(p)
#define MTXLOCK6(p)		MtxLock		__mtxlock_6__(p)
#define MTXLOCK7(p)		MtxLock		__mtxlock_7__(p)
#define MTXLOCK8(p)		MtxLock		__mtxlock_8__(p)
#define MTXLOCK9(p)		MtxLock		__mtxlock_9__(p)
