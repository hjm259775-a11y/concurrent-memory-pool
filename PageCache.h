#pragma once
#include "Common.h"
#include "ObjectPool.h"
#include<mutex>
#include<windows.h>

class PageCache {
public:
	static PageCache* GetInstance() {
		static PageCache instance;
		return &instance;
	}
	//全局总仓库
	//返回这个唯一对象的地址

	Span* NewSpan(size_t k) {
		std::lock_guard<std::mutex> lock(page_mtx);
		assert(k > 0);
		assert(k < NPAGES);

		if (!span_lists[k].Empty()) {
			return span_lists[k].PopFront();
		}
		

		for (size_t i = k + 1;i < NPAGES;i++) {
			if (!span_lists[i].Empty()) {
				Span* span = span_lists[i].PopFront();
				Span* left_span = span_pool.New();

				left_span->page_id = span->page_id + k;
				left_span->page_cnt = span->page_cnt - k;
				left_span->obj_size = 0;
				left_span->use_count = 0;
				left_span->free_list = nullptr;
				left_span->prev = nullptr;
				left_span->next = nullptr;

				span->page_cnt = k;
				span->obj_size = 0;
				span->use_count = 0;
				span->free_list = nullptr;
				span->prev = nullptr;
				span->next = nullptr;

				span_lists[left_span->page_cnt].PushFront(left_span);
				return span;
			}
		}









	}
	//向PageCache申请一个 k 页的Span（申请）


	void ReleaseSpan(Span* span);

	//把一个不用的Span还给PageCache（释放）


private:
	PageCache() = default;//设置只可在内部调用

	PageCache(const PageCache&) = delete;
	PageCache& operator=(const PageCache&) = delete;
	//禁止复制和赋值

private:
	SpanList span_lists[NPAGES];//按页数分桶的 Span 链表数组

	ObjectPool<Span> span_pool;//内存池

	std::mutex page_mtx;//锁，防止多线程同时修改 span_list


};