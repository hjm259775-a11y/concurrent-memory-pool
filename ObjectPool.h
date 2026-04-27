#pragma once
#include "Common.h"
template<class T>
class ObjectPool {
public:
	T* New();
	void Delete(T* obj) {
		assert(obj);
		NextObj(obj) = free_list;
		free_list = obj;
	}
	//头插到对象池的空闲列表里
	//即把一个对象还回对象池




private:
	char* memory = nullptr;//在大块内存中申请的原始内存起始位置
	size_t remain_bytes = 0;//记录当前大内存中还有多少字节
	void* free_list = nullptr;
};