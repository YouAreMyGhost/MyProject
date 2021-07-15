#pragma once

#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"
//tls:thread local stroge
//每个线程会获取和释放自己的存储空间，在线程建立时便会开辟好空间
//大块内存有可能直接去pageCache
//由threadCache实现的，将每一个threadCache做一个tls_threadcache  
static void* ConcurrentAlloc(size_t size)
{
	try
	{
		//如果size大于threadCache的MAX_SIZE，就去pageCache里面申请
		if (size > MAXBYTES)
		{
			//在pageCache申请
			//计算需要获取的span的页数
			size_t npage = sizeClass::RoundUp(size) >> PAGESHIFT;
			Span* span = PageCache::GetInstance()->NewSpan(npage);
			span->_objsize = size;

			void* ptr = (void*)(span->_page_id << PAGESHIFT);
			return ptr;
		}
		else
		{
			//如果tls_threadcache为空，进行threadCache的构造
			if (tls_threadcache == nullptr)
			{
				tls_threadcache = new ThreadCache;
			}
			//在tls_threadCache中开空间
			return tls_threadcache->Allocate(size);
		}
	}
	catch (const std::exception& e)
	{
		cout<<e.what()<<endl;
	}
	return nullptr;
}

static void ConcurrentFree(void* ptr)
{
	try
	{
		//获取对象与span的映射,这里的ptr是从threadCache中归还给centralCache的整页内存
		Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
		size_t size = span->_objsize;

		if (size > MAXBYTES)
		{
			//将centralCache中的span归还给pageCache，并检查前后空闲页，合并成大页
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		}
		else
		{
			assert(tls_threadcache);
			tls_threadcache->Deallocate(ptr, size);
		}
	}
	catch (const std::exception& e)
	{
		cout<<e.what()<<endl;
	}
}