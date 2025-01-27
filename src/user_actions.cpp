/*
    Generate a unique random set.

    @param {random} generator - random generator instance with seed
    @param {vector<uint16_t>} input - input vector to use as a source
    @param {uint16_t} amount - amount of elements to generate
    @param {uint16_t} offset - minimal distance to keep
*/
std::vector<uint16_t> random_set(
    random generator,
    std::vector<uint16_t> input,
    uint64_t amount = 1,
    uint16_t offset = 0)
{
  std::vector<uint16_t> _input(input);
  int size = _input.size();
  uint64_t _amount = std::min<uint64_t>(amount, size);

  std::vector<uint16_t> result = {};

  for (auto i = 0; i < _amount; i++)
  {
    auto index = generator.next(size);
    auto val = _input[index];
    result.push_back(val);

    // Remove everything around the element taking offset into account
    _input.erase(std::remove_if(
                     _input.begin(), _input.end(),
                     [&val = val, &offset = offset](const auto &tmp) -> bool
                     {
                       return tmp <= val + (offset * 2) && tmp >= val - (offset * 2);
                     }),
                 _input.end());

    size = _input.size();

    // Check if we still have items left or not
    if (size == 0)
    {
      break;
    }
  }

  return result;
}

/*
    Creates a new game. Randomizes the set using config.salt, caller and amount of completed sets. Allows rengeration of the set depending on config.params.regeneration_cd

    RAM costs are paid by the caller.

    @throws Will throw if the contract is in maintenace
    @throws Will throw if the user has a game running and the mints cannot be regenerated

    @auth caller
*/
void matchamonkey::newgame(eosio::name &owner)
{
  eosio::require_auth(owner);
  maintenace_check();

  auto games = get_games();
  auto users = get_users();
  auto config = get_config().get();
  auto game = games.find(owner.value);

  if (game != games.end())
  {
    // Has a game, check if it can be regenerated or not
    eosio::check(eosio::current_time_point() >= (game->time + eosio::milliseconds(config.params.regeneration_cd)), "Cannot regenerate your game yet");
    games.erase(game);
  }

  // Get or create the user
  auto user = users.find(owner.value);

  if (user == users.end())
  {
    users.emplace(owner, [&](auto &row)
                  { row.owner = owner; });

    user = users.find(owner.value);
  }
  else if (user->completed_sets >= config.params.reward_reset)
  {
    // Reset completed sets to 0
    users.modify(user, owner, [&](auto &row)
                 { row.completed_sets = 0; });
  }

  // Determine the next mints
  auto salt = config.salt;
  salt.append("-").append(owner.to_string());
  salt.append("-").append(std::to_string(user->completed_sets));
  salt.append("-").append(std::to_string(eosio::current_time_point().time_since_epoch().count()));

  // Randomize
  std::vector<uint16_t> result = random_set(
      // Init a random_generator based on config.salt, owner and completed sets
      random(eosio::sha256(salt.c_str(), salt.length())),

      // Generates a vector containing all the possible mints
      generate_set_with_mints(),

      pow<uint64_t>(config.params.new_game_base, std::min<uint16_t>(63, user->completed_sets + 1)),

      config.params.mint_offset);

  // Create entry in games table
  games.emplace(owner, [&](auto &row)
                {
                  row.owner = owner;
                  row.to_collect.assign(result.begin(), result.end()); // Assigns the generated set
                  row.collected = {};
                  row.time = eosio::current_time_point(); });

  eosio::action(
      permission_level{get_self(), eosio::name("active")},
      get_self(),
      eosio::name("log"),
      std::make_tuple(
          std::string("newgame"),
          result))
      .send();
}

/*
    Verify the collection by fetching the atomicassets mints.

    RAM costs are paid by the caller.

    @throws Will throw if the contract is in maintenace
    @throws Will throw if the user has no game running
    @throws Will throw if the user does not exist

    @auth caller
*/
void matchamonkey::verify(eosio::name &owner, std::vector<NFT> &owned_assets)
{
  eosio::require_auth(owner);
  maintenace_check();

  auto games = get_games();
  auto users = get_users();
  auto mints = get_mints();
  auto config = get_config().get();
  auto game = games.require_find(owner.value, "You have no running game");
  auto user = users.require_find(owner.value, "No user found");

  struct ASSET_INFO
  {
    uint16_t mint;
    uint64_t asset_id;
    // Offset = difference/distance between asset mint and mint to get + (mint / (max mint * 10))
    double offset = 0;

    bool operator<(const ASSET_INFO &other) const
    {
      return (offset < other.offset);
    }
  };
  std::vector<ASSET_INFO> owned = {};

  // Get the owned assets
  auto assets = atomicassets::get_assets(owner);
  for (const NFT &nft : owned_assets)
  {
    assets.require_find(nft.asset_id, "You do not own all assets");
    mints.require_find(nft.index, "Mint index not found");

    unfreeze(owner, nft.asset_id);

    auto entry = mints.get(nft.index);

    // Try to find the asset mint number
    auto iterator = std::find_if(entry.mints.begin(), entry.mints.end(), [&id = nft.asset_id](const MINT &mint) -> bool
                                 { return id == mint.asset_id; });

    // Asset mint number not found
    eosio::check(iterator != entry.mints.end(), "Asset mint number not found");

    // Create a ASSET_INFO struct witht he mint & asset id
    owned.push_back(ASSET_INFO{iterator->mint, nft.asset_id});
  }

  // Sort the collected & to_collect vector
  std::vector<uint16_t> collected(game->collected);
  std::vector<uint16_t> to_collect(game->to_collect);
  std::sort(to_collect.begin(), to_collect.end());
  std::sort(collected.begin(), collected.end());

  // Get the set difference (to be collected mints)
  std::vector<uint16_t> remainder;
  std::set_difference(to_collect.begin(), to_collect.end(), collected.begin(), collected.end(), std::inserter(remainder, remainder.begin()));

  auto frozen_assets = get_frozen_assets();

  // Loop over the remaining mints that are required
  for (auto &elem : remainder)
  {
    std::vector<ASSET_INFO> difference_map;

    // Loop over all the owned mints
    for (auto it = owned.begin(); it != owned.end(); ++it)
    {
      // Get the difference between the mint owned and to
      uint16_t const &difference = std::abs(int(it->mint - elem));

      // Is the difference bigger than the offset? Do not do anything
      if (difference > config.params.mint_offset)
      {
        continue;
      }

      // Is the difference 0? (exact match) populate the result and exit the loop
      if (difference == 0)
      {
        // Clear all previous matches for this mint
        difference_map.clear();
        difference_map.push_back(ASSET_INFO{it->mint, it->asset_id, 0});
        break;
      }

      // Add it to the result
      difference_map.push_back(ASSET_INFO{it->mint, it->asset_id, (double)difference + ((double)elem / (double)(config.params.max_mint * 10))});
    }

    // Skip, no match found
    if (difference_map.size() == 0)
    {
      continue;
    }

    // Sort the difference map
    std::sort(difference_map.begin(), difference_map.end());
    auto const &match = difference_map.begin();

    collected.push_back(elem);

    // Freeze asset
    // If already collected, do not freeze the asset
    frozen_assets.emplace(owner, [&](auto &row)
                          {
                            row.asset_id = match->asset_id;
                            row.owner = owner;
                            row.time = eosio::current_time_point(); });

    // Delete from mints to check
    // We remove the same mint numbers (if any) from the list
    owned.erase(std::remove_if(
                    owned.begin(), owned.end(),
                    [&mint = match->mint](const ASSET_INFO &tmp) -> bool
                    {
                      return tmp.mint == mint;
                    }),
                owned.end());
  }

  // Update the collected mints
  games.modify(game, owner, [&](auto &row)
               { row.collected.assign(collected.begin(), collected.end()); });
}

/*
    Complete the set, check if the caller collected the correct mints.
    Remove the game table entry and update user.completed_sets.

    RAM costs are paid by the caller.

    @throws Will throw if the contract is in maintenace
    @throws Will throw if the user has no game running
    @throws Will throw if the user does not exist
    @throws Will throw if the set has not been completed

    @auth caller
*/
void matchamonkey::complete(eosio::name &owner)
{
  eosio::require_auth(owner);
  maintenace_check();

  auto config = get_config().get();
  auto games = get_games();
  auto users = get_users();
  auto game = games.require_find(owner.value, "You have no running game");
  auto user = users.require_find(owner.value, "No user found");

  // Check if the user collected enough mints
  // Vyryn's fancy math request: https://discord.com/channels/733122024448458784/867103030516252713/867125763291086869
  int size = game->to_collect.size();
  int required = (size - ceil(log10(size)));
  eosio::check(game->collected.size() >= required, "You need to collect at least " + std::to_string(required) + " mints, got " + std::to_string(game->collected.size()));

  // Update the user
  users.modify(user, owner, [&](auto &row)
               { row.completed_sets = std::min<uint64_t>(user->completed_sets + 1, config.params.reward_reset); });

  auto rewards = get_rewards();

  auto reward = rewards.require_find(user->completed_sets > rewards.rbegin()->completions ? config.params.reward_cap : user->completed_sets, "No reward found");

  if (reward->amount.amount > 0)
  {
    // Can send a reward
    // Get the contract's balance of the token
    auto contract_balance = eosiotoken::get_balance(reward->contract, get_self(), reward->amount.symbol.code());

    if (contract_balance.amount >= reward->amount.amount)
    {
      // We have enough to send
      eosio::action(
          permission_level{get_self(), eosio::name("active")},
          reward->contract,
          eosio::name("transfer"),
          make_tuple(
              get_self(),
              owner,
              reward->amount,
              config.params.reward_memo))
          .send();
    }
    else
    {
      // Need to issue new tokens
      eosio::action(
          permission_level{reward->contract, eosio::name("active")},
          reward->contract,
          eosio::name("issue"),
          make_tuple(
              reward->contract,
              reward->amount,
              config.params.reward_memo))
          .send();

      // Transfer issued tokens
      eosio::action(
          permission_level{reward->contract, eosio::name("active")},
          reward->contract,
          eosio::name("transfer"),
          make_tuple(
              reward->contract,
              owner,
              reward->amount,
              config.params.reward_memo))
          .send();
    }
  }

  // Log completed to_collect
  eosio::action(
      permission_level{get_self(), eosio::name("active")},
      get_self(),
      eosio::name("log"),
      std::make_tuple(
          std::string("completed_to_collect"),
          game->to_collect))
      .send();

  // Log completed mints
  eosio::action(
      permission_level{get_self(), eosio::name("active")},
      get_self(),
      eosio::name("log"),
      std::make_tuple(
          std::string("completed_collected"),
          game->collected))
      .send();

  // Release the game entry
  games.erase(game);
}

/*
    Unfreeze an asset if the asset has been frozen for long enough.

    Frozen time set by config options.

    @throws Will throw if the contract is in maintenace
    @throws Will throw if asset canot be unfrozen

    @auth caller
*/
void matchamonkey::unfreeze(
    eosio::name &owner,
    uint64_t asset_id)
{
  require_auth(owner);
  maintenace_check();

  auto config = get_config().get();
  auto frozen_assets = get_frozen_assets();
  auto iterator = frozen_assets.find(asset_id);

  // Unfreeze if asset is found
  if (iterator != frozen_assets.end())
  {
    eosio::check(is_frozen(iterator->time, config.params.freeze_time), "Could not unfreeze the asset");

    frozen_assets.erase(iterator);
  }
}

/*
    Unfreeze all expired assets owned by someone.

    Frozen time set by config options.

    @throws Will throw if the contract is in maintenace

    @auth caller
*/
void matchamonkey::unfreezeall(
    eosio::name &owner)
{
  require_auth(owner);
  maintenace_check();

  auto config = get_config().get();
  auto frozen_assets = get_frozen_assets();
  auto owner_index = frozen_assets.get_index<eosio::name("owner")>();
  auto iterator = owner_index.lower_bound(owner.value);

  while (iterator->owner == owner)
  {
    if (is_frozen(iterator->time, config.params.freeze_time))
    {
      iterator = owner_index.erase(iterator);
    }
    else
    {
      iterator++;
    }
  }
}