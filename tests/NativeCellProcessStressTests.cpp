// SPACE-05C native worker endurance and resource-contract acceptance.
// OS processes are constrained; no child gets the original game database.
#include "galaxy/NativeCellProcessRunner.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy::spatial;
#define CHECK(X) do {if(!(X))throw std::runtime_error( #X );}while(false)
int main(int argc,char** argv){
    try{
        CHECK(argc==3);
        const std::string binary(argv[1]),hung(argv[2]);
        constexpr int workers=4, rounds=20;
        std::vector<double> elapsed_ms;
        elapsed_ms.reserve(workers*rounds);
        for(int round=0;round<rounds;++round){
            std::vector<std::future<double>> pending;
            for(int id=0;id<workers;++id){
                pending.push_back(std::async(std::launch::async,[&,round,id](){
                    const auto start=std::chrono::steady_clock::now();
                    const CellProcessTask request{
                        "spatial/v2/s"+std::to_string(id+1)+
                          "/q0,0,0/c"+std::to_string(round)+",0,0",
                        "healthy-"+std::to_string(id),
                        static_cast<std::uint64_t>(100+round),
                        0xabc012345ULL+static_cast<std::uint64_t>(round),
                        static_cast<std::size_t>(id+1),
                        static_cast<std::uint8_t>(id%3)};
                    const auto result=run_cell_process(
                        binary,request,std::chrono::milliseconds(2000));
                    CHECK(result.passed);
                    return std::chrono::duration<double,std::milli>(
                        std::chrono::steady_clock::now()-start).count();
                }));
            }
            for(auto& job:pending)elapsed_ms.push_back(job.get());
        }
        CHECK(elapsed_ms.size()==workers*rounds);
        const auto total=std::accumulate(elapsed_ms.begin(),elapsed_ms.end(),0.0);
        std::sort(elapsed_ms.begin(),elapsed_ms.end());
        CHECK(elapsed_ms.front()>0 && elapsed_ms.back()<2000);
        const CellProcessTask request{
            "spatial/v2/s1/q-12,2,3/c999,5,6","timeout-worker",
            900,12345,7,2};
        for(int i=0;i<3;++i){
            CHECK(!run_cell_process(hung,request,
                std::chrono::milliseconds(40)).passed);
        }
        CHECK(run_cell_process(binary,request).passed);
        CHECK(!run_cell_process(binary,CellProcessTask{
            request.scope,request.worker,0,request.original_hash,
            request.physical_ships,request.detail}).passed);
        CHECK(!run_cell_process(binary,CellProcessTask{
            "spatial/v2/s1/q0,0,0/c0,0,0;rm -rf /",
            request.worker,1,1,1,1}).passed);
        std::cout<<"PASS native OS worker process endurance attempts="
                 <<elapsed_ms.size()
                 <<" average_ms="<<(total/elapsed_ms.size())
                 <<" p95_ms="<<elapsed_ms[static_cast<std::size_t>(
                       elapsed_ms.size()*95/100)]
                 <<" max_ms="<<elapsed_ms.back()
                 <<" and three real timeout terminations\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL native OS process endurance: "<<e.what()<<'\n';
        return 1;
    }
}
