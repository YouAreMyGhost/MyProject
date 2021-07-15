#include "PageCache.h"

PageCache PageCache::_sInst;

// ��ϵͳ����kҳ�ڴ�
void* PageCache::SystemAllocPage(size_t k)
{
	return ::SystemAlloc(k);
}

//Span* PageCache::NewSpan(size_t k)
//{
//	std::lock_guard<std::mutex> lock(_mtx);
//	_NewSpan(k);
//}
//��pageCache��ȡspan����centralCache��k������ĸ�Ͱ���л�ȡspan
Span* PageCache::NewSpan(size_t k)
{
	//��������˵ݹ���ã�ʹ�õݹ���recursive_mutex
	std::lock_guard<std::recursive_mutex> lock(_mtx);

	// ���ֱ���������128ҳ�Ĵ���ڴ棬ֱ����ϵͳҪ
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
	//���spanList��Ϊ�գ�����ֱ�ӻ�ȡ
	if (!_spanList[k].Empty())
	{
		return _spanList[k].PopFront();
	}
	//���spanListΪ�գ�Ѱ�Һ���Ĵ�span��Ȼ������С
	for (size_t i = k+1; i < NPAGES; ++i)
	{
		// ��ҳ����С,�г�kҳ��span����
		// �г�i-kҳ�һ���������
		// �з�֮��span��span������ҳ֮����_idSpanMap�ڵ�ӳ���ϵҲҪ�ı�
		if (!_spanList[i].Empty())
		{

			// ͷ��
			/*Span* span = _spanList[i].Begin();
			_spanList->Erase(span);

			Span* splitSpan = new Span;
			splitSpan->_pageId = span->_pageId + k;
			splitSpan->_n = span->_n - k;

			span->_n = k;

			_spanList[splitSpan->_n].Insert(_spanList[splitSpan->_n].Begin(), splitSpan);

			return span;*/

			// β�г�һ��kҳspan
			Span* span = _spanList[i].PopFront();
			//�з�,splitSpanΪ���������span
			Span* split = new Span;
			//�зֺõ�splitSpan��һ��ҳ��pageid
			split->_page_id = span->_page_id + span->_n - k;
			//�зֺõĽ�Ҫ����centralCache��span
			split->_n = k;

			// �ı��г���span��ҳ�ź�span��ӳ���ϵ
			//�ı�_idSpanMap�е�ӳ���ϵ
			{
				//std::lock_guard<std::mutex> lock(_map_mtx);
				for (PAGEID i = 0; i < k; ++i)
				{
					_idSpanMap[split->_page_id + i] = split;
				}
			}

			span->_n -= k;
			//���зֹ���ʣ���span�ҵ���Ӧ��Ͱ
			_spanList[span->_n].PushFront(span);

			return split;
		}
	}
	//�ߵ��������û�к��ʵĴ�span��ֱ������һ��128page�Ĵ�span��֮����������Ĳ��������������з�
	Span* bigSpan = new Span;
	void* memory = SystemAllocPage(NPAGES - 1);
	bigSpan->_page_id = (size_t)memory >> 12;//ҳ�ţ��ڶ��ٸ�4kb
	bigSpan->_n = NPAGES - 1;//�����span�ж���page

	{
		//����span��span��span�����е�ҳ����mapӳ�䣬�ͷſռ�ʱʹ��
		//std::lock_guard<std::mutex> lock(_map_mtx);
		for (PAGEID i = 0; i < bigSpan->_n; ++i)
		{
			PAGEID id = bigSpan->_page_id + i;
			_idSpanMap[id] = bigSpan;
		}
	}

	_spanList[NPAGES - 1].Insert(_spanList[NPAGES - 1].Begin(), bigSpan);
	//�ݹ���ã���һֱ�����з֣�ֱ���ҵ����ʵ�span
	return NewSpan(k);
}
//��ȡ������span��ӳ��,�����obj�Ǵ�threadCache�й黹��centralCache����ҳ�ڴ�
Span* PageCache::MapObjectToSpan(void* obj)
{
	//std::lock_guard<std::mutex> lock(_map_mtx);

	//std::lock_guard<std::recursive_mutex> lock(_mtx);
	//��obj����ڴ������ĸ�span����ͬһ��ҳ�У����еĵ�ַ>>12��Ϊͬһ��ֵ,idΪ��ҳ��pageid
	PAGEID id = (ADDRESINT)obj >> PAGESHIFT;
	//��_idSpanMap��Ѱ��obj��ͨ��pageid��span
	/*auto ret = _idSpanMap.find(id);
	* ////�ҵ��ˣ����л�ȡ->�黹��pageCache
	if (ret != _idSpanMap.end())
	{
		return ret->second;
	}
	////����Ҳ�����һҳ��pageid��˵�������ط����������⣬���ж���
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
//��centralCache�е�span�黹��pageCache�������ǰ�����ҳ���ϲ��ɴ�ҳ
//��centraCache��span��usecount==0�󣬱�ʾ����span��������centralCache����ʱ��������ӿ�
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	//��������span��ҳ��n>128������������ڴ��ͷţ����ò�ʹ��delete
	if (span->_n >= NPAGES)
	{
		//ֻ�����޸ĵĵط�������֮��ľֲ���ҲΪͬ��
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

	////����ڴ���Ƭ
	////����ҳ��ǰ�ϲ�
	while (1)
	{
		PAGEID preId = span->_page_id - 1;
		//����idSpanMap��ͨ��ҳ����span���ҵ����ϲ�span��ǰһ��span�󣬽��кϲ�
		//auto ret = _idSpanMap.find(preId);
		////ǰһ��ҳδ�ҵ�
		////���ret=_idSpanMap.end()����δ�ҵ���end()ʱmap���һ��Ԫ��֮�������Ԫ��
		//if (ret == _idSpanMap.end())
		//{
		//	break;
		//}
		Span* preSpan = _idSpanMap.get(preId);
		if (preSpan == nullptr)
		{
			break;
		}

		// ���ǰһ��ҳ��span����ʹ���У�������ǰ�ϲ�
		if (preSpan->_usecount != 0)
		{
			break;
		}

		// ��ʼ�ϲ�...

		// ����128ҳ������Ҫ�ϲ���
		if (preSpan->_n + span->_n >= NPAGES)
		{
			break;
		}

		//�ϲ�֮ǰ�Ƚ���Ҫ�ϲ���span�����ӹ�ϵ
		_spanList[preSpan->_n].Erase(preSpan);
		//��ǰ�ϲ�
		//����span��pageid=95��n=5������95��96��97��98��99��ҳ
		//prespan��pageid=100��n=3������100��101��102��ҳ
		//�ϲ�Ϊspan��pageid=95��n=8������������ҳ
		span->_page_id = preSpan->_page_id;
		span->_n += preSpan->_n;

		// ����ҳ֮��ӳ���ϵ
		{
			//�ϲ�ҳ����Ҫ�ı�ҳ��span��ӳ���ϵ
			//std::lock_guard<std::mutex> lock(_map_mtx);
			for (PAGEID i = 0; i < preSpan->_n; ++i)
			{
				_idSpanMap[preSpan->_page_id + i] = span;
			}
		}

		delete preSpan;
	}

	////����ҳ���ϲ�
	while (1)
	{
		PAGEID nextId = span->_page_id + span->_n;
		//����idSpanMap��ͨ��ҳ����span���ҵ����ϲ�span�ĺ�һ��span�󣬽��кϲ�
		/*auto ret = _idSpanMap.find(nextId);
		* ////��һ��ҳδ�ҵ�
		////���ret=_idSpanMap.end()����δ�ҵ���end()ʱmap���һ��Ԫ��֮�������Ԫ��
		if (ret == _idSpanMap.end())
		{
			break;
		}*/

		Span* nextSpan = _idSpanMap.get(nextId);
		if (nextSpan == nullptr)
		{
			break;
		}

		//ǰһ��ҳ�в�������ʹ��
		if (nextSpan->_usecount != 0)
		{
			break;
		}

		// ����128ҳ������Ҫ�ϲ���
		if (nextSpan->_n + span->_n >= NPAGES)
		{
			break;
		}
		//�ϲ�֮ǰ�Ƚ���Ҫ�ϲ���span�����ӹ�ϵ
		_spanList[nextSpan->_n].Erase(nextSpan);

		span->_n += nextSpan->_n;

		{
			//�ϲ�ҳ����Ҫ�ı�ҳ��span��ӳ���ϵ
			//std::lock_guard<std::mutex> lock(_map_mtx);
			for (PAGEID i = 0; i < nextSpan->_n; ++i)
			{
				_idSpanMap[nextSpan->_page_id + i] = span;
			}
		}

		delete nextSpan;
	}
	//���뵽��Ӧ��Ͱ��
	_spanList[span->_n].PushFront(span);
}