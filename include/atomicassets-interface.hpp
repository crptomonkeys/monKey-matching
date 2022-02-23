#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>

using namespace eosio;
using namespace std;

namespace atomicassets
{
    static constexpr name ATOMICASSETS_ACCOUNT = name("atomicassets");

    //Scope: owner
    struct assets_s
    {
        uint64_t asset_id;
        name collection_name;
        name schema_name;
        int32_t template_id;
        name ram_payer;
        vector<asset> backed_tokens;
        vector<uint8_t> immutable_serialized_data;
        vector<uint8_t> mutable_serialized_data;

        uint64_t primary_key() const { return asset_id; };
    };

    typedef multi_index<name("assets"), assets_s> assets_t;

    assets_t get_assets(name acc)
    {
        return assets_t(ATOMICASSETS_ACCOUNT, acc.value);
    }
};