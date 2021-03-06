// @COPYRIGHT@
// Licensed under MIT license.
// See LICENSE.TXT file in the project root for more information.
// ==============================================================
#ifndef __SHADOW_MEMORY__
#define __SHADOW_MEMORY__
#include<stdint.h>
#include<atomic>
#include<stdlib.h>
#include<sys/mman.h>
#include<tuple>

// 64KB shadow pages
#define PAGE_OFFSET_BITS (16LL)
#define PAGE_OFFSET(addr) ( addr & 0xFFFF)
#define PAGE_OFFSET_MASK ( 0xFFFF)
#define SHADOW_PAGE_SIZE (1 << PAGE_OFFSET_BITS)

// 2 level page table
#define PTR_SIZE (sizeof(struct Status *))
#define LEVEL_1_PAGE_TABLE_BITS  (20)
#define LEVEL_1_PAGE_TABLE_ENTRIES  (1 << LEVEL_1_PAGE_TABLE_BITS )
#define LEVEL_1_PAGE_TABLE_SIZE  (LEVEL_1_PAGE_TABLE_ENTRIES * PTR_SIZE )

#define LEVEL_2_PAGE_TABLE_BITS  (12)
#define LEVEL_2_PAGE_TABLE_ENTRIES  (1 << LEVEL_2_PAGE_TABLE_BITS )
#define LEVEL_2_PAGE_TABLE_SIZE  (LEVEL_2_PAGE_TABLE_ENTRIES * PTR_SIZE )

#define LEVEL_1_PAGE_TABLE_SLOT(addr) ((((uint64_t)addr) >> (LEVEL_2_PAGE_TABLE_BITS + PAGE_OFFSET_BITS)) & 0xfffff)
#define LEVEL_2_PAGE_TABLE_SLOT(addr) ((((uint64_t)addr) >> (PAGE_OFFSET_BITS)) & 0xFFF)

#define SHADOW_STRUCT_SIZE (sizeof (T))
using namespace std;

/*
template <typename... Ts>
struct ShadowType {
    tuple<Ts[SHADOW_PAGE_SIZE]...> f;
    void * operator new(size_t sz) {
        void * p = mmap(0, sz, PROT_WRITE | PROT_READ, MAP_NORESERVE | MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
        if(p == MAP_FAILED) {
            perror("mmap l2pg");
            PIN_ExitProcess(-1);
        }
        return p;
    }
    void operator delete(void *p) {
        munmap(p, sizeof(ShadowType<Ts...>));
    }
};
*/

template<class ...Args>
using ShadowTuple = std::tuple<Args[SHADOW_PAGE_SIZE]...>;


#if 1
template <typename... Ts>
class ConcurrentShadowMemory {
    // All fwd declarations
    atomic< atomic< ShadowTuple<Ts...>  *> *> * pageDirectory;
    // Given a address generated by the program, returns the corresponding shadow address FLOORED to  SHADOW_PAGE_SIZE
    // If the shadow page does not exist a new one is MMAPed
public:
    inline ConcurrentShadowMemory() {
        pageDirectory = (atomic< atomic< ShadowTuple<Ts...> *> *> *) mmap(0, LEVEL_1_PAGE_TABLE_SIZE, PROT_WRITE | PROT_READ, MAP_NORESERVE | MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
        if(pageDirectory == MAP_FAILED) {
            perror("mmap pageDirectory");
            PIN_ExitProcess(-1);
        }
    }
    
    inline ~ConcurrentShadowMemory(){
        for(uint64_t i = 0; i < LEVEL_1_PAGE_TABLE_ENTRIES; i++) {
            atomic< ShadowTuple<Ts...> *> * l1Page;
            if( (l1Page=pageDirectory[i].load(memory_order_relaxed)) != 0) {
                for(uint64_t j = 0; j < LEVEL_2_PAGE_TABLE_ENTRIES; j++) {
                    ShadowTuple<Ts...> * l2Page;
                    if( (l2Page=l1Page[j].load(memory_order_relaxed)) != 0){
                        delete l2Page;
                    }
                }
                if(0 != munmap(l1Page, LEVEL_2_PAGE_TABLE_SIZE)) {
                    perror("munmap pageDirectory");
                    PIN_ExitProcess(-1);
                }
            }
        }
        if(0 != munmap(pageDirectory, LEVEL_1_PAGE_TABLE_SIZE)){
            perror("munmap pageDirectory");
            PIN_ExitProcess(-1);
        }
    }
    
    inline ShadowTuple<Ts...> & GetOrCreateShadowBaseAddress(const size_t address) {
        atomic< atomic< ShadowTuple<Ts...> *> *>  * l1Ptr = & pageDirectory[LEVEL_1_PAGE_TABLE_SLOT(address)];
        atomic< ShadowTuple<Ts...> *> * v1;
        if ( (v1=l1Ptr->load(memory_order_consume)) == 0) {
            atomic< ShadowTuple<Ts...>    *> * l1pg = (atomic<  ShadowTuple<Ts...> *> *) mmap(0, LEVEL_2_PAGE_TABLE_SIZE, PROT_WRITE | PROT_READ, MAP_NORESERVE | MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
            if(l1pg == MAP_FAILED) {
                perror("mmap l1pg");
                PIN_ExitProcess(-1);
            }
            
            atomic<ShadowTuple<Ts...> *> * nullVal = 0;
            if(!l1Ptr->compare_exchange_strong(nullVal, l1pg, memory_order_acq_rel, memory_order_relaxed)) {
                free(l1pg);
                v1 = l1Ptr->load(memory_order_consume);
            } else {
                v1 = l1pg;
            }
        }
        atomic<ShadowTuple<Ts...> *>  * l2Ptr = & v1[LEVEL_2_PAGE_TABLE_SLOT(address)];
        ShadowTuple<Ts...> * v2;
        if( (v2=l2Ptr->load(memory_order_consume)) == 0 ){
            ShadowTuple<Ts...> * l2pg = new ShadowTuple<Ts...>;
            if(l2pg == MAP_FAILED) {
                perror("mmap l2pg");
                PIN_ExitProcess(-1);
            }
            ShadowTuple<Ts...> * nullVal = 0;
            if( !l2Ptr->compare_exchange_strong(nullVal, l2pg, memory_order_acq_rel, memory_order_relaxed)){
                delete l2pg;
                v2 = l2Ptr->load(memory_order_consume);
            } else {
                v2 = l2pg;
            }
        }
        return (*v2);
    }
};



template<int I, typename... Ts>
    typename std::tuple_element<I, tuple<Ts...> >::type * GetOrCreateShadowAddress(ConcurrentShadowMemory<Ts...> & sm, const size_t address) {
 auto  shadowPage = sm.GetOrCreateShadowBaseAddress(address);
 return &(get<I>(shadowPage)[PAGE_OFFSET((uint64_t)address)]);
}
#endif

template <typename... Ts>
class ShadowMemory {
    // All fwd declarations
    ShadowTuple<Ts...> *** pageDirectory;
    // Given a address generated by the program, returns the corresponding shadow address FLOORED to  SHADOW_PAGE_SIZE
    // If the shadow page does not exist a new one is MMAPed
public:
    inline ShadowMemory() {
        pageDirectory = (ShadowTuple<Ts...> ***) mmap(0, LEVEL_1_PAGE_TABLE_SIZE, PROT_WRITE | PROT_READ, MAP_NORESERVE | MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
        if(pageDirectory == MAP_FAILED) {
            perror("mmap pageDirectory");
            PIN_ExitProcess(-1);
        }
    }
    
    inline ~ShadowMemory(){
        for(uint64_t i = 0; i < LEVEL_1_PAGE_TABLE_ENTRIES; i++) {
            ShadowTuple<Ts...> ** l1Page;
            if( (l1Page=pageDirectory[i]) != 0) {
                for(uint64_t j = 0; j < LEVEL_2_PAGE_TABLE_ENTRIES; j++) {
                    ShadowTuple<Ts...> * l2Page;
                    if( (l2Page=l1Page[j]) != 0){
                        delete l2Page;
                    }
                }
                if(0 != munmap(l1Page, LEVEL_2_PAGE_TABLE_SIZE)) {
                    perror("munmap pageDirectory");
                    PIN_ExitProcess(-1);
                }
            }
        }
        if(0 != munmap(pageDirectory, LEVEL_1_PAGE_TABLE_SIZE)){
            perror("munmap pageDirectory");
            PIN_ExitProcess(-1);
        }
    }
    
    inline ShadowTuple<Ts...> & GetOrCreateShadowBaseAddress(const size_t address) {
        ShadowTuple<Ts...> *** l1Ptr = & pageDirectory[LEVEL_1_PAGE_TABLE_SLOT(address)];
        ShadowTuple<Ts...> ** v1;
        if ( (v1=*l1Ptr) == 0) {
            ShadowTuple<Ts...> ** l1pg = (ShadowTuple<Ts...> **) mmap(0, LEVEL_2_PAGE_TABLE_SIZE, PROT_WRITE | PROT_READ, MAP_NORESERVE | MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
            if(l1pg == MAP_FAILED) {
                perror("mmap l1pg");
                PIN_ExitProcess(-1);
            }
            v1 = l1pg;
        }
        ShadowTuple<Ts...> ** l2Ptr = & v1[LEVEL_2_PAGE_TABLE_SLOT(address)];
        ShadowTuple<Ts...> * v2;
        if( (v2=*l2Ptr) == 0 ){
            ShadowTuple<Ts...> * l2pg = new ShadowTuple<Ts...>;
            if(l2pg == MAP_FAILED) {
                perror("mmap l2pg");
                PIN_ExitProcess(-1);
            }
            v2 = l2pg;
        }
        return v2;
    }
};

template<int I, typename... Ts>
typename std::tuple_element<I, tuple<Ts...> >::type * GetOrCreateShadowAddress(ShadowMemory<Ts...> & sm, const size_t address) {
    auto  shadowPage = sm.GetOrCreateShadowBaseAddress(address);
    return &(get<I>(shadowPage)[PAGE_OFFSET((uint64_t)address)]);
}
//#define ConcurrentShadowMemory ShadowMemory

#if 0
ShadowMemory<int> s;
ConcurrentShadowMemory <int> cs;
int main(){
    
    GetOrCreateShadowAddress<0>(s, 0x12345678)[0] = 1234;
    int j = GetOrCreateShadowAddress<0>(s, 0x12345678)[0];

    GetOrCreateShadowAddress<0>(cs, 0x12345678)[0] = 1234;
    j = GetOrCreateShadowAddress<0>(cs, 0x12345678)[0];

    return 0;
}
#endif

#endif // __SHADOW_MEMORY__
