#include "galaxy/ColonyThreatPolicy.hpp"
#include <iostream>
#include <stdexcept>
using namespace galaxy::npc_response;
using galaxy::colony_conflict::Attitude;
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(false)
int main(){
 try{
  ThreatContext c{};
  CHECK(assess_threat(c).intent==ThreatIntent::Ignore);
  c.colony=ColonyCharacter::Territorial;
  c.in_claimed_region=true;
  c.standing=VisitorStanding::Unknown;
  CHECK(assess_threat(c).intent==ThreatIntent::Hail);
  c.standing=VisitorStanding::Uninvited;
  CHECK(assess_threat(c).intent==ThreatIntent::WarnLeave);
  c.standing=VisitorStanding::Expelled;
  CHECK(assess_threat(c).intent==ThreatIntent::Intercept);
  c.standing=VisitorStanding::Invited;
  CHECK(assess_threat(c).intent==ThreatIntent::Ignore);
  c.standing=VisitorStanding::Uninvited;
  c.colony=ColonyCharacter::Criminal;
  CHECK(assess_threat(c).intent==ThreatIntent::PrepareAmbush);
  c.in_claimed_region=false;
  CHECK(assess_threat(c).intent==ThreatIntent::Shadow);
  c.colony=ColonyCharacter::Militant;
  c.in_claimed_region=true;
  CHECK(assess_threat(c).intent==ThreatIntent::Intercept);
  c.colony=ColonyCharacter::Cooperative;
  c.relation=Attitude::Hostile;
  CHECK(assess_threat(c).intent==ThreatIntent::WarnLeave);
  c.observed_attack=true;
  CHECK(assess_threat(c).basis==Basis::Defense);
  // All combinations remain nonbinding intent; no disposition grants
  // legal guilt, arrests, attack commands or taking other people's goods.
  for(int kind=0;kind<4;kind++)
   for(int standing=0;standing<4;standing++)
    for(int has_region=0;has_region<2;has_region++)
     for(int attacked=0;attacked<2;attacked++){
      ThreatContext p{};
      p.colony=static_cast<ColonyCharacter>(kind);
      p.standing=static_cast<VisitorStanding>(standing);
      p.in_claimed_region=has_region!=0;p.observed_attack=attacked!=0;
      auto a=assess_threat(p);
      CHECK(!a.legal_crime_finding && !a.can_attack &&
            !a.can_arrest && !a.can_seize_goods);
     }
  std::cout<<"PASS NPC-RESP-03A: hostile NPC colonies have diverse intent without fabricated legal or combat authority\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
