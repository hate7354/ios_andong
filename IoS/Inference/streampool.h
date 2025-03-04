#pragma once
#include <mutex>
#include <deque>
#include <list>

namespace StreamPool {

//////////////////////////////////////////////////////////////////////////////////////
class Chunk
{
public:
	Chunk(size_t size);
	~Chunk();

public:
	void*	alloc(size_t size);
	void	dealloc(void* p, size_t size);
	bool	isAllocPossible(size_t size);
	bool	isEmpty();
	bool	isHas(void* p, size_t size);
	size_t	getSize();
	void	reset();

private:
	unsigned char*	data_;
	size_t			chunkSize_;
	int				wp_;
	int				rp_;
};


//////////////////////////////////////////////////////////////////////////////////////
class Allocator
{
	struct MemToken {
		Chunk*	chunk;
		void*	p;
		size_t	size;
	};

	typedef std::list<Chunk*>		ChunkContainer;
	typedef std::deque<MemToken>	MemTokenContainer;

public:
	Allocator(size_t chunk_size, size_t max_chunk_num, bool tokenSizeCheck = true);
	Allocator(size_t chunk_size, bool tokenSizeCheck = true);
	Allocator();
	~Allocator();

public:
	bool	setChunk(Chunk* chunk, bool tokenSizeCheck = true);
	void*	alloc(size_t size, bool new_alloc = false);
	void	dealloc(void* p);
	void	clear();
	void	reset();

	bool	usePool()	{ return usePool_; }

private:
	bool						usePool_;
	size_t						chunkSize_;
	size_t						maxChunkNum_;
	bool						maxTokenSizeCheck_;

	ChunkContainer				chunks_;
	ChunkContainer::iterator	curChunk_;
	MemTokenContainer			memTokens_;
	std::mutex					mtx_;

private:	
	Chunk*	getChunk(size_t size);
	void	freeEmptyChunk();
};

} //namespace StreamPool
