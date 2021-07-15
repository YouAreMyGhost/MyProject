// page cache��һ����ϣӳ���span��������������,����centralCache��ͬ
// ��ҳΪ��λ����ӳ�䣬1page��2page��...��128page
// Ϊ�˱�֤ȫ��ֻ��Ψһ��page cache������౻��Ƴ��˵���ģʽ
#pragma once
#include "Common.h"
#include "PageMap.h"

class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_sInst;
	}

	// ��ϵͳ����kҳ�ڴ�ҵ���������
	void* SystemAllocPage(size_t k);
	//��pageCache��ȡspan����centralCache
	Span* NewSpan(size_t k);
	//��ȡ������span��ӳ��
	Span* MapObjectToSpan(void* obj);
	//�ͷſ��е�span�ص�pageCache���ϲ����ڵ�span
	void ReleaseSpanToPageCache(Span* span);
private:
	// ��ҳ��ӳ�䣬Ϊ�������±����������0�Ų��Ҷ���
	spanList _spanList[NPAGES];
	//ҳ����span�γ��໥ӳ�䣬���ͷ��ڴ�ʱ�����ҵ��ð��ڴ滹���ĸ�span��
	//��span�п��ܻ��ж��ҳ���⼸��ҳӳ��ͬһ��span
	//mutex _map_mtx;
	//std::unordered_map<PageId, span*> _idSpanMap;
	TCMalloc_PageMap2<32 - PAGESHIFT> _idSpanMap;//��������Ч�ʸ��� 
	std::recursive_mutex _mtx;


private:
	//���������캯��ֻ���������壬������Ϊ˽�У������Լ��Լ���������ʹ�ã�ֻ���Լ��ͱ����ʹ���Լ������Ψһʵ��
	PageCache()
	{}
	PageCache(const PageCache&) = delete;
	//����ģʽ��Ϊ�˷�ֹ��ι���ʵ�������������Ӵ��������
	//����Ϊstatic�����ǲ������κ�һ����ģ�������������ĺô����ܸ����������
	//�����ڸ������޷��ٴδ����ö�������ȫ�ַ�Χ��Ҳ�޷��ٴδ���
	static  PageCache _sInst;
};
