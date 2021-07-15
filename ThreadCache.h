#pragma once
#include "Common.h"
//threadCache������һ����ϣӳ�������������byteΪ��λ��sizeClass����������ӳ��
class ThreadCache
{
private:
	//��184��Ͱ��ÿ��Ͱ����һ����������,�Էֲ���뷽ʽӳ��,ÿ��Ͱ���涼��С��ռ�
	freeList _freeLists[NFREELISTS];
public:
	//�����붨�����
	//���ռ�
	void* Allocate(size_t size);
	//�ͷſռ�
	void Deallocate(void* ptr, size_t size);
	// �����Ļ����ȡ����
	void* FetchFromCentralCache(size_t index, size_t size);
	// �ͷŶ���ʱ���������ʱ�������ڴ�ص����Ļ���
	void LongListToCentral(freeList& list, size_t size);
};

//tls:thread local stroge�̱߳��ش洢
//thread��__declspec�ؼ���һ����������һ���̱߳��ر���
//����һ��ָ�룬�Ͳ���Ҫÿ����һ���߳̾�ҪthreadCache��
//ֻ�е��߳���������ռ�ʱ�Ż����thread Cache�Ĺ���
static __declspec(thread) ThreadCache* tls_threadcache = nullptr;

// map<int, ThreadCache> idCache;
// TLS  thread local storage