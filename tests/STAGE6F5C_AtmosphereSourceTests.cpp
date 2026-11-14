#include "galaxy/PlanetaryAtmosphere.hpp"
#include <cassert>
using galaxy::planetary::air::State;
using galaxy::planetary::air::valid;
using galaxy::planetary::air::breathable;
using galaxy::planetary::air::sealed_and_supplied;
using galaxy::planetary::air::thermally_protected;
int main(){
    assert(valid({0,0,false}));
    assert(!valid({0,210,false}));
    assert(!valid({99000,1001,false}));
    assert(!breathable({0,0,false}));
    assert(breathable({99000,210,false}));
    assert(!breathable({60000,210,false}));
    assert(!breathable({155000,210,false}));
    assert(!breathable({99000,210,true}));
    assert(!breathable({99000,100,false}));
    assert(!breathable({99000,450,false}));
    assert(!sealed_and_supplied(false,100,10,4));
    assert(!sealed_and_supplied(true,100,10,0));
    assert(!sealed_and_supplied(true,50,10,4));
    assert(!sealed_and_supplied(true,100,0,4));
    assert(sealed_and_supplied(true,100,10,4));
    assert(thermally_protected(true,80,1));
    assert(!thermally_protected(true,40,10));
}
