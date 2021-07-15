//若有一个线程走到pageCache进行申请一块内存，ReleaseSpanToPageCache与MapObjectToSpan可能冲突，在前者对span进行合并，对idSpanMap进行修改时
//MapObjectToSpan可能同时对该span进行读取，读取时是不会受到修改所加的锁的限制
//当然也可以在MapObjectToSpan中进行加锁，但是该函数的调用会非常频繁，频繁的加锁与解锁会大幅度降低效率
//在32位系统下，最多只有2^32/2^12个页，可以直接开Span* idSpanMap[2^20]进行映射，但在64位系统中最多有2^64/2^12个页，所需空间极大
//引入基数树，无需加锁，通过多级映射提高效率

#include "Common.h"

template <int BITS>
class TCMalloc_PageMap2 
{
private:
	//指向32个子节点的指针在根节点中放入32个条目，在每个叶节点中放入（2^BITS）/32个条目。
	static const int ROOT_BITS = 5;
	static const int ROOT_LENGTH = 1 << ROOT_BITS;

	static const int LEAF_BITS = BITS - ROOT_BITS;
	static const int LEAF_LENGTH = 1 << LEAF_BITS;

	// Leaf node
	struct Leaf 
	{
		Span* values[LEAF_LENGTH];
	};
	//每个leaf是一个指针数组
	Leaf* root_[ROOT_LENGTH]; // 指向32个子节点的指针

public:
	typedef uintptr_t Number;

	explicit TCMalloc_PageMap2() 
	{
		memset(root_, 0, sizeof(root_));
		PreallocateMoreMemory();
	}

	Span* get(Number k) const 
	{
		const Number i1 = k >> LEAF_BITS;
		const Number i2 = k & (LEAF_LENGTH - 1);
		if ((k >> BITS) > 0 || root_[i1] == NULL) 
		{
			return NULL;
		}
		return root_[i1]->values[i2];
	}

	void set(Number k, Span* v) {
		const Number i1 = k >> LEAF_BITS;
		const Number i2 = k & (LEAF_LENGTH - 1);
		assert(i1 < ROOT_LENGTH);
		root_[i1]->values[i2] = v;
	}

	Span*& operator[](Number k)
	{
		const Number i1 = k >> LEAF_BITS;
		const Number i2 = k & (LEAF_LENGTH - 1);
		assert(i1 < ROOT_LENGTH);
		return root_[i1]->values[i2];
	}

	void erase(Number k)
	{
		const Number i1 = k >> LEAF_BITS;
		const Number i2 = k & (LEAF_LENGTH - 1);
		assert(i1 < ROOT_LENGTH);
		root_[i1]->values[i2] = nullptr;
	}

	bool Ensure(Number start, size_t n) 
	{
		for (Number key = start; key <= start + n - 1;) 
		{
			const Number i1 = key >> LEAF_BITS;

			// Check for overflow
			if (i1 >= ROOT_LENGTH)
				return false;

			// Make 2nd level node if necessary
			if (root_[i1] == NULL) 
			{
				Leaf* leaf = new Leaf;
				if (leaf == NULL) return false;
				memset(leaf, 0, sizeof(*leaf));
				root_[i1] = leaf;
			}

			// Advance key past whatever is covered by this leaf node
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
		}
		return true;
	}

	void PreallocateMoreMemory() 
	{
		// Allocate enough to keep track of all possible pages
		Ensure(0, 1 << BITS);
	}

	void* Next(Number k) const 
	{
		while (k < (1 << BITS)) 
		{
			const Number i1 = k >> LEAF_BITS;
			Leaf* leaf = root_[i1];
			if (leaf != NULL) 
			{
				// Scan forward in leaf
				for (Number i2 = k & (LEAF_LENGTH - 1); i2 < LEAF_LENGTH; i2++) 
				{
					if (leaf->values[i2] != NULL) 
					{
						return leaf->values[i2];
					}
				}
			}
			// Skip to next top-level entry
			k = (i1 + 1) << LEAF_BITS;
		}
		return NULL;
	}
};