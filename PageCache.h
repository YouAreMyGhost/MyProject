// page cache是一个哈希映射的span对象自由链表构成,但和centralCache不同
// 以页为单位进行映射，1page，2page，...，128page
// 为了保证全局只有唯一的page cache，这个类被设计成了单例模式
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

	// 向系统申请k页内存挂到自由链表
	void* SystemAllocPage(size_t k);
	//从pageCache获取span交给centralCache
	Span* NewSpan(size_t k);
	//获取对象与span的映射
	Span* MapObjectToSpan(void* obj);
	//释放空闲的span回到pageCache，合并相邻的span
	void ReleaseSpanToPageCache(Span* span);
private:
	// 按页数映射，为方便用下标进行索引，0号不挂东西
	spanList _spanList[NPAGES];
	//页号与span形成相互映射，在释放内存时方便找到该把内存还给哪个span，
	//该span中可能会有多个页，这几个页映射同一个span
	//mutex _map_mtx;
	//std::unordered_map<PageId, span*> _idSpanMap;
	TCMalloc_PageMap2<32 - PAGESHIFT> _idSpanMap;//基数树，效率更高 
	std::recursive_mutex _mtx;


private:
	//将拷贝构造函数只声明不定义，且声明为私有，不让自己以及别的类进行使用，只让自己和别的类使用自己构造的唯一实例
	PageCache()
	{}
	PageCache(const PageCache&) = delete;
	//单例模式：为了防止多次构造实例而产生的连接错误的问题
	//定义为static变量是不属于任何一个类的，定义在类里面的好处是受该类域的限制
	//不仅在该类中无法再次创建该对象，且在全局范围内也无法再次创建
	static  PageCache _sInst;
};
