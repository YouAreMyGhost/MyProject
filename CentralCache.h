//central cache本质是由一个哈希映射的span对象自由链表构成
//为了保证全局只有唯一的central cache，这个类被设计成了单例模式
//centralCache中的内存都是被切分好的，且有一小部分对象已经被使用
#pragma once
#include "Common.h"

class CentralCache
{
private:
	//开184个自由链表做哈希桶，以sizeClass对齐方式映射，每个桶下面挂着很多span，这些span是被切分好的
	spanList _spanLists[NFREELISTS];
public:
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}
	//从centralCache获取对象给threadCache，前两个参数为范围，n为对象个数，size为单个对象大小
	size_t FetchRangeObj(void*& start, void*& end, size_t n, size_t size);
	//从pageCache获取span/直接从centralCache获取
	Span* GetOneSpan(spanList& list, size_t size);
	// 将一定数量的对象释放到span
	void ReleaseListToSpans(void* start, size_t byte_size);
private:
	CentralCache()
	{}
	CentralCache(const CentralCache&) = delete;
	static CentralCache _sInst;
};

