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
	SYNC = 1,
};

struct FibArgs{
	int left;
	int right;
	int slot;
	Worker<FibArgs, FuncType>::Task* address;
};

template<> 
void __attribute__((hot)) __attribute__((preserve_none)) Worker<FibArgs, FuncType>::spawn(int left, Worker<FibArgs, FuncType>::Task* address, int slot, int addressOwner){
		if(left >= 2){
			//createFibChildrenAndLaunch(slot, address, left);
			auto syncTaskId = createNewSyncFrameCustom(slot, address);
			createNewSpawnFrameAndWriteArgs(left - 2, syncTaskId, 1);
			createNewSpawnFrameAndWriteArgsAndLaunch(left - 1, syncTaskId, 0);
		}
		else if(address){
  		    _mm_prefetch(&address->args, _MM_HINT_T0);		    		    		    		    
		    __builtin_prefetch(&address->remainingInputs, 1, 3);
		    if(addressOwner == workerId)
		    	writeDataToFrameImpl(address, slot, left, true);
		    else
		        workers[addressOwner]->writeDataToFrameImpl(address, slot, left, false);
		}
		return;
}

template<> 
void __attribute__((hot)) __attribute__((preserve_none)) Worker<FibArgs, FuncType>::sync(int left, Worker<FibArgs, FuncType>::Task* address, int right, int slot, int addressOwner){
		int sum = left + right;
		if(address){
  		    _mm_prefetch(&address->remainingInputs, _MM_HINT_T0);		    		    		    		    		
   		   if(addressOwner == workerId){
		   	writeDataToFrameImpl(address, slot, sum, true);
		   }else{
		   	workers[addressOwner]->writeDataToFrameImpl(address, slot, sum, false);
		   }
		}
   		else{
   			std::cout<<"sum:"<<sum<<"\n";
   			exited[workerId].store(true, std::memory_order_relaxed);
   			std::atomic_thread_fence(std::memory_order_release);
   		}
   		return;
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
    ((Worker<FibArgs, FuncType>*)workers[0])->createNewSpawnFrameAndWriteArgs(42, nullptr, 0);
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
