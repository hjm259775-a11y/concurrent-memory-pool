#pragma once
#include "Common.h"
#include<cstdlib>
template<class T>
class ObjectPool {
public:
	T* New() {
		if (free_list) {
			void* obj = free_list;
			free_list = NextObj(obj);
			return (T*)obj;
		}
		//如果对象池已经有之前回收回来的旧对象，就直接复用

		if (remain_bytes < sizeof(T)) {
			remain_bytes = 128 * 1024;
			memory = (char*)malloc(remain_bytes);// malloc 为向系统要一块指定大小的内存
			assert(memory);
		}
		//如果当前内存不够放一个对象，则重新申请一个新的内存

		T* obj = (T*)memory;
		memory += sizeof(T);
		remain_bytes -= sizeof(T);
		return obj;
	} 
	//优先复用旧对象，不够则申请大块空间

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