#pragma once
#include "logos_api.h"
#include "logos_api_client.h"

#include "zone_sequencer_api.h"

struct LogosModules {
    explicit LogosModules(LogosAPI* api) : api(api), 
        zone_sequencer(api) {}
    LogosAPI* api;
    ZoneSequencer zone_sequencer;
};
