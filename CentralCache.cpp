#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_sInst;
//从pageCache获取span/直接从centralCache获取
Span* CentralCache::GetOneSpan(spanList& list, size_t size)
{
	//先在spanList中找还有内存的span
	Span* it = list.Begin();
	while (it != list.End())
	{
		if (it->_list)
		{
			return it;
		}

		it = it->_next;
	}

	// 走到这里代表着span都没有内存了，只能找pagecache
	Span* span = PageCache::GetInstance()->NewSpan(sizeClass::NumMovePage(size));
	// 切分好挂在list中
	char* start = (char*)(span->_page_id << PAGESHIFT);
	char* end = start + (span->_n << PAGESHIFT);
	while (start < end)
	{
		char* next = start + size;
		// 头插
		NextObj(start) = span->_list;
		span->_list = start;

		start = next;
	}
	//改变已申请span的切分出的单个对象大小
	span->_objsize = size;
	//将span头插到list
	list.PushFront(span);

	return span;
}
//从centralCache获取对象给threadCache，前两个参数为范围，n为对象个数，size为单个对象大小
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t n, size_t size)
{
	//判断获取span的位置 
	size_t i = sizeClass::Index(size);

	//_spanLists[i].Lock();
	//哪个桶去取相应的对象，便加互斥锁，防止重入
	//_spanLists[i].Lock();
	//lock_guard出了作用域就会自己调用析构，无需解锁
	std::lock_guard<std::mutex> lock(_spanLists[i]._mtx);
	//spanList& splist = _spanLists[i];
	//获取span
	Span* span = GetOneSpan(_spanLists[i], size);

	//从span中获取对象,返回start到prev的对象，包括两者
	//找一个有对象的span，要多少给多少，有多少给多少
	//本质上要一个span即可，多出的都是为了后面效率的提升
	size_t j = 1;
	start = span->_list;
	void* cur = start;
	void* prev = start;
	while (j <= n && cur != nullptr)
	{
		prev = cur;
		cur = NextObj(cur);
		++j;
		span->_usecount++;//释放时用
	}

	span->_list = cur;
	end = prev;
	NextObj(prev) = nullptr;

	//_spanLists[i].Unlock();

	return j-1;
}
// 将一定数量的threadCache的对象释放到centralCache的span，满足一定条件后将centralCache的span释放到pageCache， 这里的start是从threadCache中归还给centralCache的小块内存
void CentralCache::ReleaseListToSpans(void* start, size_t byte_size)
{
	size_t i = sizeClass::Index(byte_size);
	std::lock_guard<std::mutex> lock(_spanLists[i]._mtx);

	while (start)
	{
		void* next = NextObj(start);

		//找start这块内存属于哪个span；在同一个页中，所有的地址>>12都为同一个值，_span就是start所属的哪个span
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);

		//把对象插入到span中，这里相当于头插
		NextObj(start) = span->_list;
		span->_list = start;
		span->_usecount--;
		//usecount==0,说明所有page都还回来了，再将整个span还给pageCache
		if (span->_usecount == 0)
		{
			_spanLists[i].Erase(span);
			//span归还给pageCache后，不再挂很多小块内存，将_list置为nullptr
			span->_list = nullptr;
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		}

		start = next;
	}
}
