#include "PageCache.h"

PageCache PageCache::_sInst;

// 向系统申请k页内存
void* PageCache::SystemAllocPage(size_t k)
{
	return ::SystemAlloc(k);
}

//Span* PageCache::NewSpan(size_t k)
//{
//	std::lock_guard<std::mutex> lock(_mtx);
//	_NewSpan(k);
//}
//从pageCache获取span交给centralCache，k代表从哪个桶进行获取span
Span* PageCache::NewSpan(size_t k)
{
	//这里进行了递归调用，使用递归锁recursive_mutex
	std::lock_guard<std::recursive_mutex> lock(_mtx);

	// 针对直接申请大于128页的大块内存，直接找系统要
	if (k >= NPAGES)
	{
		void* ptr = SystemAllocPage(k);
		Span* span = new Span;
		span->_page_id = (ADDRESINT)ptr >> PAGESHIFT;
		span->_n = k;

		{
			//std::lock_guard<std::mutex> lock(_map_mtx);
			_idSpanMap[span->_page_id] = span;
		}

		return span;
	}
	//如果spanList不为空，从中直接获取
	if (!_spanList[k].Empty())
	{
		return _spanList[k].PopFront();
	}
	//如果spanList为空，寻找后面的大span，然后将其切小
	for (size_t i = k+1; i < NPAGES; ++i)
	{
		// 大页给切小,切成k页的span返回
		// 切出i-k页挂回自由链表
		// 切分之后span与span内所有页之间在_idSpanMap内的映射关系也要改变
		if (!_spanList[i].Empty())
		{

			// 头切
			/*Span* span = _spanList[i].Begin();
			_spanList->Erase(span);

			Span* splitSpan = new Span;
			splitSpan->_pageId = span->_pageId + k;
			splitSpan->_n = span->_n - k;

			span->_n = k;

			_spanList[splitSpan->_n].Insert(_spanList[splitSpan->_n].Begin(), splitSpan);

			return span;*/

			// 尾切出一个k页span
			Span* span = _spanList[i].PopFront();
			//切分,splitSpan为分离出来的span
			Span* split = new Span;
			//切分好的splitSpan第一个页的pageid
			split->_page_id = span->_page_id + span->_n - k;
			//切分好的将要交给centralCache的span
			split->_n = k;

			// 改变切出来span的页号和span的映射关系
			//改变_idSpanMap中的映射关系
			{
				//std::lock_guard<std::mutex> lock(_map_mtx);
				for (PAGEID i = 0; i < k; ++i)
				{
					_idSpanMap[split->_page_id + i] = split;
				}
			}

			span->_n -= k;
			//将切分过后剩余的span挂到相应的桶
			_spanList[span->_n].PushFront(span);

			return split;
		}
	}
	//走到这里表明没有合适的大span，直接申请一个128page的大span，之后会进行赏面的操作，慢慢进行切分
	Span* bigSpan = new Span;
	void* memory = SystemAllocPage(NPAGES - 1);
	bigSpan->_page_id = (size_t)memory >> 12;//页号，第多少个4kb
	bigSpan->_n = NPAGES - 1;//里面的span有多少page

	{
		//申请span后将span与span中所有的页进行map映射，释放空间时使用
		//std::lock_guard<std::mutex> lock(_map_mtx);
		for (PAGEID i = 0; i < bigSpan->_n; ++i)
		{
			PAGEID id = bigSpan->_page_id + i;
			_idSpanMap[id] = bigSpan;
		}
	}

	_spanList[NPAGES - 1].Insert(_spanList[NPAGES - 1].Begin(), bigSpan);
	//递归调用，会一直进行切分，直到找到合适的span
	return NewSpan(k);
}
//获取对象与span的映射,这里的obj是从threadCache中归还给centralCache的整页内存
Span* PageCache::MapObjectToSpan(void* obj)
{
	//std::lock_guard<std::mutex> lock(_map_mtx);

	//std::lock_guard<std::recursive_mutex> lock(_mtx);
	//找obj这块内存属于哪个span；在同一个页中，所有的地址>>12都为同一个值,id为此页的pageid
	PAGEID id = (ADDRESINT)obj >> PAGESHIFT;
	//在_idSpanMap中寻找obj，通过pageid找span
	/*auto ret = _idSpanMap.find(id);
	* ////找到了，进行获取->归还给pageCache
	if (ret != _idSpanMap.end())
	{
		return ret->second;
	}
	////如果找不到这一页的pageid，说明其他地方有严重问题，进行断言
	else
	{
		assert(false);
		return  nullptr;
	}*/

	Span* span = _idSpanMap.get(id);
	if (span != nullptr)
	{
		return span;
	}
	else
	{
		assert(false);
		return nullptr;
	}
}
//将centralCache中的span归还给pageCache，并检查前后空闲页，合并成大页
//在centraCache的span的usecount==0后，表示整块span都还给了centralCache，这时调用这个接口
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	//针对申请的span的页数n>128的特殊情况的内存释放，不得不使用delete
	if (span->_n >= NPAGES)
	{
		//只将有修改的地方加锁，之后的局部域也为同理
		{
			//std::lock_guard<std::mutex> lock(_map_mtx);
			//_idSpanMap.erase(span->_pageId);
			_idSpanMap.erase(span->_page_id);
		}
		void* ptr = (void*)(span->_page_id << PAGESHIFT);
		SystemFree(ptr);
		delete span;
		return;
	}

	std::lock_guard<std::recursive_mutex> lock(_mtx);

	////解决内存碎片
	////相邻页向前合并
	while (1)
	{
		PAGEID preId = span->_page_id - 1;
		//先在idSpanMap中通过页号找span，找到欲合并span的前一个span后，进行合并
		//auto ret = _idSpanMap.find(preId);
		////前一个页未找到
		////如果ret=_idSpanMap.end()，则未找到，end()时map最后一个元素之后的理论元素
		//if (ret == _idSpanMap.end())
		//{
		//	break;
		//}
		Span* preSpan = _idSpanMap.get(preId);
		if (preSpan == nullptr)
		{
			break;
		}

		// 如果前一个页的span还在使用中，结束向前合并
		if (preSpan->_usecount != 0)
		{
			break;
		}

		// 开始合并...

		// 超过128页，不需要合并了
		if (preSpan->_n + span->_n >= NPAGES)
		{
			break;
		}

		//合并之前先将将要合并的span解链接关系
		_spanList[preSpan->_n].Erase(preSpan);
		//向前合并
		//例：span：pageid=95，n=5，包含95，96，97，98，99五页
		//prespan：pageid=100，n=3，包含100，101，102三页
		//合并为span：pageid=95，n=8包含上述所有页
		span->_page_id = preSpan->_page_id;
		span->_n += preSpan->_n;

		// 更新页之间映射关系
		{
			//合并页后需要改变页与span的映射关系
			//std::lock_guard<std::mutex> lock(_map_mtx);
			for (PAGEID i = 0; i < preSpan->_n; ++i)
			{
				_idSpanMap[preSpan->_page_id + i] = span;
			}
		}

		delete preSpan;
	}

	////相邻页向后合并
	while (1)
	{
		PAGEID nextId = span->_page_id + span->_n;
		//先在idSpanMap中通过页号找span，找到欲合并span的后一个span后，进行合并
		/*auto ret = _idSpanMap.find(nextId);
		* ////后一个页未找到
		////如果ret=_idSpanMap.end()，则未找到，end()时map最后一个元素之后的理论元素
		if (ret == _idSpanMap.end())
		{
			break;
		}*/

		Span* nextSpan = _idSpanMap.get(nextId);
		if (nextSpan == nullptr)
		{
			break;
		}

		//前一个页有部分正在使用
		if (nextSpan->_usecount != 0)
		{
			break;
		}

		// 超过128页，不需要合并了
		if (nextSpan->_n + span->_n >= NPAGES)
		{
			break;
		}
		//合并之前先将将要合并的span解链接关系
		_spanList[nextSpan->_n].Erase(nextSpan);

		span->_n += nextSpan->_n;

		{
			//合并页后需要改变页与span的映射关系
			//std::lock_guard<std::mutex> lock(_map_mtx);
			for (PAGEID i = 0; i < nextSpan->_n; ++i)
			{
				_idSpanMap[nextSpan->_page_id + i] = span;
			}
		}

		delete nextSpan;
	}
	//插入到对应的桶上
	_spanList[span->_n].PushFront(span);
}