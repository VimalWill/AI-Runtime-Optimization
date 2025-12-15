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
#include <map>
#include <thread>
#include <vector>
#include <semaphore>

std::atomic<int> global_idle_count{0};
std::counting_semaphore sem{0};

struct alignas(64) SpinLock {
  std::atomic_flag flag = ATOMIC_FLAG_INIT;

  void lock() {
    int spins = 1;
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
    FuncTy funcType;
    int left;
    Task* address;
    int right;
    int slot;
    Task* next;
    std::atomic<int32_t> remainingInputs{0};
    Task(){
    }
    
    __attribute__((preserve_none))	
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
	
  __attribute__((preserve_none))	
  void inline setAddress(int index, Task* val){
	address = val;
  }
} Task;

  struct TaskPool {
    Task* front{nullptr};
    Task* freePoolFront{nullptr};
    int numframes{32};
    std::vector<Task*> allocatedPointers;

    __attribute__((cold))
    inline Task* allocateFrame() {
        	Task *tasks = (Task *)malloc(numframes*sizeof(Task));
        	allocatedPointers.push_back(tasks);
        	for(int i = 1; i<numframes-1;i++)
        		tasks[i].next = &tasks[i+1];
        	front = &tasks[1];
		Task* t = &tasks[0];
		tasks[numframes-1].next = nullptr;
        	return t;
        
    }

    inline bool isEmpty(){
    	return !front && !freePoolFront;
    }
    
    __attribute__((hot))    
    inline Task *getFrame() {
        if(front){
        	Task *t = front;
        	front = front->next;
        	t->next = nullptr;
        	return t;
        }
    	Task* t = freePoolFront;
       	freePoolFront = freePoolFront->next;
       	return t;
    }
    
    inline void free(Task* t, bool enqueOnBack = false){
	t->next = freePoolFront;
	freePoolFront = t;
    }
    
    ~TaskPool(){
    	for(int i = 0; i<allocatedPointers.size(); i++)
    		free(allocatedPointers[i]);
    }
  };
  TaskPool pool;
  
  struct ReadyQueue {
	std::deque<Task*> readyQueue;
  	
  	__attribute__((hot))
  	void push_back(Task* t){
		readyQueue.push_back(t);
  	}
  	
  	bool isEmpty(){
  		return readyQueue.empty();
  	}

  	__attribute__((hot))  	
  	Task* pop_back(){
  		if(readyQueue.empty())
  			return nullptr;
		auto t = readyQueue.back();
		readyQueue.pop_back();
		return t;
  	}
  	
  	__attribute__((cold))  	
  	Task* pop_front(){
  		if(readyQueue.empty())
  			return nullptr;  	
		auto t = readyQueue.front();
		readyQueue.pop_front();
		return t;
  	}
  };
  
  void __attribute__((preserve_none)) invoke(FuncTy funcType, int, Task *, int, int, int);
  
  ReadyQueue readyQueue;
  std::thread thread;
  SpinLock waitQueueMutex;
  

  llvm::SmallVector<Worker<Ty, FuncTy> *, 8> workers;

  Worker<Ty, FuncTy>(int workerId) { this->workerId = workerId; }

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
    newTask->remainingInputs.store(numInputs, std::memory_order_relaxed);
    return newTask;
  }
  
  inline Task *createNewFrameCustom(FuncTy fn, int numInputs, int val, Task* addr) {
    Task* newTask;
    if(!pool.isEmpty())
	newTask = pool.getFrame();
    else{
    	newTask = pool.allocateFrame();
    }
    newTask->funcType = fn;
    newTask->slot = val;  
    newTask->address = addr;
    newTask->remainingInputs.store(numInputs, std::memory_order_relaxed);
    return newTask;
  }

  void inline createNewFrameAndWriteArgs(FuncTy fn, int left, Task* address, int right, int slot) {
    Task* newTask;
    if(!pool.isEmpty()){
	    newTask = pool.getFrame();
            newTask->funcType = fn;
            newTask->left = left;
            newTask->address = address;
            newTask->right = right;        
            newTask->slot = slot; 	    
    	    waitQueueMutex.lock();
            readyQueue.push_back(newTask);
            waitQueueMutex.unlock();
    }
    else{
    	newTask = pool.allocateFrame();
        newTask->funcType = fn;
        newTask->left = left;
        newTask->address = address;
        newTask->right = right;        
        newTask->slot = slot;      	
    	waitQueueMutex.lock();
        readyQueue.push_back(newTask);
    	waitQueueMutex.unlock();    	
    }
    if (global_idle_count.load(std::memory_order_relaxed) > 0){
	sem.release();
    }
  }
  
  void inline createNewFrameAndWriteArgsAndLaunch(FuncTy fn, int left, Task* address, int right, int slot){
  	invoke(fn, left, address, right, slot, workerId);
  }

  void inline writeDataToFrameImpl(Task *task, int slot, int val) {
    task->setValue(slot, val);
    if (task->remainingInputs.fetch_sub(1, std::memory_order_relaxed) == 1) {
      waitQueueMutex.lock();
      readyQueue.push_back(task);
      waitQueueMutex.unlock();
      if (global_idle_count.load(std::memory_order_relaxed) > 0){
	sem.release();
      }
    }
  }

  void inline writeAddressToFrameImpl(Task *task, int slot, Task *val) {
    task->args.address = val;
    if (task->remainingInputs.fetch_sub(1, std::memory_order_relaxed) == 1) {
      waitQueueMutex.lock();
      readyQueue.push_back(task);
      waitQueueMutex.unlock();
      if (global_idle_count.load(std::memory_order_relaxed) > 0){
	sem.release();
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
  
  inline bool executeLocalTask() {
    bool valid = false;
    FuncTy fn;
    int left;
    Task* address;
    int right;
    int slot;
    waitQueueMutex.lock();
    Task* frameId = readyQueue.pop_back();
    waitQueueMutex.unlock();         
    if(frameId){
        fn = frameId->funcType;
        left = frameId->left;
        address = frameId->address;
        right = frameId->right;
        slot = frameId->slot;
        pool.free(frameId, false);
	valid = true;
    }
    if(valid)
    	 invoke(fn, left, address, right, slot, workerId);
    return valid;
  }
  
inline bool stealRemoteTask(int id) {
    bool valid = false;
    FuncTy fn;
    int left;
    Task* address;
    int right;
    int slot;
    auto worker = workers[id];
    worker->waitQueueMutex.lock();
    Task* frameId = worker->readyQueue.pop_front();
    worker->waitQueueMutex.unlock();            
    if(frameId){
        fn = frameId->funcType;
        left = frameId->left;
        address = frameId->address;
        right = frameId->right;
        slot = frameId->slot;
        pool.free(frameId, false);        
        valid = true;
    }
    if(valid)
    	 invoke(fn, left, address, right, slot, id);
    return valid;
  }

  __attribute__((hot, flatten)) void workerLoop() {
    while (true) {
      bool valid = false;
      // try to pop from my readyQueue first
      if(!executeLocalTask()){
        for (int i = 0; i < workers.size(); i++) {
          if(i == workerId)
          	continue;
          valid = stealRemoteTask(i);
	  if(valid) break;
        }
        if(!valid){
        	global_idle_count.fetch_add(1, std::memory_order_relaxed);
        	sem.acquire();
        	global_idle_count.fetch_sub(1, std::memory_order_relaxed);
        }
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
