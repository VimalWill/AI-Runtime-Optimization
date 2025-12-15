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
	int right;
	int slot;
	Worker<FibArgs, FuncType>::Task* address;
};

template<> 
void __attribute__((hot)) __attribute__((preserve_none)) Worker<FibArgs, FuncType>::invoke(FuncType funcType, int left, Worker<FibArgs, FuncType>::Task* address, int right, int slot, int addressOwner){
	if(funcType == FuncType::SPAWN){
		if(left >= 2){
			auto syncTaskId = createNewFrameCustom(FuncType::SYNC, 2,  slot, address);
			createNewFrameAndWriteArgs(FuncType::SPAWN, left - 2, syncTaskId, 0, 2);
			createNewFrameAndWriteArgsAndLaunch(FuncType::SPAWN, left - 1, syncTaskId, 0, 0);
		}
		else if(address != 0){
		    if(addressOwner == workerId)
		    	writeDataToFrameImpl(address, slot, left);
		    else
		        workers[addressOwner]->writeDataToFrameImpl(address, slot, left);
		}
	}else{
		int sum = left + right;
   		if(address == nullptr){
   			std::cout<<"sum:"<<sum<<"\n";
   			exit(0);
   		}
   		else{
   		   if(addressOwner == workerId){
		   	writeDataToFrameImpl(address, slot, sum);
		   }else{
		   	workers[addressOwner]->writeDataToFrameImpl(address, slot, sum);
		   }
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
