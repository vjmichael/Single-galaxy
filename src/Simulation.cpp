
        else {
            ship.cargo += n;
            if(snapshot_version_>=123 && n>0)
                track_station_ore_mining(actor_id,job.target,job.start_command_id,n,
                    g!=geology_.end()?static_cast<std::uint8_t>(g->second.taxonomy):
                    local?static_cast<std::uint8_t>(*local):3);
        }
        const auto g=geology_.find(job.target);
        const auto local=local_deposit_taxonomy(job.target);
        const bool geological=g!=geology_.end() || local.has_value();
        if(n>0 && snapshot_version_<123 && colony_ships_.count(actor_id) && geological)
                if(snapshot_version_>=123)
                    consume_station_ore_lots(c.actor,0,c.issuer,c.amount,false);
        case Action::BuildStationRefinery: {
            // The plant is installed at a real player-built site, not gifted
            // by the station UI or a new save. Modules are finite ship goods.
            const auto* site=find(c.target);
            const auto utility=utilities_.find(c.target);
            auto colony=colony_ships_.find(c.actor);
            if(!site || site->type!=EntityType::Station ||
               !built_outposts_.count(c.target) ||
               built_outposts_.at(c.target).source_colony_ship!=c.actor ||
               site->owner_id!=c.issuer || actor.owner_id!=c.issuer ||
               colony==colony_ships_.end() || !colony->second.crew ||
               actor.dock_state!=DockState::Docked || actor.dock_station!=c.target){
                result.result=Outcome::Unauthorized;return result;
            }
            if(station_refineries_.count(c.target) || c.amount!=1){
                result.result=Outcome::InvalidTarget;return result;
            }
            if(utility==utilities_.end() || utility->second.source_ship!=c.actor ||
               utility->second.technicians<1 || utility->second.condition<=0){
                result.result=Outcome::InsufficientCrew;return result;
            }
            if(colony->second.modules<2){result.result=Outcome::NoResources;return result;}
            colony->second.modules-=2;
            --utility->second.technicians; // same finite trained resident, reassigned
            StationRefineryPlant plant{};
            plant.station=c.target;plant.source_ship=c.actor;plant.owner=c.issuer;
            plant.commissioned_receipt=c.id;
            station_refineries_.emplace(c.target,std::move(plant));
            snapshot_version_=std::max(snapshot_version_,123);
            result.transferred=4;result.result=Outcome::Applied;return result;
        }
        case Action::RefineStationOre: {
            const auto* site=find(c.target);
            const auto fit=station_refineries_.find(c.target);
            const auto h=hangars_.find({c.target,c.issuer});
            if(!site || site->type!=EntityType::Station ||
               fit==station_refineries_.end() ||
               fit->second.owner!=c.issuer || fit->second.source_ship!=c.actor ||
               actor.owner_id!=c.issuer || actor.dock_state!=DockState::Docked ||
               actor.dock_station!=c.target || h==hangars_.end()){
                result.result=Outcome::Unauthorized;return result;
            }
            // Physical station presence if this persona is registered.
            if(const auto person=player_people_.find(c.issuer);
               person!=player_people_.end() &&
               (person->second.state!=PlayerPersonState::AtStation ||
                person->second.physical_station!=c.target)){
                result.result=Outcome::Unauthorized;return result;
            }
            const auto lot_it=station_ore_lots_.find(static_cast<std::uint64_t>(c.amount));
            if(lot_it==station_ore_lots_.end() || lot_it->second.station!=c.target ||
               lot_it->second.owner!=c.issuer || lot_it->second.taxonomy>2 ||
               !lot_it->second.unload_receipt){
                result.result=Outcome::InvalidTarget;return result;
            }
            const auto util=utilities_.find(c.target);
            if(util==utilities_.end() || util->second.condition<=0 ||
               !util->second.powered || util->second.stored_energy<=0){
                result.result=Outcome::ModuleOffline;return result;
            }
            auto& plant=fit->second;
            station_refining::Facility engine{};
            engine.station=plant.station;engine.source_ship=plant.source_ship;
            engine.owner=plant.owner;engine.installed_modules=plant.embodied_modules;
            engine.assigned_workers=plant.assigned_workers;
            engine.power_units=util->second.stored_energy;
            engine.processed_tonnes=plant.processed_tonnes;
            engine.ready_tick=plant.ready_tick;engine.last_receipt=plant.last_receipt;
            engine.mineral_kg=plant.mineral_kg;
            station_refining::OreLot source{c.target,lot_it->second.rock,c.issuer,
                lot_it->second.unload_receipt,
                static_cast<AsteroidClass>(lot_it->second.taxonomy),lot_it->second.tonnes};
            auto& hangar=hangars_.at({c.target,c.issuer});
            auto status=station_refining::refine_one_tonne(hangar,source,engine,
                c.issuer,c.target,tick_,c.id);
            if(status.status!=station_refining::Status::Applied){
                result.result=status.status==station_refining::Status::NoOre?
                    Outcome::NoResources:status.status==station_refining::Status::CoolingDown?
                    Outcome::CooldownActive:Outcome::InvalidArgument;
                return result;
            }
            plant.processed_tonnes=engine.processed_tonnes;
            plant.ready_tick=engine.ready_tick;plant.last_receipt=engine.last_receipt;
            plant.mineral_kg.swap(engine.mineral_kg);
            const auto power_used=util->second.stored_energy-engine.power_units;
            if(power_used!=1 || util->second.consumed_energy>
               std::numeric_limits<std::int64_t>::max()-power_used)
                throw std::runtime_error("station refinery utility power ledger overflow");
            util->second.stored_energy=engine.power_units;
            util->second.consumed_energy+=power_used;
            auto& record=station_ore_lots_.at(lot_it->first);
            record.tonnes=source.remaining_tonnes;
            if(!record.tonnes)station_ore_lots_.erase(lot_it);
            result.transferred=status.processed_kg;
            snapshot_version_=std::max(snapshot_version_,123);
            result.result=Outcome::Applied;return result;
        }
                       h.residents+c.amount<(ui==utilities_.end()?0:ui->second.technicians)+
                           (station_refineries_.count(c.target)?station_refineries_.at(c.target).assigned_workers:0)+
                           community_staff+clinic_staff+assigned_policy_inspectors(c.target)){
                       hall+medics+refinery_staff+c.amount+
                       assigned_policy_inspectors(c.target)>hi->second.residents){
                    const auto refinery_staff=station_refineries_.count(c.target)?
                        station_refineries_.at(c.target).assigned_workers:0;
                if(snapshot_version_>=123)
                    consume_station_ore_lots(c.actor,actor.dock_station,c.issuer,moved,true);
                if(snapshot_version_>=123)
                    move_station_ore_lots(c.actor,actor.dock_station,c.issuer,moved,false,c.id);
            if(snapshot_version_>=123)
                move_station_ore_lots(c.actor,actor.dock_station,c.issuer,moved,true,c.id);
            else {
                actor.cargo += n;
                if(snapshot_version_>=123 && n>0)
                    track_station_ore_mining(c.actor,c.target,c.id,n,
                        g!=geology_.end()?static_cast<std::uint8_t>(g->second.taxonomy):
                        local?static_cast<std::uint8_t>(*local):3);
            }
            const auto g=geology_.find(c.target);
            const auto local=local_deposit_taxonomy(c.target);
            const bool geological=g!=geology_.end() || local.has_value();
            if(n>0 && snapshot_version_<123 && colony_ships_.count(c.actor) && geological)