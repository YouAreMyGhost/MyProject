#pragma once
#include "Common.h"
//threadCache里面是一个哈希映射的自由链表，以byte为单位，sizeClass对齐规则进行映射
class ThreadCache
{
private:
	//开184个桶，每个桶都是一个自由链表,以分层对齐方式映射,每个桶下面都是小块空间
	freeList _freeLists[NFREELISTS];
public:
	//声明与定义分离
	//开空间
	void* Allocate(size_t size);
	//释放空间
	void Deallocate(void* ptr, size_t size);
	// 从中心缓存获取对象
	void* FetchFromCentralCache(size_t index, size_t size);
	// 释放对象时，链表过长时，回收内存回到中心缓存
	void LongListToCentral(freeList& list, size_t size);
};

//tls:thread local stroge线程本地存储
//thread与__declspec关键字一起用来声明一个线程本地变量
//定义一个指针，就不必要每创建一个线程就要threadCache，
//只有当线程真正申请空间时才会进行thread Cache的构造
static __declspec(thread) ThreadCache* tls_threadcache = nullptr;

// map<int, ThreadCache> idCache;
// TLS  thread local storage