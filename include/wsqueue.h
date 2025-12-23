// ================= worker.h =================
#ifndef Worker_H
#define Worker_H
#include <atomic>
#include <condition_variable>
#include <deque>
#include <emmintrin.h>
#include <functional>
#include <iostream>
#include <llvm/ADT/SmallVector.h>
#include <thread>
#include <vector>
#include <semaphore>

std::atomic<bool> exited[8];

struct alignas(64) SpinLock {
  std::atomic_flag flag = ATOMIC_FLAG_INIT;

  void lock() {
    int spins = 128;
    while (flag.test_and_set(std::memory_order_acquire)) {
      for (int i = 0; i < spins; i++)
        _mm_pause();
      if (spins < 1024)
        spins *= 2;
    }
  }

  void unlock() { flag.clear(std::memory_order_release); }
};

template <typename Ty, typename FuncTy> struct alignas(64) Worker {
  int workerId;

  struct Task;

  typedef struct alignas(64) Task {
    alignas(64) std::atomic<int32_t> remainingInputs{0};
    FuncTy funcType;
    int left;
    Task* __restrict__ address{nullptr};
    int right;
    int slot;
    Task* __restrict__ next;
    int addressOwner;
    Task(){
    }
    
    __attribute__((preserve_none))	
   void inline setValue(int index, int val){
	index == 0? left = val: right = val;
  }
	
  __attribute__((preserve_none))	
  void inline setAddress(int index, Task* val){
	address = val;
  }
} Task;

  struct  alignas(64) TaskPool {
    Task*  __restrict__ front{nullptr};
    Task*  __restrict__ freePoolFront{nullptr};
    int numframes{32};
    std::vector<Task*> allocatedPointers;

    __attribute__((cold))
    inline Task* allocateFrame() {
        	Task *tasks = new Task[numframes];
        	allocatedPointers.push_back(tasks);
        	for(int i = 1; i<numframes-1;i++)
        		tasks[i].next = &tasks[i+1];
        	front = &tasks[1];
		Task* t = &tasks[0];
		tasks[numframes-1].next = nullptr;
        	return t;
        
    }

    inline bool isEmpty(){
    	return front == nullptr && freePoolFront == nullptr;
    }
    
    __attribute__((hot))    
    inline Task *getFrame() {
        if(front){
        	Task *t = front;
        	front = front->next;
        	t->next = nullptr;
        	return t;
        }
        if(freePoolFront){
    		Task* t = freePoolFront;
	       	freePoolFront = freePoolFront->next;
	       	return t;	       	
       	}
	return nullptr;
    }
    
    inline void free(Task* t, bool enqueOnBack = false){
	t->next = freePoolFront;
	t->address = nullptr;
	freePoolFront = t;
    }
    
    ~TaskPool(){
    	for(int i = 0; i<allocatedPointers.size(); i++)
    		delete [] allocatedPointers[i];
    }
  };
  TaskPool pool;
  
  struct  alignas(64) ReadyQueue {
	Task* readyLocalQueue[4];
  	int m{4};
  	int localQueueBack{0};

	Task* readyStealQueue[65536];
	int front{0};
	int back{0};
	int n{65536};
	
  	__attribute__((hot))
  	bool local_push_back(Task* t){
		if(localQueueBack == m)
			return false;
		readyLocalQueue[localQueueBack++] = t;
		return true;
  	}
  	
  	__attribute__((hot))  	
  	Task* local_pop_back(){
  		if(localQueueBack == 0)
  			return nullptr;
		auto t = readyLocalQueue[--localQueueBack];
		return t;
  	}
  	
  	__attribute__((hot))
  	void steal_push_back(Task* t){
		if(back - front == n){
			std::cout<<"queue full\n";
			exit(0);
		}
		readyStealQueue[back++%n] = t;
  	}
  	
  	__attribute__((cold))  	
  	Task* steal_pop_front(){
  		if(back == front){
			return nullptr;
  		}	
		return readyStealQueue[front++%n];
  	}  	
  	
  	__attribute__((cold))  	
  	Task* steal_pop_back(){
  		if(back == front){
			return nullptr;
  		}
		return readyStealQueue[--back%n];
  	}
  };
  
  void __attribute__((preserve_none)) invoke(FuncTy funcType, int, Task *, int, int, int);
  
  ReadyQueue readyQueue;
  std::thread thread;
  SpinLock waitQueueMutex;

  llvm::SmallVector<Worker<Ty, FuncTy> *, 8> workers;

  Worker<Ty, FuncTy>(int workerId) { this->workerId = workerId; exited[workerId].store(false, std::memory_order_relaxed); }

  void inline setWorkers(llvm::SmallVector<Worker<Ty, FuncTy> *, 8> &workers) {
    this->workers = workers;
  }

  void inline join() {
    if (thread.joinable())
      thread.join();
  }

  inline Task *createNewFrame(FuncTy fn, int numInputs) {
    Task* newTask;
    if(!pool.isEmpty())
	    newTask = pool.getFrame();
    else{
    	newTask = pool.allocateFrame();
    }
    newTask->funcType = fn;
    newTask->addressOwner = workerId;
    newTask->remainingInputs.store(numInputs, std::memory_order_relaxed);
    return newTask;
  }
  
  inline Task *createNewFrameCustom(FuncTy fn, int numInputs, int val, Task* addr) {
    Task* newTask = nullptr;
    if(!pool.isEmpty())
	newTask = pool.getFrame();
    else{
    	newTask = pool.allocateFrame();
    }
    assert(newTask != nullptr);
    newTask->funcType = fn;
    newTask->slot = val;  
    newTask->address = addr;
    newTask->addressOwner = workerId;    
    newTask->remainingInputs.store(numInputs, std::memory_order_relaxed);
    return newTask;
  }

__attribute__((always_inline))
  void inline createNewFrameAndWriteArgs(FuncTy fn, int left, Task* address, int right, int slot) {
    Task* newTask = nullptr;
    if(!pool.isEmpty()){
	    newTask = pool.getFrame();
            newTask->funcType = fn;
            newTask->left = left;
            newTask->address = address;
            newTask->right = right;        
            newTask->slot = slot;
            newTask->addressOwner = workerId;
            if(!readyQueue.local_push_back(newTask)){ 	    
    	    	waitQueueMutex.lock();
	        readyQueue.steal_push_back(newTask);
        	waitQueueMutex.unlock();
            }
    }
    else{
    	newTask = pool.allocateFrame();
        newTask->funcType = fn;
        newTask->left = left;
        newTask->address = address;
        newTask->right = right;        
        newTask->slot = slot;
        newTask->addressOwner = workerId;
        if(!readyQueue.local_push_back(newTask)){ 	    
    		waitQueueMutex.lock();
	        readyQueue.steal_push_back(newTask);
        	waitQueueMutex.unlock();
        }  	
    }
    assert(newTask != nullptr);
  }
  
__attribute__((always_inline))  
  void inline createNewFrameAndWriteArgsAndLaunch(FuncTy fn, int left, Task* address, int right, int slot){
  	invoke(fn, left, address, right, slot, address->addressOwner);
  }

__attribute__((always_inline))
  void inline writeDataToFrameImpl(Task *task, int slot, int val, bool enqueueLocally = false) {
    task->setValue(slot, val);
    if (task->remainingInputs.fetch_sub(1, std::memory_order_relaxed) == 1) {
	__builtin_prefetch(&readyQueue, 1, 3);    
    	bool enqueueSuccess = false;
    	if(enqueueLocally){
    		enqueueSuccess = readyQueue.local_push_back(task);
    	}
        if(!enqueueSuccess){ 	    
    		waitQueueMutex.lock();
	        readyQueue.steal_push_back(task);
        	waitQueueMutex.unlock();
        }
    }
  }

  void inline writeAddressToFrameImpl(Task *task, int slot, Task *val, bool enqueueLocally = false) {
    task->args.address = val;
    if (task->remainingInputs.fetch_sub(1, std::memory_order_relaxed) == 1) {
	__builtin_prefetch(&readyQueue, 1, 3);
    	bool enqueueSuccess = false;
    	if(enqueueLocally){
    		enqueueSuccess = readyQueue.local_push_back(task);
    	}
        if(!enqueueSuccess){  
    		waitQueueMutex.lock();
	        readyQueue.steal_push_back(task);
        	waitQueueMutex.unlock();
        } 
    }
  }
  
/*  void inline writeDataToFrame(Task *task, int slot, int val, bool local) {
    if (__builtin_expect(local, 1)) {
      writeDataToFrameImpl(task, slot, val);
    } else {
      workers[task->workerId]->writeDataToFrameImpl(task, slot, val);
    }
  }

  void inline writeAddressToFrame(Task *task, int slot, Task *val, bool local) {
    if (local) {
      writeAddressToFrameImpl(task, slot, val);
    } else {
      workers[task->workerId]->writeAddressToFrameImpl(task, slot, val);
    }
  }
  */
  
inline Task* executeLocalTask() {
   Task * t = readyQueue.local_pop_back();
   if(!t){
   	waitQueueMutex.lock();   
   	t = readyQueue.steal_pop_back();
	waitQueueMutex.unlock();  
   }
   return t;
}

inline Task* stealRemoteTask(int id) {
    workers[id]->waitQueueMutex.lock();       
    Task* frameId =  workers[id]->readyQueue.steal_pop_front();
     workers[id]->waitQueueMutex.unlock();         
    return frameId;
  }

  __attribute__((hot, flatten)) void workerLoop() {
    while (true) {
      bool valid = false;
      __builtin_prefetch(readyQueue.readyLocalQueue, 1, 3);
      __builtin_prefetch(&pool, 1, 3);      
      // try to pop from my readyQueue first
      Task* t = executeLocalTask();
      if(t){
      	     FuncTy fn = t->funcType;
             int left = t->left;
       	     int right = t->right;
             Task* address = t->address;
             int slot = t->slot;
             int addressOwner = t->addressOwner;
	     pool.free(t);
             invoke(fn, left, address, right, slot, addressOwner);
             continue;
      }
      else{
       for (int i = 0; i < workers.size(); i++) {
          if(i == workerId)
          	continue;
      __builtin_prefetch(&workers[i]->waitQueueMutex, 1, 3);          	
      __builtin_prefetch(&workers[i]->readyQueue.readyStealQueue, 1, 3);          	
          t = stealRemoteTask(i);
	  if(t) {
	  	break;
	  }
        }
       }
       	bool end = false;
        if(t){
        	FuncTy fn = t->funcType;
        	int left = t->left;
        	int right = t->right;
        	Task* address = t->address;
        	int slot = t->slot;
        	int addressOwner = t->addressOwner;
	        pool.free(t);
        	invoke(fn, left, address, right, slot, addressOwner);
        	continue;
        }else{
		std::this_thread::yield();
		for(int i = 0; i<workers.size(); i++){
			end = exited[i].load(std::memory_order_relaxed);
			if(end)
				break;
       	        }
        }
        if(end){
		break;        	
	}
      }
    }

  void start() {
    thread = std::thread(&Worker::workerLoop, this);
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(workerId, &cpuset);

    int r = pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t),
                                   &cpuset);
    if (r != 0) {
      perror("pthread_setaffinity_np");
    }
  }
};

#endif
