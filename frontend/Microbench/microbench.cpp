//
// Created by ruihong on 9/27/23.
//
// Copyright (c) 2018 The GAM Authors


#include <thread>
#include <atomic>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <mutex>
#include <set>
#include <random>


#include "zipf.h"
#include "PerfEvent.hpp"
#include "scalestore/Config.hpp"
#include "scalestore/ScaleStore.hpp"
#include "scalestore/rdma/CommunicationManager.hpp"
#include "scalestore/storage/datastructures/BTree.hpp"
#include "scalestore/utils/RandomGenerator.hpp"
#include "scalestore/utils/ScrambledZipfGenerator.hpp"
#include "scalestore/utils/Time.hpp"
#include "Memcached.h"

//#define PERF_GET
//#define PERF_MALLOC

//#define BENCHMARK_DEBUG
//#define STATS_COLLECTION
//#define LOCAL_MEMORY

//TODO: shall be adjusted according to the no_thread and
//#define NUMOFBLOCKS (2516582ull) //around 48GB totally, local cache is 8GB per node. (25165824ull)
//#define SYNC_KEY NUMOFBLOCKS
#define CMU_ZIPF
#define FULLY_SHARED_WITHIN_NODE
//2516582ull =  48*1024*1024*1024/(2*1024)
#define MEMSET_GRANULARITY (64*1024)
//#define EXCLUSIVE_HOTSPOT // exclusive hotspot per compute. workload == 1.

uint64_t NUMOFBLOCKS = 0;
uint64_t SYNC_KEY = 0;
uint64_t cache_size = 0;
int Memcache_offset = 1024;

uint64_t STEPS = 0;


using namespace scalestore;
using namespace storage;
uint16_t node_id;

//bool is_master = false;
uint16_t tcp_port=19843;
//string ip_master = get_local_ip("eth0");
//string ip_worker = get_local_ip("eth0");
//int port_master = 12345;
//int port_worker = 12346;

const char* result_file = "result.csv";

//exp parameters
// Cache can hold 4Million cache entries. Considering the random filling mechanism,
// if we want to gurantee that the cache has been filled, we need to run 8Million iterations (2 times). space locality use 16384000
//long ITERATION_TOTAL = 8192000;
long ITERATION_TOTAL = 16384000;

long ITERATION = 0;
DEFINE_uint32(read_ratio, 100, "");
DEFINE_bool(all_workloads, false , "Execute all workloads i.e. 50 95 100 ReadRatio on same tree");
DEFINE_int32(zip_workload, 0, "/0: random; 1: zipfian 2: multi-hotspot");
DEFINE_uint64(allocated_mem_size, 48, "Remote memory usage in GB");
DEFINE_int32(space_locality, 0, "space locality 0~100");
DEFINE_int32(shared_ratio, 100, "shared_ratio 0~100");
DEFINE_double(zipfian_param, 0.99, "Default value according to spec");
//DEFINE_double(YCSB_run_for_seconds, 10.0, "");
////long FENCE_PERIOD = 1000;
//int no_thread = 2;
////int remote_ratio = 0;  //0..100
//int shared_ratio = 10;  //0..100
//int space_locality = 10;  //0..100
//int time_locality = 10;  //0..100 (how probable it is to re-visit the current position)
//int read_ratio = 10;  //0..100
int op_type = 0;  //0: read/write; 1: rlock/wlock; 2: rlock+read/wlock+write
//int workload = 0;  //0: random; 1: zipfian 2: multi-hotspot
//double zipfian_param = 1;
////int total_spot_num = 0; // used when workload == 2
//
//int compute_num = 0;
//int memory_num = 100;

float cache_th = 0.15;  //0.15
//uint64_t cache_size = 0;

//runtime statistics
std::atomic<long> remote_access(0);
std::atomic<long> shared_access(0);
std::atomic<long> space_local_access(0);
std::atomic<long> time_local_access(0);
std::atomic<long> read_access(0);

std::atomic<long> total_throughput(0);
std::atomic<long> avg_latency(0);

bool reset = false;

std::set<PID> gen_accesses;
std::set<PID> real_accesses;
std::mutex stat_lock;

constexpr int addr_size = sizeof(PID);
constexpr int item_size = addr_size;
int items_per_block =  storage::PAGE_SIZE / item_size;
std::atomic<int> thread_sync_counter(0);

__inline__ unsigned long long rdtsc(void) {
    unsigned hi, lo;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}


//extern uint64_t cache_invalidation[MAX_APP_THREAD];
//extern uint64_t cache_hit_valid[MAX_APP_THREAD][8];
#ifdef GETANALYSIS
extern std::atomic<uint64_t> PrereadTotal;
extern std::atomic<uint64_t> Prereadcounter;
extern std::atomic<uint64_t> PostreadTotal;
extern std::atomic<uint64_t> Postreadcounter;
extern std::atomic<uint64_t> MemcopyTotal;
extern std::atomic<uint64_t> Memcopycounter;
extern std::atomic<uint64_t> NextStepTotal;
extern std::atomic<uint64_t> NextStepcounter;
extern std::atomic<uint64_t> WholeopTotal;
extern std::atomic<uint64_t> Wholeopcounter;
#endif
class WorkloadGenerator {
public:
    WorkloadGenerator() = default;
    virtual ~WorkloadGenerator() = default;
    virtual int getValue(){
        return 0;
    }
};

class ZipfianDistributionGenerator: public WorkloadGenerator {
private:
    uint64_t array_size;
    double skewness;
    std::vector<double> probabilities;
//    std::vector<int> zipfian_values;
    std::default_random_engine generator;
    std::discrete_distribution<int>* distribution;

public:
    ZipfianDistributionGenerator(uint64_t size, double s, unsigned int seed, uint8_t rank_of_spot = 0,
                                 uint8_t total_num_of_spot = 1)
            : array_size(size), skewness(s), probabilities(size), generator(seed) {
        probabilities.resize(array_size);
        uint64_t spot_interval = array_size/total_num_of_spot;
        uint64_t spot_offset = rank_of_spot*spot_interval;
        uint64_t overflow_offset = 0;
        for(uint64_t i = 0; i < array_size; ++i) {
            overflow_offset = (i+spot_offset)%array_size;
            probabilities[overflow_offset] = 1.0 / (pow(i+1, skewness));
//            zipfian_values[i] = i;
        }
        double smallest_probability = 1.0 / (pow(array_size, skewness));
// Convert smallest_probability to a string
        char buffer[50];
        snprintf(buffer, sizeof(buffer), "%.15f", smallest_probability);

        // Print the smallest_probability
        printf("Smallest Probability: %s\n", buffer);
        distribution = new std::discrete_distribution<int>(probabilities.begin(), probabilities.end());
//        std::shuffle(zipfian_values.begin(), zipfian_values.end(), generator);
    }

    int getValue() override {
//        return zipfian_values[distribution(generator)];
        return (*distribution)(generator);
    }
};

class MultiHotSpotGenerator: public WorkloadGenerator {
private:
    uint64_t array_size;
    int spot_num_;
    double skewness;
    std::vector<double> probabilities;
//    std::vector<int> zipfian_values;
    std::default_random_engine generator;
    std::discrete_distribution<int>* distribution;

public:
    MultiHotSpotGenerator(uint64_t size, double s, unsigned int seed, int spot_num) : array_size(size), spot_num_(spot_num),
                                                                                      skewness(s), probabilities(size), generator(seed) {
        probabilities.resize(array_size, 0);
        uint64_t spot_interval = array_size/spot_num_;
        uint64_t spot_offset = 0;
        uint64_t overflow_offset = 0;
        for (int j = 0; j < spot_num; ++j) {
            spot_offset = spot_interval*j;
            for(uint64_t i = 0; i < array_size; ++i) {
                overflow_offset = (i+spot_offset)%array_size;
                probabilities[overflow_offset] = probabilities[overflow_offset] + 1.0 / (pow(i+1, skewness));
//            zipfian_values[i] = i;
            }
        }

        double smallest_probability = 1.0 / (pow(array_size, skewness));
// Convert smallest_probability to a string
        char buffer[50];
        snprintf(buffer, sizeof(buffer), "%.15f", smallest_probability);

        // Print the smallest_probability
        printf("Smallest Probability: %s\n", buffer);
        distribution = new std::discrete_distribution<int>(probabilities.begin(), probabilities.end());
//        std::shuffle(zipfian_values.begin(), zipfian_values.end(), generator);
    }

    int getValue() override {
//        return zipfian_values[distribution(generator)];
        return (*distribution)(generator);
    }
};

inline int GetRandom(int min, int max, unsigned int* seedp) {
    int ret = (rand_r(seedp) % (max - min)) + min;
    return ret;
}
struct timespec init_time;
void init() __attribute__ ((constructor));
void fini() __attribute__ ((destructor));
void init() {
    clock_gettime(CLOCK_REALTIME, &init_time);
}
long get_time() {
//	struct timeval start;
//	gettimeofday(&start, NULL);
//	return start.tv_sec*1000l*1000+start.tv_usec;
    struct timespec start;
    clock_gettime(CLOCK_REALTIME, &start);
    return (start.tv_sec - init_time.tv_sec) * 1000l * 1000 * 1000
           + (start.tv_nsec - init_time.tv_nsec);;
}
bool TrueOrFalse(double probability, unsigned int* seedp) {
    return (rand_r(seedp) % 100) < probability;
}



//int GetRandom(int min, int max, unsigned int* seedp) {
//	int ret = (rand_r(seedp) % (max-min)) + min;
//	return ret;
//}

int CyclingIncr(int a, int cycle_size) {
    return ++a == cycle_size ? 0 : a;
}

double Revise(double orig, int remaining, bool positive) {
    if (positive) {  //false positive
        return (remaining * orig - 1) / remaining;
    } else {  //false negative
        return (remaining * orig + 1) / remaining;
    }
}

PID GADD(PID addr, uint64_t offset) {
    return PID(addr.getOwner(), addr.plainPID() + offset);
}
PID AlignToPage(PID addr) {
    return PID(addr.getOwner(), addr.plainPID() & ~(storage::PAGE_SIZE - 1));
}

volatile bool data_array_is_ready = false;
static constexpr uint64_t BARRIER_ID = 1;
void Init(Memcached* memcached, PID data[], PID access[], bool shared[], int id,
          unsigned int* seedp) {
    printf( "start init\n");
//    int l_space_locality = FLAGS_space_locality;
    int l_shared_ratio = FLAGS_shared_ratio;
    PID memset_buffer[MEMSET_GRANULARITY];
    PID* memget_buffer = nullptr;
//    int current_get_block = -1;
    if (node_id == 0 && id == 0) {
        for (uint64_t i = 0; i < STEPS; i++) {
            //There is no meanings to do the if clause below.
            if (TrueOrFalse(l_shared_ratio, seedp)) {
                shared[i] = true;
            } else {
                shared[i] = false;
            }
            ExclusiveBFGuard xg_leaf;
            data[i] = xg_leaf.getFrame().pid;
//            data[i] = ddsm->Allocate_Remote(Regular_Page);

            //Register the allocation for master into a key value store.
            if (i%MEMSET_GRANULARITY == MEMSET_GRANULARITY - 1) {
                memset_buffer[i%MEMSET_GRANULARITY] = data[i];
                printf("Memset a key %lu\n", i);
                memcached->memSet((const char*)&i, sizeof(i), (const char*)memset_buffer, sizeof(PID) * MEMSET_GRANULARITY);
//                    assert(i%MEMSET_GRANULARITY == MEMSET_GRANULARITY-1);
            }else{
                memset_buffer[i%MEMSET_GRANULARITY] = data[i];
//                assert(data[i].offset <= 64ull*1024ull*1024*1024);

            }
            if (i == STEPS - 1) {
                printf("Memset a key %lu\n", i);
                memcached->memSet((const char*)&i, sizeof(i), (const char*)memset_buffer, sizeof(PID) * MEMSET_GRANULARITY);
            }
        }
        data_array_is_ready = true;
    } else {
        if (node_id!=0 && id == 0){
            for (uint64_t i = 0; i < STEPS; i++) {
                if(FLAGS_shared_ratio > 0 && i == (STEPS/MEMSET_GRANULARITY)*MEMSET_GRANULARITY){
                    if (memget_buffer){
                        delete memget_buffer;
                    }
                    size_t v_size;
                    int key =  STEPS - 1;
                    memget_buffer = (PID*)memcached->memGet((const char*)&key, sizeof(key),  &v_size);
                    assert(v_size == sizeof(PID) * MEMSET_GRANULARITY);
                }else if ((FLAGS_shared_ratio > 0 && i % MEMSET_GRANULARITY == 0 )) {
                    if (memget_buffer){
                        delete memget_buffer;
                    }
                    size_t v_size;
                    int key =  i + MEMSET_GRANULARITY - 1;
                    memget_buffer = (PID*)memcached->memGet((const char*)&key, sizeof(key),  &v_size);
                    assert(v_size == sizeof(PID) * MEMSET_GRANULARITY);
                }

                //we prioritize the shared ratio over other parameters
                if (i < STEPS*l_shared_ratio/100) {
//                    PID addr;
//                    size_t v_size;

                    data[i] = memget_buffer[i%MEMSET_GRANULARITY];
//                    assert(data[i].offset <= 64ull*1024ull*1024*1024);
                    //revise the l_remote_ratio accordingly if we get the shared addr violate the remote probability
                    shared[i] = true;
                } else {
                    ExclusiveBFGuard xg_leaf;
                    data[i] = xg_leaf.getFrame().pid;
//                    data[i] = ddsm->Allocate_Remote(Regular_Page);
//                    assert(data[i].offset <= 64ull*1024ull*1024*1024);



                    shared[i] = false;
                }
            }
            data_array_is_ready = true;
        }else{
            while (!data_array_is_ready){};
        }

    }
    //access[0] = data[0];
    access[0] = data[GetRandom(0, STEPS, seedp)];
#ifdef STATS_COLLECTION
    stat_lock.lock();
  gen_accesses.insert(TOBLOCK(access[0]));
  stat_lock.unlock();
#endif

    struct zipf_gen_state state;

    WorkloadGenerator* workload_gen = nullptr;

    if (FLAGS_zip_workload == 1){
#ifdef EXCLUSIVE_HOTSPOT
        workload_gen = new ZipfianDistributionGenerator(STEPS, zipfian_param, *seedp, ddsm->GetID()/2, compute_num);
#else

#ifdef CMU_ZIPF
        mehcached_zipf_init(&state, STEPS, FLAGS_zipfian_param,
                            (rdtsc() & (0x0000ffffffffffffull)) ^ id);
#else
        workload_gen = new ZipfianDistributionGenerator(STEPS, zipfian_param, *seedp);
#endif
#endif
    } else if (FLAGS_zip_workload > 1){
        workload_gen = new MultiHotSpotGenerator(STEPS, FLAGS_zipfian_param, *seedp, FLAGS_zip_workload);
    }


    // Access is the address of future acesses.
    for (int i = 1; i < 2*ITERATION; i++) {
        //PopulateOneBlock(alloc, data, ldata, i, l_remote_ratio, l_space_locality, seedp);
        PID next;
        if (TrueOrFalse(FLAGS_space_locality, seedp)) {
            next = access[i - 1];
            next = GADD(next, GetRandom(0, items_per_block, seedp) * item_size);
            next = GADD(next,item_size);
            if (AlignToPage(next) != AlignToPage(access[i - 1])) {
                next = AlignToPage(access[i - 1]);
            }
        } else {
            if (FLAGS_zip_workload == 0){
                PID n = data[GetRandom(0, STEPS, seedp)];
//                while (TOPAGE(n) == TOPAGE(access[i - 1])) {
//                    n = data[GetRandom(0, STEPS, seedp)];
//                }
                next = GADD(n, GetRandom(0, items_per_block, seedp) * item_size);
            } else if (FLAGS_zip_workload > 0){
#ifdef CMU_ZIPF
                uint64_t pos = mehcached_zipf_next(&state);

#else
                uint64_t pos = workload_gen->getValue();

#endif
                PID n = data[pos];
//                while (TOPAGE(n) == TOPAGE(access[i - 1])) {
//                    pos = workload_gen->getValue();
//                    n = data[pos];
//                }
                next = GADD(n, GetRandom(0, items_per_block, seedp) * item_size);
            }


//            PID n = data[GetRandom(0, STEPS, seedp)];
//            while (n == access[i - 1]) {
//                n = data[GetRandom(0, STEPS, seedp)];
//            }
//            next = n;
        }
        access[i] = next;
#ifdef STATS_COLLECTION
        stat_lock.lock();
    gen_accesses.insert(TOBLOCK(next));
    stat_lock.unlock();
#endif
    }
    printf("end init\n");
    if (workload_gen){
        delete workload_gen;
    }
}

bool Equal(char buf1[], char buf2[], int size) {
    int i;
    for (i = 0; i < size; i++) {
        if (buf1[i] != buf2[i]) {
            break;
        }
    }
    return i == size ? true : false;
}

void Run(PID access[], int id, unsigned int *seedp, bool warmup, uint32_t read_ratio) {

    PID to_access = access[0];  //access starting point
    char buf[item_size];
//    int ret;
//    int j = 0;
//	int writes = 0;
//	GAddr fence_addr = alloc->Malloc(1);
//	epicAssert(fence_addr);
    long start = get_time();
    for (int i = 0; i < ITERATION; i++) {
//		if(writes == FENCE_PERIOD) {
//			alloc->MFence();
//			char c;
//			ret = alloc->Read(fence_addr, &c, 1);
//			epicAssert(ret == 1);
//			writes = 0;
//		}

        switch (op_type) {
            case 0:  //blind write no need to read before write.
                if (TrueOrFalse(read_ratio, seedp)) {
                    PID target_cache_line = AlignToPage(to_access);
                    SharedBFGuard guard(target_cache_line);
                    memcpy(buf, (char*)guard.getFrame().page->begin() + (to_access.plainPID() - target_cache_line.plainPID()), item_size);

//                    memcpy(buf, (char*)page_buffer + (to_access.offset - target_cache_line.offset), item_size);
//                    alloc->SELCC_Shared_UnLock(target_cache_line, handle);

                } else {
//                    void* page_buffer;

                    memset(buf, i, item_size);
                    PID target_cache_line = AlignToPage(to_access);
                    ExclusiveBFGuard guard(target_cache_line);
                    uint64_t cache_line_offset = to_access.plainPID() - target_cache_line.plainPID();
                    memcpy((char*)guard.getFrame().page->begin()  + (cache_line_offset), buf, item_size);
                }
                break;
            case 1:  //rlock/wlock
            {
                if (TrueOrFalse(read_ratio, seedp)) {
                    PID target_cache_line = AlignToPage(to_access);
                    SharedBFGuard guard(target_cache_line);
                    memcpy(buf, (char*)guard.getFrame().page->begin() + (to_access.plainPID() - target_cache_line.plainPID()), item_size);
                } else {
                    memset(buf, i, item_size);
                    PID target_cache_line = AlignToPage(to_access);
                    ExclusiveBFGuard guard(target_cache_line);
                    uint64_t cache_line_offset = to_access.plainPID() - target_cache_line.plainPID();
                    memcpy((char*)guard.getFrame().page->begin()  + (cache_line_offset), buf, item_size);
                }
                break;
            }
            case 2:  //rlock+read/wlock+write Is this GAM PSO
            {
                if (TrueOrFalse(read_ratio, seedp)) {
                    PID target_cache_line = AlignToPage(to_access);
                    SharedBFGuard guard(target_cache_line);
                    memcpy(buf, (char*)guard.getFrame().page->begin() + (to_access.plainPID() - target_cache_line.plainPID()), item_size);
                } else {
                    memset(buf, i, item_size);
                    PID target_cache_line = AlignToPage(to_access);
                    ExclusiveBFGuard guard(target_cache_line);
                    uint64_t cache_line_offset = to_access.plainPID() - target_cache_line.plainPID();
                    memcpy((char*)guard.getFrame().page->begin()  + (cache_line_offset), buf, item_size);
                }
                break;
            }
            case 3:  //try_rlock/try_wlock
            {
                if (TrueOrFalse(read_ratio, seedp)) {
                    PID target_cache_line = AlignToPage(to_access);
                    SharedBFGuard guard(target_cache_line);
                    memcpy(buf, (char*)guard.getFrame().page->begin() + (to_access.plainPID() - target_cache_line.plainPID()), item_size);
                } else {
                    memset(buf, i, item_size);
                    PID target_cache_line = AlignToPage(to_access);
                    ExclusiveBFGuard guard(target_cache_line);
                    uint64_t cache_line_offset = to_access.plainPID() - target_cache_line.plainPID();
                    memcpy((char*)guard.getFrame().page->begin()  + (cache_line_offset), buf, item_size);
                }
                break;
            }
            default:
                printf( "unknown op type\n");
                break;
        }
        //time locality
//        if (TrueOrFalse(time_locality, seedp)) {
//            //we keep to access the same addr
//            //epicLog(LOG_DEBUG, "keep to access the current location");
//        } else {
//            j++;
//            if (j == ITERATION) {
//                j = 0;
//                assert(i == ITERATION - 1);
//            }
//            to_access = access[j];
//            //epicAssert(buf == to_access || addr_to_pos.count(buf) == 0);
//        }
        if (i%10000 == 0 && id == 0){
            printf("Node %d finish %d ops \n", node_id, i);
            fflush(stdout);
        }
    }
    long end = get_time();
    long throughput = ITERATION / ((double) (end - start) / 1000 / 1000 / 1000);
    long latency = (end - start) / ITERATION;
    printf(
            "node_id %d, thread %d, average throughput = %ld per-second, latency = %ld ns %s\n",
            node_id, id, throughput, latency, warmup ? "(warmup)" : "");
    fflush(stdout);
    if (!warmup) {
        total_throughput.fetch_add(throughput);
        avg_latency.fetch_add(latency);
    }
}

void Benchmark(int id, ScaleStore *alloc, PID *data, Memcached *memcached, uint32_t read_ratio) {

    unsigned int seedp = FLAGS_worker * alloc->getNodeID() + id;
    printf("seedp = %d\n", seedp);
//    bindCore(id);


    std::unordered_map<uint64_t , int> addr_to_pos;

    // gernerate 2*Iteration access target, half for warm up half for the real test
    static PID* access = nullptr;
    static bool* shared = nullptr;
    if (!access){
        assert(shared == nullptr);
        access = (PID*) malloc(sizeof(PID) * 2*ITERATION);
        shared = (bool*) malloc(sizeof(bool) * STEPS);
        Init(memcached, data, access, shared, id, &seedp);
    }


    auto& catalog = alloc->getCatalog();
    printf("start warmup the benchmark on thread %d", id);
    storage::DistributedBarrier barrier(catalog.getCatalogEntry(BARRIER_ID).pid);
    barrier.wait();
    bool warmup = true;
    Run(access, id, &seedp, warmup, read_ratio);
    barrier.wait();

    warmup = false;


    printf( "start run the benchmark on thread %d\n", id);
    Run(&access[ITERATION], id, &seedp, warmup, read_ratio);
    barrier.wait();
//#ifndef LOCAL_MEMORY
//    //make sure all the requests are complete
//    alloc->MFence();
//    alloc->WLock(data[0], BLOCK_SIZE);
//    alloc->UnLock(data[0], BLOCK_SIZE);
//#endif
}

int main(int argc, char* argv[]) {
    gflags::SetUsageMessage("Catalog Test");
    gflags::ParseCommandLineFlags(&argc, &argv, true);


#ifdef LOCAL_MEMORY
    int memory_type = 0;  //"local memory";
#else
    int memory_type = 1;  //"global memory";
#endif
    printf("Currently configuration is: ");
//    printf(  is_master == 1 ? "true" : "false", no_thread, compute_num);
    printf(
            "compute_num = %lu, no_thread = %lu, shared_ratio: %d, read_ratio: %d, "
            "space_locality: %d, op_type = %s, memory_type = %s, item_size = %d, cache_th = %f, result_file = %s\n",
            FLAGS_nodes/2,
            FLAGS_worker,
//      remote_ratio,
            FLAGS_shared_ratio,
            FLAGS_read_ratio,
            FLAGS_space_locality,
            op_type == 0 ?
            "read/write" :
            (op_type == 1 ?
             "rlock/wlock" :
             (op_type == 2 ? "rlock+read/wlock+write" : "try_rlock/try_wlock")),
            memory_type == 0 ? "local memory" : "global memory", item_size, cache_th,
            result_file);

    ScaleStore ddsm;
    Memcached memcached;
//    compute_num = ddsm.rdma_mg->GetComputeNodeNum();
//    memory_num = ddsm.rdma_mg->GetMemoryNodeNum();
    NUMOFBLOCKS = FLAGS_allocated_mem_size/(storage::PAGE_SIZE);
    printf("number of blocks is %lu\n", NUMOFBLOCKS);
    SYNC_KEY = NUMOFBLOCKS;
#ifdef FULLY_SHARED_WITHIN_NODE
    STEPS = NUMOFBLOCKS;
#else
    STEPS = NUMOFBLOCKS/((no_thread - 1)*(100-shared_ratio)/100.00L + 1);
#endif
//    STEPS = NUMOFBLOCKS/((no_thread*compute_num - 1)*(100-shared_ratio)/100.00L + 1);

    printf("number of steps is %lu\n", STEPS);
    printf("workload is %d, zipfian_alpha is %f", FLAGS_zip_workload, FLAGS_zipfian_param);
    ITERATION = ITERATION_TOTAL/FLAGS_worker;
    sleep(1);
    //sync with all the other workers
    //check all the workers are started
//    int id;
    node_id = ddsm.getNodeID();
    printf("This node id is %d\n", node_id);
    if (node_id == 0){
        ddsm.getWorkerPool().scheduleJobSync(0, [&]() {
            ddsm.createBarrier(FLAGS_worker * FLAGS_nodes/2);

        });

    }
    std::vector<uint32_t> workloads;
    if(FLAGS_all_workloads){
//      workloads.push_back(5);
        workloads.push_back(0);
        workloads.push_back(50);
        workloads.push_back(95);
        workloads.push_back(100);
    }else{
        workloads.push_back(FLAGS_read_ratio);
    }
    PID *data = (PID*) malloc(sizeof(PID) * STEPS);

    for (uint64_t t_i = 0; t_i < FLAGS_worker; ++t_i) {
        for (auto read_ratio: workloads){
            ddsm.getWorkerPool().scheduleJobAsync(t_i, [&, t_i](){
                // barrier inside
                Benchmark(t_i, &ddsm, data, &memcached, read_ratio);

            });
        }
        ddsm.getWorkerPool().joinAll();
        long t_thr = total_throughput;
        long a_thr = total_throughput;
        a_thr /= FLAGS_worker;
        long a_lat = avg_latency;
        a_lat /= FLAGS_worker;
        uint64_t invalidation_num = 0;
        uint64_t hit_valid_num = 0;
        printf(
                "results for  node_id %d: workload: %d, zipfian_alpha: %f total_throughput: %ld, avg_throuhgput:%ld, avg_latency:%ld， operation need cache invalidation %lu, operation cache hit and valid is %lu,  total operation executed %ld\n\n",
                node_id, FLAGS_zip_workload, FLAGS_zipfian_param, t_thr, a_thr, a_lat, invalidation_num, hit_valid_num, ITERATION_TOTAL);

        //sync with all the other workers
        //check all the benchmark are completed
        unsigned long res[5];
        res[0] = t_thr;  //total throughput for the current node
        res[1] = a_thr;  //avg throuhgput for the current node
        res[2] = a_lat;  //avg latency for the current node
        res[3] = invalidation_num;  //avg invalidated message number
        res[4] = hit_valid_num;  //avg latency for the current node
        int temp = SYNC_KEY + Memcache_offset + node_id;
        printf("memset temp key %d\n", temp);
        memcached.memSet((char*)&temp, sizeof(int), (char*)res, sizeof(long) * 5);
        t_thr = a_thr = a_lat = invalidation_num = hit_valid_num = 0;
        for (uint64_t i = 0; i < FLAGS_nodes/2; i++) {
            memset(res, 0, sizeof(long) * 5);
            temp = SYNC_KEY + Memcache_offset + i * 2;
            size_t len;
            printf("memGet temp key %d\n", temp);
            long* ret = (long*)memcached.memGet((char*)&temp , sizeof(int), &len);
            assert(len == sizeof(long) * 5);
            t_thr += ret[0];
            a_thr += ret[1];
            a_lat += ret[2];
            invalidation_num += ret[3];
            hit_valid_num += ret[4];
        }
        a_thr /= (FLAGS_nodes/2);
        a_lat /= (FLAGS_nodes/2);
        invalidation_num /= (FLAGS_nodes/2);
        hit_valid_num /= (FLAGS_nodes/2);

        if (node_id == 0) {
            std::ofstream result;
            result.open(result_file, std::ios::app);
            result << (FLAGS_nodes/2) << "," << FLAGS_worker << ","  << ","
                   << FLAGS_shared_ratio << "," << FLAGS_read_ratio << "," << FLAGS_space_locality << ","
                   << op_type << "," << memory_type << ","
                   << item_size << "," << t_thr << "," << a_thr << "," << a_lat << ","
                   << cache_th << "\n";
            printf(
                    "results for all the nodes: "
                    "compute_num: %lu, workload: %d, zipfian_alpha: %f no_thread: %lu, shared_ratio: %d, read_ratio: %d, space_locality: %d, "
                    "op_type = %d, memory_type = %d, item_size = %d, "
                    "operation with cache invalidation message accounts for %f percents, average cache valid hit percents %f total_throughput: %ld, avg_throuhgput:%ld, avg_latency:%ld, \n\n",
                    (FLAGS_nodes/2), FLAGS_zip_workload, FLAGS_zipfian_param, FLAGS_worker, FLAGS_shared_ratio, FLAGS_read_ratio,
                    FLAGS_space_locality, op_type, memory_type, item_size, static_cast<double>(invalidation_num) / ITERATION_TOTAL, static_cast<double>(hit_valid_num) / ITERATION_TOTAL, t_thr,
                    a_thr, a_lat);
#ifdef GETANALYSIS
            if (Prereadcounter.load() != 0){
            printf("Preread average time elapse is %lu ns, Postread average time elapse is %lu ns, Memcopy average time elapse is %lu ns, "
                   "prepare next step is %lu ns, counter is %lu, whole ops average time elapse is %lu ns, PostreadCOunter is %lu\n",
                   PrereadTotal.load()/Prereadcounter.load(), PostreadTotal.load()/Postreadcounter.load(), MemcopyTotal.load()/Memcopycounter.load(),
                   NextStepTotal.load()/NextStepcounter.load(), NextStepcounter.load(), WholeopTotal/Wholeopcounter, Postreadcounter.load());
        }

#endif
            result.close();
        }
    }






#ifdef STATS_COLLECTION
    epicLog(LOG_WARNING, "shared_ratio = %lf, remote_ratio = %lf, read_ratio = %lf, space_locality = %lf, time_locality = %lf, "
      "total blocks touched %d, expected blocks touched %d\n",
      ((double)shared_access)/(ITERATION*no_thread)*100/2, ((double)remote_access)/(ITERATION*no_thread)*100/2,
      ((double)read_access)/(ITERATION*no_thread)*100/2,
      ((double)space_local_access)/(ITERATION*no_thread)*100/2, ((double)time_local_access)/(ITERATION*no_thread)*100/2,
      real_accesses.size(), gen_accesses.size());
#endif

//	long time = no_thread*compute_num*(double)(100-read_ratio)/100+1;
//	time /= 2;
//	if(time < 2) time += 1;
    long time = 1;
    printf( "sleep for %ld s\n\n", time);
    sleep(time);

    return 0;
}

