//threadCache里面是一个哈希映射的对象自由链表，以byte为单位
//threadCache内存不够时会向centralCache申请
//centralCache周期性的从threadCache中回收内存
//pageCache也是一个哈希映射的对象自由链表，以页为单位
//回收管理centralCache中连续的大块内存
#pragma once
#include <iostream>
#include <exception>
#include <vector>
#include <time.h>
#include <assert.h>
#include <map>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <algorithm>

#ifdef _WIN32
	#include <windows.h>
#else
	// ..
#endif

using std::cout;
using std::endl;

//每个threadCache中最大的空间大小，16个页
static const size_t MAXBYTES = 1024 * 64;
//每个threadCache中freelist最大数量,与sizeClass一起使用，对 
static const size_t NFREELISTS = 184;
//pageCache中哈希桶中页的最大数量
static const size_t NPAGES = 129;
//一页为4096byte，左移12位为1，代表获取一页的数据移动
static const size_t PAGESHIFT = 12;
//为适应不同平台&方便使用，对int&long long进行条件编译&自定义
#ifdef _WIN32
typedef size_t ADDRESINT;
#else
typedef unsigned long long ADDRESINT;
#endif // _WIN32
#ifdef _WIN32
typedef size_t PAGEID;
#else
typedef unsigned long long ADDRESINT;
#endif // _WIN32


inline void*& NextObj(void* obj)
{
	//为了让所开的空间能适应不同系统下的不同数据所占的空间大小
	//使链表在头插时让插入的节点存入它后面节点的指针，之后删除或遍历
	return *((void**)obj);
}

static size_t Index(size_t size)
{
	return ((size + (2^3 - 1)) >> 3) - 1;
}

// 管理对齐和映射等关系
class sizeClass
{
public:
	// 控制在1%-12%左右的内碎片浪费
	// [1,128]					8byte对齐	     freelist[0,16)
	// [129,1024]				16byte对齐		 freelist[16,72)
	// [1025,8*1024]			128byte对齐	     freelist[72,128)
	// [8*1024+1,64*1024]		1024byte对齐     freelist[128,184)
	static inline size_t _RoundUp(size_t bytes, size_t align)
	{
		return (((bytes)+align - 1) & ~(align - 1));
	}

	// 对齐大小计算，浪费大概在1%-12%左右
	static inline size_t RoundUp(size_t bytes)
	{
		//assert(bytes <= MAX_BYTES);

		if (bytes <= 128){
			return _RoundUp(bytes, 8);
		}
		else if (bytes <= 1024){
			return  _RoundUp(bytes, 16);
		}
		else if (bytes <= 8192){
			return  _RoundUp(bytes, 128);
		}
		else if (bytes <= 65536){
			return  _RoundUp(bytes, 1024);
		}
		else
		{
			return _RoundUp(bytes, 1 << PAGESHIFT);
		}

		return -1;
	}

	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	//映射的自由链表的位置，计算所开的空间应该挂在哪里
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAXBYTES);

		// 每个区间有多少个链
		static int group_array[4] = { 16, 56, 56, 56 };
		if (bytes <= 128){
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024){
			return _Index(bytes - 128, 4) + group_array[0];
		}
		else if (bytes <= 8192){
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (bytes <= 65536){
			return _Index(bytes - 8192, 10) + group_array[2] + group_array[1] + group_array[0];
		}

		assert(false);

		return -1;
	}

	//一次从中心缓存取多少个对象
	//单个对象越大，获取的少
	//单个对象越小，获取的多
	//threadCache从centralCache获取对象慢启动的上限值，保证获取内存不会过大
	static size_t NumMoveSize(size_t size)
	{
		if (size == 0)
			return 0;

		// [2, 512]，一次批量移动多少个对象的(慢启动)上限值
		// 小对象一次批量上限高
		// 小对象一次批量上限低
		int num = MAXBYTES / size;
		if (num < 2)
			num = 2;

		if (num > 512)
			num = 512;

		return num;
	}

	// 计算一次向系统获取几个页
	// 单个对象 8byte,16byte,...,64KB，
	//单个对象越小，要的页越少,单个对象越大反之
	static size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size);
		size_t npage = num*size;

		npage >>= 12;
		if (npage == 0)
			npage = 1;

		return npage;
	}
};


class freeList
{
public:
	//将一个范围的大块空间插入到自由链表
	void PushRange(void* start, void* end, int n)
	{
		NextObj(end) = _head;
		_head = start;
		_size += n;
	}

	void PopRange(void*& start, void*& end, int n)
	{
		start = _head;
		for (int i = 0; i < n; ++i)
		{
			end = _head;
			_head = NextObj(_head);
		}

		NextObj(end) = nullptr;
		_size -= n;
	}

	// 头插
	void Push(void* obj)
	{
		NextObj(obj) = _head;
		_head = obj;
		_size += 1;
	}

	// 头删
	void* Pop()
	{
		void* obj = _head;
		_head = NextObj(_head);
		_size -= 1;

		return obj;
	}

	bool Empty()
	{
		return _head == nullptr;
	}
	// 从中心缓存获取对象（慢启动）时与sizeClass中NumMoveSize一起使用，取两者较小值
	size_t MaxSize()
	{
		return _max_size;
	}

	void SetMaxSize(size_t n)
	{
		_max_size = n;
	}

	size_t Size()
	{
		return _size;
	}

private:
	void* _head = nullptr;
	size_t _max_size = 1;
	size_t _size = 0;
};

//span用来管理一个以页为单位跨度的大块内存
//实现为双向带头循环链表 
struct Span
{
	PAGEID _page_id = 0;   //页号，总体第多少个4kb
	size_t _n = 0;        //页数，里面的span有多少page

	Span* _next = nullptr;
	Span* _prev = nullptr;

	void* _list = nullptr;  // 大块内存切小链接起来，这样回收回来的内存也方便链接
	size_t _usecount = 0;	//已经使用的内存计数，当==0时，说明所有对象都没有被使用

	size_t _objsize = 0;	// 切出来的单个对象的大小
};

class spanList
{
public:
	spanList()
	{
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	Span* Begin()
	{
		return _head->_next;
	}

	Span* End()
	{
		return _head;
	}

	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}

	Span* PopFront()
	{
		Span* span = Begin();
		Erase(span);

		return span;
	}

	void Insert(Span* cur, Span* newspan)
	{
		Span* prev = cur->_prev;
		// prev newspan cur
		prev->_next = newspan;
		newspan->_prev = prev;

		newspan->_next = cur;
		cur->_prev = newspan;
	}

	void Erase(Span* cur)
	{
		assert(cur != _head);

		Span* prev = cur->_prev;
		Span* next = cur->_next;

		prev->_next = next;
		next->_prev = prev;
	}

	bool Empty()
	{
		return _head->_next == _head;
	}

	void Lock()
	{
		_mtx.lock();
	}

	void Unlock()
	{
		_mtx.unlock();
	}

private:
	Span* _head;

public:
	std::mutex _mtx;
};

//在系统上开辟空间
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage*(1 << PAGESHIFT),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// brk mmap等
#endif

	if (ptr == nullptr)
		throw std::bad_alloc();

	return ptr;
}
//在系统上释放空间
inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	// sbrk unmmap等
#endif
}