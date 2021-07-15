//central cache��������һ����ϣӳ���span��������������
//Ϊ�˱�֤ȫ��ֻ��Ψһ��central cache������౻��Ƴ��˵���ģʽ
//centralCache�е��ڴ涼�Ǳ��зֺõģ�����һС���ֶ����Ѿ���ʹ��
#pragma once
#include "Common.h"

class CentralCache
{
private:
	//��184��������������ϣͰ����sizeClass���뷽ʽӳ�䣬ÿ��Ͱ������źܶ�span����Щspan�Ǳ��зֺõ�
	spanList _spanLists[NFREELISTS];
public:
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}
	//��centralCache��ȡ�����threadCache��ǰ��������Ϊ��Χ��nΪ���������sizeΪ���������С
	size_t FetchRangeObj(void*& start, void*& end, size_t n, size_t size);
	//��pageCache��ȡspan/ֱ�Ӵ�centralCache��ȡ
	Span* GetOneSpan(spanList& list, size_t size);
	// ��һ�������Ķ����ͷŵ�span
	void ReleaseListToSpans(void* start, size_t byte_size);
private:
	CentralCache()
	{}
	CentralCache(const CentralCache&) = delete;
	static CentralCache _sInst;
};

