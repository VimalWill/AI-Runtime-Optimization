// ================= worker.h =================
#ifndef Worker_H
#define Worker_H
#include <atomic>
#include <functional>
#include <vector>
#include <thread>
#include <condition_variable>
#include <deque>
#include <map>
#include <llvm/ADT/SmallVector.h>
#include <emmintrin.h>

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

    void unlock() {
        flag.clear(std::memory_order_release);
    }
};


template <typename Ty, typename FuncTy>
struct alignas(64) Worker {
    __attribute__((preserve_none)) void invoke(FuncTy funcType, Ty args);
    int workerId;
     typedef struct alignas(64) Task{
	FuncTy funcType;
  	Ty args;
  	int workerId;
  	std::atomic<int32_t> remainingInputs;
  	
  	char pad[64 - sizeof(funcType) - sizeof(args) - sizeof(std::atomic<int32_t>)];
    }Task;
    
    std::deque<Task*> readyQueue;   
    char pad0[64 - (sizeof(int) % 64)];
    std::thread thread;
    SpinLock waitQueueMutex;
    std::condition_variable cv;
    
    llvm::SmallVector<Worker<Ty, FuncTy>*> workers;
    
    Worker<Ty, FuncTy>(int workerId){
    	this->workerId = workerId;
    }
    
    void inline setWorkers(llvm::SmallVector<Worker<Ty, FuncTy>*>& workers){
    	this->workers = workers;
    }

    void inline join() {
        if (thread.joinable())
            thread.join();
    }
  
  inline Task* createNewFrame(FuncTy fn, int numInputs){
	Task* newTask = new Task(fn, Ty{}, workerId);
	newTask->remainingInputs.store(numInputs, std::memory_order_relaxed);
  	return newTask;
  }

  void inline createNewFrameAndWriteArgs(FuncTy fn, Ty args, bool launch = false){
  	if(launch){
  		invoke(fn, args);
  		cv.notify_all();
  	}else{
		waitQueueMutex.lock();
		readyQueue.push_back(new Task(fn, args, workerId));
       		waitQueueMutex.unlock();
       	}
  }
  
  /*void deleteFrame(int32_t frameId){
 	while(!taskMutex.try_lock()){
	}
 	taskFunctions[frameId] = nullptr;
 	taskMutex.unlock();
	while(!readyQueueMutex.try_lock()){
   	}
   	int i = -1;
   	for(i = 0; i<readyQueue.size();i++){
		if(readyQueue[i] == frameId)
		break;
	}
	if(i < readyQueue.size() && i >=0)
		readyQueue.erase(readyQueue.begin() + i);
  }*/

  void inline writeDataToFrameImpl(Task* task, int slot, int val){
        task->args.setValue(slot, val);
	if(task->remainingInputs.fetch_sub(1, std::memory_order_relaxed) == 1){
	       	waitQueueMutex.lock();
		readyQueue.push_back(task);
	       	waitQueueMutex.unlock();
 	}
  }
  
  void inline writeAddressToFrameImpl(Task* task, int slot, Task* val){
        task->args.setAddress(slot, val);
	if(task->remainingInputs.fetch_sub(1, std::memory_order_relaxed) == 1){
	       	waitQueueMutex.lock();
		readyQueue.push_back(task);
	       	waitQueueMutex.unlock();
 	}
  }
  
 void inline writeDataToFrame(Task* task, int slot, int val, bool local){
  	if(local){
  	 	writeDataToFrameImpl(task,  slot, val);
  	}else{
		workers[task->workerId]->writeDataToFrameImpl(task, slot, val);
  	}
  }
  
  void inline writeAddressToFrame(Task* task, int slot, Task* val, bool local){
    	if(local){
    	        writeAddressToFrameImpl(task,  slot, val);
  	}else{
		workers[task->workerId]->writeAddressToFrameImpl(task,  slot, val);
  	}
  }
  
  inline Task* stealTask(bool local){
    Task* frameId = nullptr;
    waitQueueMutex.lock();
    if (!readyQueue.empty()) {
    	if(local){
       		frameId = readyQueue.back();
	       	readyQueue.pop_back();
        }else{
        	frameId = readyQueue.front();
	       	readyQueue.pop_front();       		
        }
     }
    waitQueueMutex.unlock();
    return frameId;
  }
  
  void inline workerLoop(){
      while(true){
	Task* t;
        // try to pop from my readyQueue first
        t = stealTask(true);
        if(t == nullptr) {
            for (int i = 0; i < workers.size(); i++) {
            	if(i == workerId)
            		continue;
                t = workers[i]->stealTask(false);
                if (t != nullptr){
        	        break;
                }
            }
         }

        if (t == nullptr) {
            // no work anywhere
            //std::this_thread::yield();
            continue;
        }

        (invoke)(t->funcType, t->args);
         workers[t->workerId]->cv.notify_one();
         delete t;
     }
  }
  
  void start() {
        thread = std::thread(&Worker::workerLoop, this);
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(workerId, &cpuset);

       int r = pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t), &cpuset);
       if (r != 0) {
         perror("pthread_setaffinity_np");
       }
  }

};

#endif
