#pragma once

#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"
//tls:thread local stroge
//ÿ���̻߳��ȡ���ͷ��Լ��Ĵ洢�ռ䣬���߳̽���ʱ��Ὺ�ٺÿռ�
//����ڴ��п���ֱ��ȥpageCache
//��threadCacheʵ�ֵģ���ÿһ��threadCache��һ��tls_threadcache  
static void* ConcurrentAlloc(size_t size)
{
	try
	{
		//���size����threadCache��MAX_SIZE����ȥpageCache��������
		if (size > MAXBYTES)
		{
			//��pageCache����
			//������Ҫ��ȡ��span��ҳ��
			size_t npage = sizeClass::RoundUp(size) >> PAGESHIFT;
			Span* span = PageCache::GetInstance()->NewSpan(npage);
			span->_objsize = size;

			void* ptr = (void*)(span->_page_id << PAGESHIFT);
			return ptr;
		}
		else
		{
			//���tls_threadcacheΪ�գ�����threadCache�Ĺ���
			if (tls_threadcache == nullptr)
			{
				tls_threadcache = new ThreadCache;
			}
			//��tls_threadCache�п��ռ�
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
		//��ȡ������span��ӳ��,�����ptr�Ǵ�threadCache�й黹��centralCache����ҳ�ڴ�
		Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
		size_t size = span->_objsize;

		if (size > MAXBYTES)
		{
			//��centralCache�е�span�黹��pageCache�������ǰ�����ҳ���ϲ��ɴ�ҳ
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