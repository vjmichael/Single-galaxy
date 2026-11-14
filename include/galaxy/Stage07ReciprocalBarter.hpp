#pragma once
// Stage 7 POP-10B: reciprocal material barter, NEVER created currency.
// Each side is a real independently owned, factory-return-backed physical
// v57 consignment. Original three-party shipment approvals, carrier transit,
// resource-ledger delivery and carrier/receiver settlement remain authoritative.
// Barter closes ONLY after *both* independently settled original deliveries.
#include "galaxy/Stage07IndependentCommerce.hpp"
#include <sqlite3.h>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace galaxy::stage7 {
struct BarterTerms {
    std::string id,export_a,export_b;
    PlayerId owner_a=0,owner_b=0;
    std::uint64_t generation=0,tick=0;
};
class ReciprocalBarter {
    std::string path_;
    sqlite3* db_=nullptr;
    std::function<bool(PlayerId)> authenticate_;
    struct Query {
        sqlite3_stmt* s=nullptr;
        Query(sqlite3* db,const std::string& query) {
            if(sqlite3_prepare_v2(db,query.c_str(),-1,&s,nullptr)!=SQLITE_OK)
                throw std::runtime_error("POP10 barter SQL prepare rejected");
        }
        ~Query(){sqlite3_finalize(s);}
        void text(int p,const std::string& v) {
            if(sqlite3_bind_text(s,p,v.c_str(),-1,SQLITE_TRANSIENT)!=SQLITE_OK)
                throw std::runtime_error("POP10 barter text bind failed");
        }
        void number(int p,std::uint64_t v) {
            if(v>static_cast<std::uint64_t>(
                std::numeric_limits<sqlite3_int64>::max()) ||
                sqlite3_bind_int64(s,p,static_cast<sqlite3_int64>(v))!=SQLITE_OK)
                throw std::runtime_error("POP10 barter numeric bind failed");
        }
        bool next(){
            const auto rc=sqlite3_step(s);
            if(rc!=SQLITE_ROW && rc!=SQLITE_DONE)
                throw std::runtime_error("POP10 barter SQL result invalid");
            return rc==SQLITE_ROW;
        }
        void done(){
            if(sqlite3_step(s)!=SQLITE_DONE)
                throw std::runtime_error("POP10 barter SQLite write failed");
        }
        std::uint64_t num(int c)const{
            const auto v=sqlite3_column_int64(s,c);
            if(v<0)throw std::runtime_error("POP10 negative original identity");
            return static_cast<std::uint64_t>(v);
        }
        std::string str(int c)const{
            const auto* p=sqlite3_column_text(s,c);
            return p?reinterpret_cast<const char*>(p):"";
        }
    };
    void sql(const std::string& q) {
        char* err=nullptr;
        if(sqlite3_exec(db_,q.c_str(),nullptr,nullptr,&err)!=SQLITE_OK){
            std::string message=err?err:"POP10 barter SQLite failed";
            sqlite3_free(err);throw std::runtime_error(message);
        }
    }
    bool matched(const PersistentWorld& w) {
        Query q(db_,"SELECT COALESCE(MAX(generation),0) FROM snapshots");
        return q.next() && q.num(0)==w.generation();
    }
    static bool valid_id(const std::string& s){
        return !s.empty() && s.size()<=100 &&
            s.find_first_not_of("abcdefghijklmnopqrstuvwxyz"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")==std::string::npos;
    }
    void bump(const char* field) {
        const auto command=std::string("UPDATE world_meta SET ")+field+"="+
            field+"+1 WHERE singleton=1";
        sql(command);
        if(sqlite3_changes(db_)!=1)
            throw std::runtime_error("POP10 original world anchor missing");
    }
    struct NativeLeg {
        std::string id,load,unload;
        PlayerId seller=0,buyer=0,carrier=0;
        EntityId source_ship=0,receiver_ship=0,site=0;
        int item=0;
        std::uint64_t amount=0;
    };
    NativeLeg leg(const std::string& id) {
        Query q(db_,"SELECT contract_id,source_owner,receiving_owner,"
            "carrier_owner,source_ship,receiving_ship,destination_site,"
            "item_index,quantity_kg,load_command,unload_command "
            "FROM sg_v56_contract WHERE contract_id=?");
        q.text(1,id);
        if(!q.next())throw std::runtime_error("POP10 original contract absent");
        NativeLeg l{};
        l.id=q.str(0);l.seller=std::stoull(q.str(1));
        l.buyer=std::stoull(q.str(2));l.carrier=std::stoull(q.str(3));
        l.source_ship=std::stoull(q.str(4));
        l.receiver_ship=std::stoull(q.str(5));
        l.site=std::stoull(q.str(6));
        l.item=static_cast<int>(q.num(7));l.amount=q.num(8);
        l.load=q.str(9);l.unload=q.str(10);
        if(l.seller==0 || l.buyer==0 || l.carrier==0 ||
           !l.amount || l.amount>1000 || (l.item!=1 && l.item!=2))
            throw std::runtime_error("POP10 invalid original physical leg");
        return l;
    }
    bool valid_pair(const std::string& a,const std::string& b,
                    PlayerId x,PlayerId y) {
        if(!valid_id(a)||!valid_id(b)||a==b||!x||!y||x==y)return false;
        const auto first=leg(a),second=leg(b);
        if(first.seller!=x || first.buyer!=y ||
           second.seller!=y || second.buyer!=x ||
           first.carrier==x || first.carrier==y ||
           second.carrier==x || second.carrier==y ||
           first.source_ship==second.source_ship ||
           first.site==second.site)return false;
        for(const auto& id:{a,b}){
            Query p(db_,"SELECT COUNT(*) FROM sg_v57_policy "
                        "WHERE contract_id=?");
            p.text(1,id);if(!p.next() || p.num(0)!=1)return false;
            Query cancel(db_,"SELECT COUNT(*) FROM sg_v56_cancel "
                             "WHERE contract_id=?");
            cancel.text(1,id);if(!cancel.next() || cancel.num(0))return false;
        }
        return true;
    }
    bool delivered(const NativeLeg& l) {
        Query q(db_,"SELECT s.outcome,s.receipt_command,s.receipt_generation,"
            "s.receipt_tick,s.site,s.quantity_kg "
            "FROM sg_v57_settlement s WHERE s.contract_id=?");
        q.text(1,l.id);
        if(!q.next() || q.str(0)!="delivered" || q.str(1)!=l.unload ||
           q.num(2)==0 || q.num(3)==0 || q.str(4)!=std::to_string(l.site) ||
           q.num(5)!=l.amount)return false;
        Query original(db_,"SELECT COUNT(*) FROM resource_ledger "
            "WHERE command_id=? AND reason='storage_stack_transfer_kg' "
            "AND amount=?");
        original.text(1,l.unload);original.number(2,l.amount);
        return original.next() && original.num(0)==1;
    }
    BarterTerms find(const std::string& id){
        Query q(db_,"SELECT id,export_a,export_b,owner_a,owner_b,"
            "generation,tick FROM sg_stage7_barter_terms WHERE id=?");
        q.text(1,id);
        if(!q.next())throw std::runtime_error("POP10 barter terms absent");
        BarterTerms t{};
        t.id=q.str(0);t.export_a=q.str(1);t.export_b=q.str(2);
        t.owner_a=q.num(3);t.owner_b=q.num(4);
        t.generation=q.num(5);t.tick=q.num(6);return t;
    }
    void ensure_count(const char* table,const char* anchor) {
        Query a(db_,std::string("SELECT ")+anchor+
                " FROM world_meta WHERE singleton=1");
        Query b(db_,std::string("SELECT COUNT(*) FROM ")+table);
        if(!a.next()||!b.next()||a.num(0)!=b.num(0))
            throw std::runtime_error("POP10 barter anchor/index corruption");
    }
public:
    ReciprocalBarter(const std::string& original_sqlite,
                     std::function<bool(PlayerId)> authenticate)
        :path_(original_sqlite),authenticate_(std::move(authenticate)){
        try{
            if(sqlite3_open_v2(path_.c_str(),&db_,
                 SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)
                throw std::runtime_error("POP10 cannot open original world");
            sqlite3_busy_timeout(db_,10000);
            {
                Query s(db_,"SELECT COUNT(*) FROM sqlite_master WHERE "
                            "type='table' AND name='snapshots'");
                if(!s.next() || s.num(0)!=1)
                    throw std::runtime_error("POP10 refuses shadow barter world");
            } // release original read snapshot before DDL or world writes
            sql("CREATE TABLE IF NOT EXISTS sg_stage7_barter_terms("
                "id TEXT PRIMARY KEY,export_a TEXT NOT NULL UNIQUE,"
                "export_b TEXT NOT NULL UNIQUE,owner_a INTEGER NOT NULL,"
                "owner_b INTEGER NOT NULL,generation INTEGER NOT NULL,"
                "tick INTEGER NOT NULL)");
            sql("CREATE TABLE IF NOT EXISTS sg_stage7_barter_leg_claim("
                "contract_id TEXT PRIMARY KEY,"
                "barter_id TEXT NOT NULL REFERENCES sg_stage7_barter_terms(id),"
                "position INTEGER NOT NULL CHECK(position IN (1,2)),"
                "UNIQUE(barter_id,position))");
            sql("CREATE TABLE IF NOT EXISTS sg_stage7_barter_consent("
                "barter_id TEXT NOT NULL REFERENCES sg_stage7_barter_terms(id),"
                "role INTEGER NOT NULL CHECK(role IN (1,2)),"
                "principal INTEGER NOT NULL,generation INTEGER NOT NULL,"
                "PRIMARY KEY(barter_id,role))");
            sql("CREATE TABLE IF NOT EXISTS sg_stage7_barter_close("
                "barter_id TEXT PRIMARY KEY REFERENCES sg_stage7_barter_terms(id),"
                "witness_a TEXT NOT NULL,witness_b TEXT NOT NULL,"
                "generation INTEGER NOT NULL,tick INTEGER NOT NULL)");
            for(const char* name:{"sg_stage7_barter_terms",
                                 "sg_stage7_barter_leg_claim",
                                 "sg_stage7_barter_consent",
                                 "sg_stage7_barter_close"}){
                sql(std::string("CREATE TRIGGER IF NOT EXISTS ")+name+
                    "_freeze_u BEFORE UPDATE ON "+name+
                    " BEGIN SELECT RAISE(ABORT,'immutable barter receipt');END");
                sql(std::string("CREATE TRIGGER IF NOT EXISTS ")+name+
                    "_freeze_d BEFORE DELETE ON "+name+
                    " BEGIN SELECT RAISE(ABORT,'immutable barter receipt');END");
            }
            for(const char* name:{"stage7_barter_terms_count",
                                 "stage7_barter_claim_count",
                                 "stage7_barter_consent_count",
                                 "stage7_barter_close_count"}){
                bool exists=false;
                Query cols(db_,"PRAGMA table_info(world_meta)");
                while(cols.next())if(cols.str(1)==name)exists=true;
                if(!exists)sql(std::string("ALTER TABLE world_meta ADD COLUMN ")+
                    name+" INTEGER NOT NULL DEFAULT 0 CHECK("+name+">=0)");
            }
            verify();
        }catch(...){if(db_)sqlite3_close(db_);db_=nullptr;throw;}
    }
    ~ReciprocalBarter(){if(db_)sqlite3_close(db_);}
    ReciprocalBarter(const ReciprocalBarter&)=delete;
    ReciprocalBarter& operator=(const ReciprocalBarter&)=delete;
    void verify(){
        shipping::verify_third_party_registry(path_);
        ensure_count("sg_stage7_barter_terms","stage7_barter_terms_count");
        ensure_count("sg_stage7_barter_leg_claim","stage7_barter_claim_count");
        ensure_count("sg_stage7_barter_consent","stage7_barter_consent_count");
        ensure_count("sg_stage7_barter_close","stage7_barter_close_count");
        Query terms(db_,"SELECT id,export_a,export_b,owner_a,owner_b "
                        "FROM sg_stage7_barter_terms");
        while(terms.next()){
            const auto id=terms.str(0),a=terms.str(1),b=terms.str(2);
            const auto owner_a=terms.num(3),owner_b=terms.num(4);
            if(!valid_pair(a,b,owner_a,owner_b))
                throw std::runtime_error("POP10 counterfeit bilateral contract");
            Query claimed(db_,"SELECT contract_id,position FROM "
                "sg_stage7_barter_leg_claim WHERE barter_id=? ORDER BY position");
            claimed.text(1,id);
            if(!claimed.next() || claimed.str(0)!=a || claimed.num(1)!=1 ||
               !claimed.next() || claimed.str(0)!=b || claimed.num(1)!=2 ||
               claimed.next())
                throw std::runtime_error("POP10 missing or reused physical leg");
            Query consents(db_,"SELECT role,principal FROM sg_stage7_barter_consent "
                               "WHERE barter_id=? ORDER BY role");
            consents.text(1,id);int n=0;
            bool seen_first=false,seen_second=false;
            while(consents.next()){
                const auto role=consents.num(0);
                if((role!=1 && role!=2) ||
                   (role==1 && seen_first) || (role==2 && seen_second) ||
                   consents.num(1)!=(role==1?owner_a:owner_b))
                    throw std::runtime_error("POP10 counterfeit trade consent");
                if(role==1)seen_first=true;
                if(role==2)seen_second=true;
                ++n;
            }
            if(n>2)throw std::runtime_error("POP10 excessive trade approvals");
            Query closed(db_,"SELECT witness_a,witness_b FROM "
                "sg_stage7_barter_close WHERE barter_id=?");
            closed.text(1,id);
            if(closed.next()){
                if(n!=2 || closed.str(0)!=leg(a).unload ||
                   closed.str(1)!=leg(b).unload ||
                   !delivered(leg(a)) || !delivered(leg(b)))
                    throw std::runtime_error("POP10 forged reciprocal settlement");
            }
        }
    }
    bool propose(const PersistentWorld& world,const std::string& id,
                 const std::string& sale_a,const std::string& sale_b,
                 PlayerId principal,bool owner_approved) {
        if(!owner_approved || !valid_id(id)||!valid_id(sale_a)||
           !valid_id(sale_b)||!principal ||
           !authenticate_ || !authenticate_(principal) ||
           !world.generation())return false;
        try{
            const auto a=leg(sale_a),b=leg(sale_b);
            if(principal!=a.seller ||
               !valid_pair(sale_a,sale_b,a.seller,b.seller))return false;
            // Trade terms must be agreed BEFORE physical dispatch; do not
            // retrospectively label two unrelated delivered shipments barter.
            Query sent(db_,"SELECT COUNT(*) FROM sg_v57_dispatch "
                "WHERE contract_id=? OR contract_id=?");
            sent.text(1,sale_a);sent.text(2,sale_b);
            if(!sent.next() || sent.num(0)!=0)return false;
            sql("BEGIN IMMEDIATE");
            // Re-read original independently mutable consignment authority
            // under the SAME SQLite write lock; a cancellation or dispatch
            // cannot race the earlier read-only offer inspection.
            if(!matched(world) ||
               !valid_pair(sale_a,sale_b,a.seller,b.seller)){
                sql("ROLLBACK");return false;
            }
            Query dispatch_now(db_,"SELECT COUNT(*) FROM sg_v57_dispatch "
                                   "WHERE contract_id=? OR contract_id=?");
            dispatch_now.text(1,sale_a);dispatch_now.text(2,sale_b);
            if(!dispatch_now.next() || dispatch_now.num(0)!=0){
                sql("ROLLBACK");return false;
            }
            Query insert(db_,"INSERT INTO sg_stage7_barter_terms "
               "(id,export_a,export_b,owner_a,owner_b,generation,tick) "
               "VALUES(?,?,?,?,?,?,?)");
            insert.text(1,id);insert.text(2,sale_a);insert.text(3,sale_b);
            insert.number(4,a.seller);insert.number(5,b.seller);
            insert.number(6,world.generation());
            insert.number(7,world.world().tick());insert.done();
            for(const auto& entry:std::vector<std::pair<std::string,int>>{
                {sale_a,1},{sale_b,2}}){
                Query claim(db_,"INSERT INTO sg_stage7_barter_leg_claim "
                    "(contract_id,barter_id,position) VALUES(?,?,?)");
                claim.text(1,entry.first);claim.text(2,id);
                claim.number(3,static_cast<std::uint64_t>(entry.second));
                claim.done();
                bump("stage7_barter_claim_count");
            }
            bump("stage7_barter_terms_count");sql("COMMIT");
            return true;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);return false;}
    }
    bool approve(const PersistentWorld& world,const std::string& id,
                 PlayerId principal) {
        if(!principal || !authenticate_ || !authenticate_(principal))
            return false;
        try{
            const auto t=find(id);
            const auto role=principal==t.owner_a?1:principal==t.owner_b?2:0;
            if(!role || !valid_pair(t.export_a,t.export_b,
                                   t.owner_a,t.owner_b))return false;
            sql("BEGIN IMMEDIATE");
            if(!matched(world) ||
               !valid_pair(t.export_a,t.export_b,t.owner_a,t.owner_b)){
                sql("ROLLBACK");return false;
            }
            Query q(db_,"INSERT INTO sg_stage7_barter_consent "
                       "(barter_id,role,principal,generation) VALUES(?,?,?,?)");
            q.text(1,id);q.number(2,role);q.number(3,principal);
            q.number(4,world.generation());q.done();
            bump("stage7_barter_consent_count");sql("COMMIT");
            return true;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);return false;}
    }
    // Purely accounting closure. No phantom material or money changes here.
    // The two ORIGINAL shipments have ALREADY transferred their real cargo.
    bool settle(const PersistentWorld& world,const std::string& id){
        try{
            const auto t=find(id);
            verify();
            Query consents(db_,"SELECT COUNT(*) FROM sg_stage7_barter_consent "
                               "WHERE barter_id=?");
            consents.text(1,id);
            if(!consents.next() || consents.num(0)!=2 ||
               !delivered(leg(t.export_a)) || !delivered(leg(t.export_b)))
                return false;
            sql("BEGIN IMMEDIATE");
            if(!matched(world) ||
               !valid_pair(t.export_a,t.export_b,t.owner_a,t.owner_b) ||
               !delivered(leg(t.export_a)) || !delivered(leg(t.export_b))){
                sql("ROLLBACK");return false;
            }
            Query q(db_,"INSERT INTO sg_stage7_barter_close "
                "(barter_id,witness_a,witness_b,generation,tick) "
                "VALUES(?,?,?,?,?)");
            q.text(1,id);q.text(2,leg(t.export_a).unload);
            q.text(3,leg(t.export_b).unload);
            q.number(4,world.generation());q.number(5,world.world().tick());
            q.done();bump("stage7_barter_close_count");sql("COMMIT");
            verify();return true;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);return false;}
    }
};
} // namespace galaxy::stage7
