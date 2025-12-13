// ================= fib.h =================
#ifndef Fib_H
#define Fib_H
#include <atomic>
#include <condition_variable>
#include <functional>
#include <wsqueue.h>
#include <iostream>
#include "runtime.h"

enum FuncType:bool{
	SPAWN = 0,
	SYNC = 1
};

struct FibArgs{
	int left;
	Worker<FibArgs, FuncType>::Task* address;
	int right;
	int slot;
	
	void inline setValue(int index, int val){
		switch(index){
			case 0: left = val;
				break;
			case 2: right = val;
				break;
			case 3: slot = val;
				break;								
		}
	}
		
	void inline setAddress(int index, Worker<FibArgs, FuncType>::Task* val){
		address = val;
	}
};

template<> 
void inline __attribute__((preserve_none)) Worker<FibArgs, FuncType>::invoke(FuncType funcType, int left, Worker<FibArgs, FuncType>::Task* address, int right, int slot){
	if(funcType == FuncType::SPAWN){
		if(left >= 2){
			auto syncTaskId = createNewFrame(FuncType::SYNC, 4);
			writeAddressToFrame(syncTaskId, 1, address, true);
			writeDataToFrame(syncTaskId, 3, slot, true);
			createNewFrameAndWriteArgs(FuncType::SPAWN, left - 2, syncTaskId, 0, 2);
			createNewFrameAndWriteArgsAndLaunch(FuncType::SPAWN, left - 1, syncTaskId, 0, 0);
		}
		else if(address != nullptr){
			writeDataToFrame(address, slot, left, false);
		}
	}else{
		int sum = left + right;
   		if(address == nullptr){
   			std::cout<<"sum:"<<sum<<"\n";
   			exit(0);
   		}
   		else{
			writeDataToFrame(address, slot, sum, false);
		}
	}
}

template class Runtime<FibArgs, Worker<FibArgs, FuncType> >;

template<>
Runtime<FibArgs, Worker<FibArgs, FuncType>>::Runtime(int numThreads){
	for(int i = 0; i<numThreads; i++){
		Worker<FibArgs, FuncType>* worker = new Worker<FibArgs, FuncType>(i);
		workers.push_back(worker);
	}

	
	for(int i = 0; i<numThreads; i++){
		workers[i]->setWorkers(workers);
	}
}

template<>
void Runtime<FibArgs, Worker<FibArgs,FuncType>>::init(){
    ((Worker<FibArgs, FuncType>*)workers[0])->createNewFrameAndWriteArgs(FuncType::SPAWN, 40, nullptr, 0, 0);
}

template<>
void Runtime<FibArgs, Worker<FibArgs, FuncType>>::run(){
    init();
    for (auto w : workers) 
    	w->start();
    for (auto w : workers) 
    	w->join();
}


#endif
