#pragma once
#include "Common.h"
#include "ObjectPool.h"
#include<mutex>
#include<windows.h>
#include<unordered_map>

class PageCache {
public:
	Span* MapObjectToSpan(void* obj) {
		size_t page_id = (size_t)obj >> PAGE_SHIFT;
		auto it = id_span_map.find(page_id);
		if (it == id_span_map.end()) {
			return nullptr;
		}
		return it->second;
	}
	//根据对象地址找出属于哪个 Span

	void RegisterSpan(Span* span) {
		for (size_t i = 0;i < span->page_cnt;i++) {
			id_span_map[span->page_id + i] = span;
		}
	}
	//将 span 覆盖到所管理的每一页上

	void UnregisterSpan(Span* span) {
		for (size_t i = 0;i < span->page_cnt;i++) {
			id_span_map.erase(span->page_id + i);
		}
	}
	//把 span 对应的页数从 map 中删掉



	static PageCache* GetInstance() {
		static PageCache instance;
		return &instance;
	}
	//全局总仓库
	//返回这个唯一对象的地址

	Span* NewSpan(size_t k) {
		std::lock_guard<std::mutex> lock(page_mtx);//加锁
		assert(k > 0);
		assert(k < NPAGES);

		if (!span_lists[k].Empty()) {
			return span_lists[k].PopFront();
		}
		//如果k页这个桶本就有剩余，就直接拿出来返回

		for (size_t i = k + 1;i < NPAGES;i++) {
			if (!span_lists[i].Empty()) {
				Span* span = span_lists[i].PopFront();
				Span* left_span = span_pool.New();

				//开始分这个比所需大小更大的内存
				left_span->page_id = span->page_id + k;//记录去掉 k 后的起始地址
				left_span->page_cnt = span->page_cnt - k;//总大小自然需要减 k
				left_span->obj_size = 0;
				left_span->use_count = 0;
				left_span->free_list = nullptr;
				left_span->prev = nullptr;
				left_span->next = nullptr;//剩下的全部初始化干净，因为还没切成小对象或者挂链表
				//这里是分割后剩下的内存，要放回去

				span->page_cnt = k;
				span->obj_size = 0;
				span->use_count = 0;
				span->free_list = nullptr;
				span->prev = nullptr;
				span->next = nullptr;
				//这里是分割后需要的内存

				RegisterSpan(span);
				RegisterSpan(left_span);
				//在 map 中记录

				span_lists[left_span->page_cnt].PushFront(left_span);//将剩下的内存放回链表桶中
				return span;
			}
		}
		//要是没有k页的桶，就去找更大的桶


		void* ptr = VirtualAlloc(0, (NPAGES - 1) << PAGE_SHIFT, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		assert(ptr);
		//直接向 Windows 申请一块新页内存，128 页每页 8 kb

		Span* big_span = span_pool.New();
		big_span->page_id = (size_t)ptr >> PAGE_SHIFT;//（原地址/2^13）看他在第几页，即算出起始位置
		big_span->page_cnt = NPAGES - 1;//页数为 128 页
		big_span->obj_size = 0;
		big_span->use_count = 0;
		big_span->free_list = nullptr;
		big_span->prev = nullptr;
		big_span->next = nullptr;
		// big_span 用来记录刚申请下来的内存

		if (k == NPAGES - 1) {
			RegisterSpan(big_span);
			//在map中记录

			return big_span;
		}
		//如果本来就需要128页，就不切了，直接给


		Span* left_span = span_pool.New();
		left_span->page_id = big_span->page_id + k;
		left_span->page_cnt = big_span->page_cnt - k;
		left_span->obj_size = 0;
		left_span->use_count = 0;
		left_span->free_list = nullptr;
		left_span->prev = nullptr;
		left_span->next = nullptr;
		//继续拆， left_span 为拆剩下的

		big_span->page_cnt = k;
		// big_span 为需要的内存，需要返回

		span_lists[left_span->page_cnt].PushFront(left_span);
		//剩下的内存装回链表桶

		RegisterSpan(big_span);
		RegisterSpan(left_span);
		//在 map 中记录


		return big_span;

	}
	//向PageCache申请一个 k 页的Span（申请）


	void ReleaseSpan(Span* span) {
		std::lock_guard<std::mutex> lock(page_mtx);
		//加锁
		assert(span);

		span->obj_size = 0;
		span->use_count = 0;
		span->free_list = nullptr;
		span->prev = nullptr;
		span->next = nullptr;

		while (span->page_id > 0) {
			size_t prev_page_id = span->page_id - 1;
			auto it = id_span_map.find(prev_page_id);
			if (it==id_span_map.end()) {
				break;
			}

			Span* prev_span = it->second;
			if (prev_span == span) {
				break;
			}
			if (prev_span->use_count != 0) {
				break;
			}
			if (prev_span->obj_size != 0) {
				break;
			}
			if (prev_span->page_cnt + span->page_cnt >= NPAGES) {
				break;
			}



		}
	}
	//把一个不用的Span还给PageCache（释放）,并且让前后相邻空闲页块合并


private:
	PageCache() = default;//设置只可在内部调用

	PageCache(const PageCache&) = delete;
	PageCache& operator=(const PageCache&) = delete;
	//禁止复制和赋值

private:
	SpanList span_lists[NPAGES];//按页数分桶的 Span 链表数组

	ObjectPool<Span> span_pool;//内存池

	std::mutex page_mtx;//锁，防止多线程同时修改 span_list

private:
	std::unordered_map<size_t, Span*>id_span_map;
	//使用map来记录当前页属于哪个 span
};