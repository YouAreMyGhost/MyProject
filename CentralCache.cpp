#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_sInst;
//��pageCache��ȡspan/ֱ�Ӵ�centralCache��ȡ
Span* CentralCache::GetOneSpan(spanList& list, size_t size)
{
	//����spanList���һ����ڴ��span
	Span* it = list.Begin();
	while (it != list.End())
	{
		if (it->_list)
		{
			return it;
		}

		it = it->_next;
	}

	// �ߵ����������span��û���ڴ��ˣ�ֻ����pagecache
	Span* span = PageCache::GetInstance()->NewSpan(sizeClass::NumMovePage(size));
	// �зֺù���list��
	char* start = (char*)(span->_page_id << PAGESHIFT);
	char* end = start + (span->_n << PAGESHIFT);
	while (start < end)
	{
		char* next = start + size;
		// ͷ��
		NextObj(start) = span->_list;
		span->_list = start;

		start = next;
	}
	//�ı�������span���зֳ��ĵ��������С
	span->_objsize = size;
	//��spanͷ�嵽list
	list.PushFront(span);

	return span;
}
//��centralCache��ȡ�����threadCache��ǰ��������Ϊ��Χ��nΪ���������sizeΪ���������С
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t n, size_t size)
{
	//�жϻ�ȡspan��λ�� 
	size_t i = sizeClass::Index(size);

	//_spanLists[i].Lock();
	//�ĸ�Ͱȥȡ��Ӧ�Ķ��󣬱�ӻ���������ֹ����
	//_spanLists[i].Lock();
	//lock_guard����������ͻ��Լ������������������
	std::lock_guard<std::mutex> lock(_spanLists[i]._mtx);
	//spanList& splist = _spanLists[i];
	//��ȡspan
	Span* span = GetOneSpan(_spanLists[i], size);

	//��span�л�ȡ����,����start��prev�Ķ��󣬰�������
	//��һ���ж����span��Ҫ���ٸ����٣��ж��ٸ�����
	//������Ҫһ��span���ɣ�����Ķ���Ϊ�˺���Ч�ʵ�����
	size_t j = 1;
	start = span->_list;
	void* cur = start;
	void* prev = start;
	while (j <= n && cur != nullptr)
	{
		prev = cur;
		cur = NextObj(cur);
		++j;
		span->_usecount++;//�ͷ�ʱ��
	}

	span->_list = cur;
	end = prev;
	NextObj(prev) = nullptr;

	//_spanLists[i].Unlock();

	return j-1;
}
// ��һ��������threadCache�Ķ����ͷŵ�centralCache��span������һ��������centralCache��span�ͷŵ�pageCache�� �����start�Ǵ�threadCache�й黹��centralCache��С���ڴ�
void CentralCache::ReleaseListToSpans(void* start, size_t byte_size)
{
	size_t i = sizeClass::Index(byte_size);
	std::lock_guard<std::mutex> lock(_spanLists[i]._mtx);

	while (start)
	{
		void* next = NextObj(start);

		//��start����ڴ������ĸ�span����ͬһ��ҳ�У����еĵ�ַ>>12��Ϊͬһ��ֵ��_span����start�������ĸ�span
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);

		//�Ѷ�����뵽span�У������൱��ͷ��
		NextObj(start) = span->_list;
		span->_list = start;
		span->_usecount--;
		//usecount==0,˵������page���������ˣ��ٽ�����span����pageCache
		if (span->_usecount == 0)
		{
			_spanLists[i].Erase(span);
			//span�黹��pageCache�󣬲��ٹҺܶ�С���ڴ棬��_list��Ϊnullptr
			span->_list = nullptr;
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		}

		start = next;
	}
}
