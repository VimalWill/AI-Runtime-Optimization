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
std::binary_semaphore sem{0};

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
    Ty args;
    int workerId;
    std::atomic<int32_t> remainingInputs{0};
    Task *next{nullptr};
    Task *qPrev{nullptr};
    Task *qNext{nullptr};   
    Task(FuncTy funcType, Ty args, int workerId) {
      this->funcType = funcType;
      this->args = args;
    }
    Task(){
    }
    
  } Task;

  struct TaskPool {
    Task* front{nullptr};
    Task* back{nullptr};

    inline Task* allocateFrame() {
        Task *tasks = (Task *)calloc(16, sizeof(Task));
        for (int i = 0; i < 15; i++){
            tasks[i].next = &tasks[i + 1];
        }
        front = &tasks[1];
        back = &tasks[15];
	Task* t = &tasks[0];
        return t;
    }

    inline bool isEmpty(){
    	return !front;
    }
    
    inline Task *getFrame() {
        Task *t = front;
        front = front->next;
        t->next = nullptr;
        return t;
    }

    inline void free(Task *t) {
        back->next = t;
        back = t;
        t->next = nullptr;
    }
  };
  TaskPool pool;
  
  struct ReadyQueue {
  	Task* front{nullptr};
  	Task* back{nullptr};
  	
  	inline bool isEmpty(){
  		return (!back || !front);
  	}
  	
  	inline void push_back(Task* t){
  		if(back == nullptr){
  			back = front = t;
  			t->qPrev = t->qNext = nullptr;
  		}else{
  			back->qNext = t;
  			t->qPrev = back;
  			back = t;
  			back->qNext = nullptr;
  		}
  	}
  	
  	inline Task* pop_back(){
		Task* t = back;
		if(back->qPrev == nullptr){
			back = front = nullptr;
		}else{
			back = back->qPrev;
			back->qNext = nullptr;
		}
		return t;
  	}
  	
  	inline Task* pop_front(){
  		Task* t = front;
  		if(front->qNext == nullptr){
  			front = back = nullptr;
  		}else{
  			front = front->qNext;
  			front->qPrev = nullptr;
  		}
  		return t;
  	}
  };
  
  void __attribute__((preserve_none)) invoke(FuncTy funcType, int, Task *, int, int);
  
  ReadyQueue readyQueue;
  std::thread thread;
  SpinLock waitQueueMutex;
  

  llvm::SmallVector<Worker<Ty, FuncTy> *> workers;

  Worker<Ty, FuncTy>(int workerId) { this->workerId = workerId; }

  void inline setWorkers(llvm::SmallVector<Worker<Ty, FuncTy> *> &workers) {
    this->workers = workers;
  }

  void inline join() {
    if (thread.joinable())
      thread.join();
  }

  inline Task *createNewFrame(FuncTy fn, int numInputs) {
    Task* newTask;
    if(!pool.isEmpty()){
      	newTask = pool.getFrame();
    }
    else{
    	waitQueueMutex.lock();
    	newTask = pool.allocateFrame();
    	waitQueueMutex.unlock();
    }
    newTask->funcType = fn;
    newTask->workerId = workerId;
    newTask->remainingInputs.store(numInputs, std::memory_order_relaxed);
    return newTask;
  }

  void inline createNewFrameAndWriteArgs(FuncTy fn, int left, Task* address, int right, int slot) {
    Task* newTask;
    if(!pool.isEmpty()){
      	newTask = pool.getFrame();
      	newTask->funcType = fn;
        newTask->args.left = left;
        newTask->args.address = address;
        newTask->args.right = right;        
        newTask->args.slot = slot;        
        newTask->workerId = workerId;
        waitQueueMutex.lock();
        readyQueue.push_back(newTask);
    	waitQueueMutex.unlock();
    }
    else{
    	waitQueueMutex.lock();
    	newTask = pool.allocateFrame();
    	newTask->funcType = fn;
        newTask->args.left = left;
        newTask->args.address = address;
        newTask->args.right = right;        
        newTask->args.slot = slot;    
        newTask->workerId = workerId;
        readyQueue.push_back(newTask);
    	waitQueueMutex.unlock();
    }
    if (global_idle_count.load(std::memory_order_release) > 0){
	sem.release();
    }
  }
  
  void inline createNewFrameAndWriteArgsAndLaunch(FuncTy fn, int left, Task* address, int right, int slot){
  	invoke(fn, left, address, right, slot);
  }

  void inline writeDataToFrameImpl(Task *task, int slot, int val, bool local) {
    task->args.setValue(slot, val);
    if (task->remainingInputs.fetch_sub(1, std::memory_order_relaxed) == 1) {
      waitQueueMutex.lock();
      readyQueue.push_back(task);
      waitQueueMutex.unlock();
      if (global_idle_count.load(std::memory_order_release) > 0){
	sem.release();
      }
    }
  }

  void inline writeAddressToFrameImpl(Task *task, int slot, Task *val, bool local) {
    task->args.setAddress(slot, val);
    if (task->remainingInputs.fetch_sub(1, std::memory_order_relaxed) == 1) {
      waitQueueMutex.lock();
      readyQueue.push_back(task);
      waitQueueMutex.unlock();
      if (global_idle_count.load(std::memory_order_release) > 0){
	sem.release();
      }      
    }
  }

  void inline writeDataToFrame(Task *task, int slot, int val, bool local) {
    if (local) {
      writeDataToFrameImpl(task, slot, val, true);
    } else {
      workers[task->workerId]->writeDataToFrameImpl(task, slot, val, false);
    }
  }

  void inline writeAddressToFrame(Task *task, int slot, Task *val, bool local) {
    if (local) {
      writeAddressToFrameImpl(task, slot, val, true);
    } else {
      workers[task->workerId]->writeAddressToFrameImpl(task, slot, val, false);
    }
  }

  struct TaskInstance{
  	FuncTy fn;
  	Ty args;
  	bool valid{false};
  };
  
  inline std::pair<FuncTy, Ty> stealTask(bool local, bool &valid) {
    valid = false;
    std::pair<FuncTy, Ty> ret;
    waitQueueMutex.lock();
    if(!readyQueue.isEmpty()){
      if (local) {
        Task* frameId = readyQueue.pop_back();
        if(frameId != nullptr){
        	ret = std::make_pair(frameId->funcType, frameId->args);
	        pool.free(frameId);
	        valid = true;
        }
      } else {
        Task* frameId = readyQueue.pop_front();
        if(frameId != nullptr){
        	ret = std::make_pair(frameId->funcType, frameId->args);
        	pool.free(frameId);
        	valid = true;
        }
      }
    }
    waitQueueMutex.unlock();
    return ret;
  }

  void inline workerLoop() {
    while (true) {
      std::pair<FuncTy, Ty> t;
      bool valid;
      // try to pop from my readyQueue first
      t = stealTask(true, valid);
      if (!valid) {
        for (int i = 0; i < workers.size(); i++) {
          if (i == workerId)
            continue;
          t = workers[i]->stealTask(false, valid);
          if (valid) {
            break;
          }
        }
      }

      if (!valid) {
        if (global_idle_count.fetch_add(1, std::memory_order_release) >= 0) {
            sem.acquire();
        }
        global_idle_count.fetch_sub(1, std::memory_order_release);
        continue;
      }
      invoke(t.first, t.second.left, t.second.address, t.second.right, t.second.slot);
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
