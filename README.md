# monKey matching

Moved to https://github.com/crptomonkeys/monKey-matching

| Component      | Test net                                                     | Main net                                                  | Source                                         |
| -------------- | ------------------------------------------------------------ | --------------------------------------------------------- | ---------------------------------------------- |
| Contract name  | [bvyzsshbnkjl](https://wax-test.bloks.io/account/bvyzsshbnkjl) | [monkeysmatch](https://wax.bloks.io/account/monkeysmatch) | https://github.com/crptomonkeys/monKey-matching |
| Game UI        |                                                              | https://match.cryptomonkeys.cc/                           |                                                |
| Token contract | [wfqzrtsviwdq](https://wax-test.bloks.io/account/wfqzrtsviwdq) | [monkeystoken](https://wax.bloks.io/account/monkeystoken) | https://github.com/EOSIO/eosio.contracts/tree/master/contracts/eosio.token    |
| Shop contract  | [xummfxbitlwa](https://wax-test.bloks.io/account/xummfxbitlwa) | [monkeymarket](https://wax.bloks.io/account/monkeymarket) | https://github.com/crptomonkeys/monkeymarket   |

## Actions

| Action name | Description                                                  | Scope/authorization |
| ----------- | ------------------------------------------------------------ | ------------------- |
| newgame     | Create a new game based on parameters set in the contract's config. Generates a random set of mints to collect.<br /><br />Users can regenerate their game after a certain amount of time, see regeneration_cd. | owner (invoker)     |
| verify      | Verifies the supplied assets based on the the parameters set in the contract's config and the generated random mints. | owner (invoker)     |
| complete    | Complete the game after verifying your assets. Upon successful completion you will receive a reward based on the rewards configured in the "rewards" table. | owner (invoker)     |
| unfreeze    | Unfreeze a certain asset, the time an asset is frozen depends on the contract's config. **Cleans up the RAM used by playing this asset.** | none                |
| unfreezeall | Same behaviour as unfreeze except it unfreezes all the assets, if possible, from a certain user. **This is an important part in cleaning up RAM for users.** | none                |
| init        | Initialize the contract's config.                            | contract            |
| destruct    | Destruct the contract's config, important for contract upgrades. | contract            |
| maintenance | Enable/disable maintenance mode. If maintenance mode is on then the following actions will be blocked from execution: newgame, verify, complete, complete, unfreeze, unfreezeall | contract            |
| setsalt     | Update the contract's salt used in PRNG.                     | contract            |
| setparams   | Update the contract's configuration.                         | contract            |
| rmmint      | Remove a certain mint index from the "mints" table.          | contract            |
| addmint     | Add a chunk of mints to the contracts' "mints" table for a certain template. | contract            |
| rmreward    | Removed a reward from the "rewards" table.                   | contract            |
| addreward   | Adds or updates a reward from the "rewards" table.           | contract            |

## Contract config

The contract is mainly configured through the `setparams` action. For rewards refer to the next section.

| Key             | Default               | Description                                                  |
| --------------- | --------------------- | ------------------------------------------------------------ |
| new_game_base   | 2                     | The amount of mints will be based on new_game_base^completions. This will make it so that games follow the following pattern of mints for a config value of 2: 2, 4, 8, 16, 32, 64, ... |
| reward_cap      | 6                     | When a player reaches the highest level/completions defined in the "rewards" table then fall back to this level for rewards. |
| reward_reset    | 16                    | Resets the user back to level 0 after reaching this level.   |
| min_mint        | 20                    | The min mint number to be generated in the game. (Inclusive) |
| max_mint        | 1919                  | The max mint number to be generated in the game. (Inclusive) |
| mint_offset     | 1                     | The distance or offset between mints when verifying your assets. When set to 1 this always people to collect #18, #19 or #20 when their target is #19. When set to 0 the mints must be an exact match. |
| freeze_time     | 86400000              | How long an asset should be frozen, in seconds.              |
| regeneration_cd | 259200000             | Define after how long can a game be regenerated, in seconds. |
| reward_memo     | Set completion reward | The memo used in the FT transfer when a game is completed.   |

## Rewards config

Rewards are configured using `addreward` and `rmreward`. Please also refer to the `reward_cap` config option in the previous section.

| Key         | Description                                                  | Example         |
| ----------- | ------------------------------------------------------------ | --------------- |
| completions | The amount of completions.                                   | 1               |
| contract    | The contract's address of the FT. Any FT is supported.       | eosio.token     |
| amount      | The amount of the FT to send. Please make sure that the precision and symbol are correct. | 19.00000000 WAX |




## Scripts & limitations
This smart contract requires some data to be populated from an external source. The main reasons for this are: 
- PRNG (pseudo random number generation)

  - A design choice was made to not use the orng.wax oracle, this is due to this contract otherwise being vulnerable to an attack vectors which might result in high RAM usage for the contract and making the contract unusable. This is due to how the contract is designed to work. 

    Whenever a new game is created a call would need to be made to orng.wax which costs the contract RAM. Due to the new game action being callable without any sort of requirements by any address we would leave open a big attack vector. This cost could not be transferred to the caller in order to prevent any sort of abuse.

  - A semi-static salt is used instead to feed the PRNG as a part of the seed. **There is a trade-off in 'security' here so this solution is only viable and usable in this specific situation and the salt should be updated on a regular interval (eg every 5 minutes) in order to prevent any sort of other attacks where people can predict their sets.**

- Mint numbers

  - Mint numbers are not saved on the WAX chain and need to be fed into the contract somehow. 
  - Smart contracts cannot "make external API calls" to external sources so they get populated in the contract's table from an external source.
  - Mint numbers should be updated every so often when new assets are minted.
  - The mint numbers are not saved as individual entries, this is due to the RAM consumption with tables. Every template is "chunked", during ingestion, into sets of x assets in order to save on RAM consumption. This method can save the contract several megabytes in RAM.
    - Every new entry uses minimal 112 bytes
    - Ref: https://github.com/EOSIO/eos/issues/4532#issuecomment-403250745
  
  
  
  Configuration wise, copy `scripts/config.example.json` to `scripts/config.json` and update
  
  - auth.address, the address you will be running this script with
  - auth.key, the private key of your account (active key)
  - endpoints, update both wax & atomicassets to the matching test or main net endpoints; leave undefined to get an automatic list of endpoints
  - target.contract, the address of the contract (same as auth.address)
  - mintsConfig.filter.collection_name, the name of the collection to filter on
  - mintsConfig.filter.schema_name, the name of the schema to filter on
  - mintsConfig.minMint, the minimal mint to include
  - mintsConfig.maxMint, the max mint to include
  
  
  
  For updating the mints, run: 
  
  ```bash
  cd scripts
  yarn
  node .\cacheMints.js
  node .\uploadMints.js
  ```
  
  For updating the salt:
  
  ```bash
  cd scripts
  yarn
  node .\salt.js
  ```
  
  
