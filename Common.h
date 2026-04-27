#pragma once
#include <cassert>
#include <cstddef>

constexpr size_t MAX_BYTES = 256 * 1024;//内存池主要管理的小对象上限是256KB
constexpr size_t NFREELISTS = 208;
constexpr size_t PAGE_SHIFT = 13;
constexpr size_t PAGE_SIZE = static_cast<size_t>(1) << PAGE_SHIFT;//即一页大小为8KB，2^13

inline void*& NextObj(void* obj) {
	void** p = (void**)obj;
	return *p;
}
//将空闲对象的前8个字节变成next指针，方便链接

static inline size_t Roundup1(size_t bytes, size_t align_num) {
	return (bytes + align_num - 1) & ~(align_num - 1);
}
//根据 bytes 大小向上取整，即向上取整为 align_num 的倍数

class SizeClass {
public:
	static inline size_t Roundup(size_t bytes) {
		if (bytes <= 128) return Roundup1(bytes, 8);
		else if (bytes <= 1024) return Roundup1(bytes, 16);
		else if (bytes <= 8 * 1024) return Roundup1(bytes, 128);
		else if (bytes <= 64 * 1024) return Roundup1(bytes, 1024);
		else if (bytes <= MAX_BYTES) return Roundup1(bytes, 8 * 1024);

		return Roundup1(bytes, PAGE_SIZE);
	}
	//找到 bytes 需要对应的是哪个 align_num ，用 Roundup 和 Roundup1 配合可直接得出用户申请的大小

	static inline size_t Index(size_t bytes, size_t align_num) {
		assert(bytes > 0);
		return (bytes + align_num - 1) / align_num - 1;
	}
	//Index可得出在自己区间内自己是第几个桶

	static inline size_t Index1(size_t bytes) {
		assert(bytes <= MAX_BYTES);
		static int group_array[4] = { 16,56,56,56 };
		if (bytes <= 128) return Index(bytes, 8);
		else if (bytes <= 1024) return Index(bytes - 128, 16) + group_array[0];
		else if (bytes <= 8 * 1024) return Index(bytes - 1024, 128) + group_array[0] + group_array[1];
		else if (bytes <= 64 * 1024) return Index(bytes - 8 * 1024, 1024) + group_array[0] + group_array[1] + group_array[2];
		else return Index(bytes - 64 * 1024, 8 * 1024) + group_array[0] + group_array[1] + group_array[2] + group_array[3];//后面加的是前面区间的桶
	}
	//Index1 和 Index 配合可得出自身在208个桶里的哪个桶

	static inline size_t NumMoveSize(size_t size) {
		assert(size > 0);
		size_t num = MAX_BYTES / size;
		if (num < 2) num = 2;//防止频繁向下申请
		if (num > 512) num = 512;//卡上限防止占太多缓存
		return num;
	}
	//用总上限除单个对象大小，算下最多可以拿多少个这种对象

	static inline size_t NumMovePage(size_t size) {
		size_t num = NumMoveSize(size);
		size_t bytes = num * size;
		size_t pages = bytes / PAGE_SIZE;
		if (bytes % PAGE_SIZE != 0) {
			pages++;
		}
		return pages;
	}
	//算出拿这些对象需要多少页内存



};

class FreeList {
public:
	void Push(void* obj) {
		assert(obj);
		NextObj(obj) = free_list;
		free_list = obj;
		size++;
	}
	//将一个空闲内存插在链表头上（头插）

	void* Pop() {
		assert(free_list);
		void* obj = free_list;
		free_list = NextObj(obj);
		size--;
		return obj;
	}
	//删除链表头的第一个空间内存（头删）

	bool Empty() const {
		return free_list == nullptr;
	}
	//查看链表是否为空

	size_t Size() const {
		return size;
	}
	//查看当前链表大小


	void PushRange(void* start, void* end, size_t n) {
		assert(start);
		assert(end);
		assert(n > 0);
		NextObj(end) = free_list;
		free_list = start;
		size += n;
	}
	//将一段连接好的长度为 n 的空间内存，插到链表头上

	void PopRange(void*& start, void*& end, size_t n) {
		assert(n > 0);
		assert(n <= size);
		start = free_list;
		end = start;
		for (size_t i = 1;i < n;i++) {
			end = NextObj(end);
		}
		free_list = NextObj(end);
		NextObj(end) = nullptr;
		size -= n;
	}
	//从链表头一次性拿走 n 个空间内存

private:
	void* free_list = nullptr;
	size_t size = 0;
};
//小对象单链表，管理某个桶的空闲内存


struct Span {
	size_t page_id = 0;//起始页数
	size_t page_cnt = 0;//一共多少页
	size_t obj_size = 0;//小对象大小
	size_t use_count = 0;//小对象个数
	void* free_list = nullptr;
	Span* prev = nullptr;
	Span* next = nullptr;
};
//记录一段连续的页数

class SpanList {
public:
	SpanList() {
		head.next = &head;
		head.prev = &head;
	}
	//初始化双向循环空链表

	bool Empty() const {
		return head.next == &head;
	}
	//判断链表是否为空

	Span* Begin() {
		return head.next;
	}
	//返回循环链表第一个节点

	Span* End() {
		return &head;
	}
	//表示循环链表结束位置

	void Insert(Span* pos,Span* span) {
		assert(pos);
		assert(span);
		assert(span != &head);
		Span* prev = pos->prev;
		prev->next = span;
		span->prev = prev;
		span->next = pos;
		pos->prev = span;
	}
	//把span插到pos前面

	void Erase(Span* span) {
		assert(span);
		assert(span != &head);
		Span* prev = span->prev;
		Span* next = span->next;
		prev->next = next;
		next->prev = prev;
		span->prev = nullptr;
		span->next = nullptr;
	}
	//把span从链表里删除

	void PushFront(Span* span) {
		Insert(Begin(), span);
	}
	//将span插入到链表最前面

	Span* PopFront() {
		if (Empty())return nullptr;
		Span* front = Begin();
		Erase(front);
		return front;
	}
	//删除链表第一个节点，并返回
private:
	Span head; 
};
//双向循环链表，管理span







