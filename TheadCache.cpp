#include "ThreadCache.h"
#include "CentralCache.h"
//�����붨�����
//���ռ�
void* ThreadCache::FetchFromCentralCache(size_t i, size_t size)
{
	// ��ȡһ����������ʹ����������ʽ
	// SizeClass::NumMoveSize(size)������ֵ
	// ��ȡһ����������ʹ����������ʽ,NumMoveSize(size)�������ޣ�MaxSize()��������
	size_t batchNum = min(sizeClass::NumMoveSize(size), _freeLists[i].MaxSize());

	// ȥ���Ļ����ȡbatch_num������
	void* start = nullptr;
	void* end = nullptr;
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, sizeClass::RoundUp(size));
	assert(actualNum > 0);

	//// ��������˶������ʣ�¹ҵ�threadCache���������´������ֱ����threadCache�н���
	// ���һ����������ʣ�¹��������´�����Ͳ���Ҫ�����Ļ���
	// ���������������Ч��
	if (actualNum > 1)
	{
		_freeLists[i].PushRange(NextObj(start), end, actualNum - 1);
	}
	//MaxSize����++������ȡ�Ķ��������Ŀ���ᳬ��NumMoveSize
	if (_freeLists[i].MaxSize() == batchNum)
	{
		_freeLists[i].SetMaxSize(_freeLists[i].MaxSize() + 1);
	}

	return start;
}

void* ThreadCache::Allocate(size_t size)
{
	//��������Ͱ���±�
	size_t i = sizeClass::Index(size);
	//Ͱ��Ϊ�գ�������ͷ����һ������
	if (!_freeLists[i].Empty())
	{
		return _freeLists[i].Pop();
	}
	//ͰΪ�գ�ȥcentralCache��ȡ�ռ�
	//������
	else
	{
		return FetchFromCentralCache(i, size);
	}
}
//�ͷſռ�
void ThreadCache::Deallocate(void* ptr, size_t size)
{
	size_t i = sizeClass::Index(size);
	_freeLists[i].Push(ptr);

	//���size>=maxsize�������ͷ�
	if (_freeLists[i].Size() >= _freeLists[i].MaxSize())
	{
		LongListToCentral(_freeLists[i], size);
	}
}

// �ͷŶ���ʱ���������ʱ�������ڴ�ص����Ļ���
void ThreadCache::LongListToCentral(freeList& list, size_t size)
{
	size_t batchNum = list.MaxSize();
	void* start = nullptr;
	void* end = nullptr;
	list.PopRange(start, end, batchNum);

	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}