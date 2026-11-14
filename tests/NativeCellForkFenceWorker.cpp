// SPACE-05C: isolated computation endpoint with no native world/SQLite API.
// It can only attest the narrow, immutable host-provided dispatch summary.
// A report NEVER grants cargo authority or permission to advance the world.
#include <cstdint>
#include <cerrno>
#ifdef __linux__
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
namespace {
std::uint64_t unsigned_number(const char* data){
    const std::string text(data?data:"");
    if(text.empty() || text.size()>20)throw std::invalid_argument("invalid number");
    for(const char c:text)if(c<'0'||c>'9')
        throw std::invalid_argument("nondecimal worker field");
    std::size_t used=0;
    const auto result=std::stoull(text,&used,10);
    if(used!=text.size())throw std::invalid_argument("invalid worker numeral");
    return result;
}
bool safe_scope(const std::string& text){
    if(text.empty() || text.size()>256 ||
       (text.rfind("spatial/v1/s",0)!=0 &&
        text.rfind("spatial/v2/s",0)!=0))return false;
    for(const char c:text)
        if(!((c>='0'&&c<='9')||(c>='A'&&c<='Z')||
             (c>='a'&&c<='z')||c=='/'||c==','||
             c=='-'||c=='_'))return false;
    return true;
}
bool safe_hex(const std::string& value){
    if(value.empty() || value.size()>256 || value.size()%2)return false;
    for(const char c:value)
        if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
}
int main(int argc,char** argv){
    try{
        if(argc!=8 || std::string(argv[1])!="--cell-read-v1")
            return 64;
        const std::string cell(argv[2]),worker(argv[3]);
        if(!safe_scope(cell) || !safe_hex(worker))return 65;
        const auto generation=unsigned_number(argv[4]);
        const auto digest=unsigned_number(argv[5]);
        const auto ships=unsigned_number(argv[6]);
        const auto detail=unsigned_number(argv[7]);
        if(!generation || !ships || ships>100000 || detail>2)return 66;
#ifdef __linux__
        // Explicit admission proof: report success only when kernel
        // seccomp rejects fork with EPERM. An unchecked or missing filter
        // must fail, not silently become a passing timeout test.
        errno=0;
        const pid_t attempted=fork();
        if(attempted==0)_exit(72);
        if(attempted>0){
            int status=0;
            (void)waitpid(attempted,&status,0);
            return 73;
        }
        if(errno!=EPERM)return 74;
#endif
        const auto score=ships*(detail+1);
        std::cout<<"SGCELL1|"<<cell<<'|'<<worker<<'|'<<generation
                 <<'|'<<digest<<'|'<<ships<<'|'<<detail
                 <<'|'<<score<<'\n';
        return std::cout.good()?0:70;
    }catch(const std::exception&){return 71;}
}
