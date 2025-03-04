#include "pch.h"
#include "streampool.h"
#include <assert.h>

const size_t DEFAULT_CHUNK_SIZE		= 0x200000;	//1MB
const size_t DEFAULT_CHUNK_NUM		= 1;
const size_t MAX_TOKEN_SIZE			= 0x40000;	//256KB


namespace StreamPool {

//////////////////////////////////////////////////////////////////////////////////////
Chunk::Chunk(size_t size)
	: wp_(0)
	, rp_(0)
{
	assert(size != 0);
	chunkSize_ = size;
	data_ = new unsigned char[chunkSize_];
}

Chunk::~Chunk()
{
	delete [] data_;
}

void* Chunk::alloc(size_t size)
{
	if(!isAllocPossible(size))
		return nullptr;

	if(wp_ >= rp_)
	{
		if(wp_ + size > chunkSize_)
			wp_ = 0;
	}

	void* p = data_ + wp_;
	wp_ += (int)size;

	return p;
}

void Chunk::dealloc(void* p, size_t size)
{
	assert(isHas(p, size));
	rp_ = (int)((unsigned char*)p - data_ + size);
}

bool Chunk::isAllocPossible(size_t size)
{
	int w = wp_;
	int r = rp_;

	if(w >= r)
	{
		if(((size_t)(w + size) <= chunkSize_) || ((size_t)r > size))
			return true;
	}
	else
	{
		if((size_t)(r - w) > size)
			return true;
	}

	return false;
}

bool Chunk::isEmpty()
{
	return (wp_ == rp_);
}

bool Chunk::isHas(void* p, size_t size)
{
	if(p < data_)
		return false;

	if(((unsigned char*)p + size) > (data_ + chunkSize_))
		return false;

	return true;
}

size_t Chunk::getSize()
{
	return chunkSize_;
}

void Chunk::reset()
{
	wp_ = 0;
	rp_ = 0;
}


//////////////////////////////////////////////////////////////////////////////////////
Allocator::Allocator(size_t chunk_size, size_t max_chunk_num, bool tokenSizeCheck)
	: usePool_(true)
	, chunkSize_(chunk_size)
	, maxChunkNum_(max_chunk_num)
	, curChunk_(chunks_.end())
	, maxTokenSizeCheck_(tokenSizeCheck)
{
}

Allocator::Allocator(size_t chunk_size, bool tokenSizeCheck)
	: usePool_(true)
	, chunkSize_(chunk_size)
	, maxChunkNum_(DEFAULT_CHUNK_NUM)
	, curChunk_(chunks_.end())
	, maxTokenSizeCheck_(tokenSizeCheck)
{
}

Allocator::Allocator()
	: usePool_(false)
	, chunkSize_(0)
	, maxChunkNum_(0)
	, curChunk_(chunks_.end())
	, maxTokenSizeCheck_(false)
{
}

Allocator::~Allocator()
{
	clear();
}

bool Allocator::setChunk(Chunk* chunk, bool tokenSizeCheck)
{
	if(!usePool_ && chunk && chunks_.empty())
	{
		usePool_			= true;
		chunkSize_			= chunk->getSize();
		maxChunkNum_		= 1;
		maxTokenSizeCheck_	= tokenSizeCheck;
		curChunk_			= chunks_.insert(chunks_.end(), chunk);
		return true;
	}

	return false;
}

void* Allocator::alloc(size_t size, bool new_alloc)
{
	if(usePool_)
	{
		void*  pMem		= nullptr;
		Chunk* pChunk	= nullptr;

		std::unique_lock<std::mutex> lock(mtx_);

		if(!new_alloc && (size < MAX_TOKEN_SIZE || !maxTokenSizeCheck_))
		{
			if((pChunk = getChunk(size)))
				pMem = pChunk->alloc(size);
		}
		else
		{
			pMem = (void*)(new char[size]);
		}

		if(pMem)
		{
			MemToken token;
			token.chunk = pChunk;
			token.p = pMem;
			token.size = size;		
			memTokens_.push_back(token);
		}

		return pMem;
	}
	else
	{
		void* pMem = (void*)(new char[size]);
		return pMem;
	}
}

void Allocator::dealloc(void* p)
{
	if(usePool_)
	{
		std::unique_lock<std::mutex> lock(mtx_);

		MemToken token = memTokens_.front();
		assert(token.p == p);

		if(token.chunk)	
			token.chunk->dealloc(token.p, token.size);
		else
			delete [] token.p;

		memTokens_.pop_front();
	}
	else
	{
		delete [] p;
	}
}

void Allocator::clear()
{
	if(usePool_)
	{
		std::unique_lock<std::mutex> lock(mtx_);

		//MemTokens
		{
			if(maxTokenSizeCheck_)
			{
				for(MemTokenContainer::iterator it = memTokens_.begin(); it != memTokens_.end(); ++it)
				{
					MemToken token = *it;

					if(token.chunk)	
						token.chunk->dealloc(token.p, token.size);
					else
						delete [] token.p;
				}
			}

			memTokens_.clear();
		}
		
		//Chunks
		{
			for(ChunkContainer::iterator it = chunks_.begin(); it != chunks_.end(); ++it)
				delete (*it);

			chunks_.clear();
		}
		
		curChunk_ = chunks_.end();
	}
}

void Allocator::reset()
{
	if(usePool_)
	{
		std::unique_lock<std::mutex> lock(mtx_);

		//MemTokens
		{
			if(maxTokenSizeCheck_)
			{
				for(MemTokenContainer::iterator it = memTokens_.begin(); it != memTokens_.end(); ++it)
				{
					MemToken token = *it;

					if(token.chunk)	
						token.chunk->dealloc(token.p, token.size);
					else
						delete [] token.p;
				}
			}

			memTokens_.clear();
		}

		//Chunks
		{
			for(ChunkContainer::iterator it = chunks_.begin(); it != chunks_.end(); ++it)
				(*it)->reset();
		}

		curChunk_ = chunks_.begin();
	}
}

Chunk* Allocator::getChunk(size_t size)
{
	ChunkContainer::iterator it = curChunk_;

	if(curChunk_ != chunks_.end())
	{
		if((*curChunk_)->isAllocPossible(size))
			return *curChunk_;

		if(++curChunk_ == chunks_.end())
			curChunk_ = chunks_.begin();

		if((*curChunk_)->isAllocPossible(size))
		{
			freeEmptyChunk();
			return *curChunk_;
		}

		++it;
	}
	
	if(chunks_.size() < maxChunkNum_)
	{
		curChunk_ = chunks_.insert(it, new Chunk(chunkSize_));
		return *curChunk_;
	}
	else
	{
		return nullptr;
	}
}

void Allocator::freeEmptyChunk()
{
	ChunkContainer::iterator it = curChunk_;
	++it;

	if(it == chunks_.end())
		it = chunks_.begin();

	if((it != curChunk_) && (*it)->isEmpty())
	{
		delete (*it);
		chunks_.erase(it++);
	}
}

} //namespace StreamPool