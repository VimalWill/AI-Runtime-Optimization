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
void __attribute__((hot)) __attribute__((preserve_none)) Worker<FibArgs, FuncType>::spawn(int left, Worker<FibArgs, FuncType>::Task* address, int slot, int addressOwner, bool lastProducer){
		//count++;
		if(left >= 2){
			//createFibChildrenAndLaunch(slot, address, left);
			auto syncTaskId = createNewSyncFrameCustom(slot, address, left - 2);
			createNewSpawnFrameAndWriteArgsAndLaunch(left - 1, syncTaskId, 0);
		}
		else {
  		    _mm_prefetch(&address->args, _MM_HINT_T0);		    		    		    		    
		    __builtin_prefetch(&address->remainingInputs, 1, 3);
		    if(addressOwner == workerId)
		    	writeDataToFrameImpl(address, slot, left, true, lastProducer);
		    else
		        workers[addressOwner]->writeDataToFrameImpl(address, slot, left, false, false);
		}
		return;
}

template<> 
void __attribute__((hot)) __attribute__((preserve_none)) Worker<FibArgs, FuncType>::sync(int left, Worker<FibArgs, FuncType>::Task* address, int right, int slot, int addressOwner){
		//count++;
		int sum = left + right;		
		if(address){
	   		if(addressOwner == workerId){
				writeDataToFrameImpl(address, slot, sum, true, false);
	                }else{
			   	workers[addressOwner]->writeDataToFrameImpl(address, slot, sum, false, false);
			}
	   		return;
   		}else{
   			int sum = left + right;
   			std::cout<<"sum:"<<sum<<"\n";
   			exited.store(true, std::memory_order_release);
   		}
}

template<> 
void __attribute__((cold)) __attribute__((preserve_none)) Worker<FibArgs, FuncType>::exitTask(int left, int right){
		//count++;
		int sum = left + right;
   		std::cout<<"sum:"<<sum<<"\n";
   		exited.store(true, std::memory_order_release);
   		//std::atomic_thread_fence(std::memory_order_release);		
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
    ((Worker<FibArgs, FuncType>*)workers[0])->createNewSpawnFrameAndWriteArgs(40, 0);
}

template<>
void Runtime<FibArgs, Worker<FibArgs, FuncType>>::run(){
    init();
    for (auto w : workers) 
    	w->start();
    for (auto w : workers) 
    	w->join();
    int total = 0;
    for(auto w:workers)
    	total+=w->count;
    int totalsteals = 0;
    for(auto w:workers)
    	totalsteals += w->numsteals;
   int totaltransitivesteals = 0;
    for(auto w:workers)
    	totaltransitivesteals += w->numtransitivesteals;
   int totalSquashes = 0;
    for(auto w:workers)
    	totalSquashes += w->lastProducerSquash;    	
    std::cout<<"total:"<<total<<", steals:"<<totalsteals<<", totaltransitivesteals:"<<totaltransitivesteals<<", total sqaushes:"<<totalSquashes<<"\n";
}


#endif
