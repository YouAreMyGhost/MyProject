#include "ThreadCache.h"
#include "CentralCache.h"
//声明与定义分离
//开空间
void* ThreadCache::FetchFromCentralCache(size_t i, size_t size)
{
	// 获取一批对象，数量使用慢启动方式
	// SizeClass::NumMoveSize(size)是上限值
	// 获取一批对象，数量使用慢启动方式,NumMoveSize(size)决定上限，MaxSize()决定下限
	size_t batchNum = min(sizeClass::NumMoveSize(size), _freeLists[i].MaxSize());

	// 去中心缓存获取batch_num个对象
	void* start = nullptr;
	void* end = nullptr;
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, sizeClass::RoundUp(size));
	assert(actualNum > 0);

	//// 如果申请了多个对象，剩下挂到threadCache自由链表，下次申请可直接在threadCache中进行
	// 如果一次申请多个，剩下挂起来，下次申请就不需要找中心缓存
	// 减少锁竞争，提高效率
	if (actualNum > 1)
	{
		_freeLists[i].PushRange(NextObj(start), end, actualNum - 1);
	}
	//MaxSize会逐渐++，但获取的对象最大数目不会超过NumMoveSize
	if (_freeLists[i].MaxSize() == batchNum)
	{
		_freeLists[i].SetMaxSize(_freeLists[i].MaxSize() + 1);
	}

	return start;
}

void* ThreadCache::Allocate(size_t size)
{
	//计算所在桶的下标
	size_t i = sizeClass::Index(size);
	//桶不为空，从链表头弹出一个对象
	if (!_freeLists[i].Empty())
	{
		return _freeLists[i].Pop();
	}
	//桶为空，去centralCache中取空间
	//慢启动
	else
	{
		return FetchFromCentralCache(i, size);
	}
}
//释放空间
void ThreadCache::Deallocate(void* ptr, size_t size)
{
	size_t i = sizeClass::Index(size);
	_freeLists[i].Push(ptr);

	//如果size>=maxsize，进行释放
	if (_freeLists[i].Size() >= _freeLists[i].MaxSize())
	{
		LongListToCentral(_freeLists[i], size);
	}
}

// 释放对象时，链表过长时，回收内存回到中心缓存
void ThreadCache::LongListToCentral(freeList& list, size_t size)
{
	size_t batchNum = list.MaxSize();
	void* start = nullptr;
	void* end = nullptr;
	list.PopRange(start, end, batchNum);

	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}