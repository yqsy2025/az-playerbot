/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Item.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "ItemEnchantmentMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "Tokenize.h"
#include "WorldPacket.h"

void AddItemsSetItem(Player* player, Item* item)
{
    ItemTemplate const* proto = item->GetTemplate();
    uint32 setid = proto->ItemSet;

    ItemSetEntry const* set = sItemSetStore.LookupEntry(setid);

    if (!set)
    {
        LOG_ERROR("sql.sql", "Item set {} for item (id {}) not found, mods not applied.", setid, proto->ItemId);
        return;
    }

    if (set->required_skill_id && player->GetSkillValue(set->required_skill_id) < set->required_skill_value)
        return;

    ItemSetEffect* eff = nullptr;

    for (std::size_t x = 0; x < player->ItemSetEff.size(); ++x)
    {
        if (player->ItemSetEff[x] && player->ItemSetEff[x]->setid == setid)
        {
            eff = player->ItemSetEff[x];
            break;
        }
    }

    if (!eff)
    {
        eff = new ItemSetEffect();
        eff->setid = setid;

        std::size_t x = 0;
        for (; x < player->ItemSetEff.size(); ++x)
            if (!player->ItemSetEff[x])
                break;

        if (x < player->ItemSetEff.size())
            player->ItemSetEff[x] = eff;
        else
            player->ItemSetEff.push_back(eff);
    }

    ++eff->item_count;

    for (uint32 x = 0; x < MAX_ITEM_SET_SPELLS; ++x)
    {
        if (!set->spells [x])
            continue;
        //not enough for  spell
        if (set->items_to_triggerspell[x] > eff->item_count)
            continue;

        uint32 z = 0;
        for (; z < MAX_ITEM_SET_SPELLS; ++z)
            if (eff->spells[z] && eff->spells[z]->Id == set->spells[x])
                break;

        if (z < MAX_ITEM_SET_SPELLS)
            continue;

        //new spell
        for (uint32 y = 0; y < MAX_ITEM_SET_SPELLS; ++y)
        {
            if (!eff->spells[y])                             // free slot
            {
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(set->spells[x]);
                if (!spellInfo)
                {
                    LOG_ERROR("entities.item", "WORLD: unknown spell id {} in items set {} effects", set->spells[x], setid);
                    break;
                }

                // spell casted only if fit form requirement, in other case will casted at form change
                if (sScriptMgr->CanItemApplyEquipSpell(player, item))
                {
                    player->ApplyEquipSpell(spellInfo, nullptr, true);
                }

                eff->spells[y] = spellInfo;
                break;
            }
        }
    }
}

void RemoveItemsSetItem(Player* player, ItemTemplate const* proto)
{
    uint32 setid = proto->ItemSet;

    ItemSetEntry const* set = sItemSetStore.LookupEntry(setid);

    if (!set)
    {
        LOG_ERROR("sql.sql", "Item set #{} for item #{} not found, mods not removed.", setid, proto->ItemId);
        return;
    }

    ItemSetEffect* eff = nullptr;
    std::size_t setindex = 0;
    for (; setindex < player->ItemSetEff.size(); setindex++)
    {
        if (player->ItemSetEff[setindex] && player->ItemSetEff[setindex]->setid == setid)
        {
            eff = player->ItemSetEff[setindex];
            break;
        }
    }

    // can be in case now enough skill requirement for set appling but set has been appliend when skill requirement not enough
    if (!eff)
        return;

    --eff->item_count;

    for (uint32 x = 0; x < MAX_ITEM_SET_SPELLS; x++)
    {
        if (!set->spells[x])
            continue;

        // enough for spell
        if (set->items_to_triggerspell[x] <= eff->item_count)
            continue;

        for (uint32 z = 0; z < MAX_ITEM_SET_SPELLS; z++)
        {
            if (eff->spells[z] && eff->spells[z]->Id == set->spells[x])
            {
                // spell can be not active if not fit form requirement
                player->ApplyEquipSpell(eff->spells[z], nullptr, false);
                eff->spells[z] = nullptr;
                break;
            }
        }
    }

    if (!eff->item_count)                                    //all items of a set were removed
    {
        ASSERT(eff == player->ItemSetEff[setindex]);
        delete eff;
        player->ItemSetEff[setindex] = nullptr;
    }
}

bool ItemCanGoIntoBag(ItemTemplate const* pProto, ItemTemplate const* pBagProto)
{
    if (!pProto || !pBagProto)
        return false;

    switch (pBagProto->Class)
    {
        case ITEM_CLASS_CONTAINER:
        {
            if (pBagProto->SubClass == ITEM_SUBCLASS_CONTAINER)
            {
                return true;
            }
            else
            {
                if (pProto->Class == ITEM_CLASS_CONTAINER)
                {
                    return false;
                }

                switch (pBagProto->SubClass)
                {
                    case ITEM_SUBCLASS_SOUL_CONTAINER:
                        if (!(pProto->BagFamily & BAG_FAMILY_MASK_SOUL_SHARDS))
                            return false;
                        return true;
                    case ITEM_SUBCLASS_HERB_CONTAINER:
                        if (!(pProto->BagFamily & BAG_FAMILY_MASK_HERBS))
                            return false;
                        return true;
                    case ITEM_SUBCLASS_ENCHANTING_CONTAINER:
                        if (!(pProto->BagFamily & BAG_FAMILY_MASK_ENCHANTING_SUPP))
                            return false;
                        return true;
                    case ITEM_SUBCLASS_MINING_CONTAINER:
                        if (!(pProto->BagFamily & BAG_FAMILY_MASK_MINING_SUPP))
                            return false;
                        return true;
                    case ITEM_SUBCLASS_ENGINEERING_CONTAINER:
                        if (!(pProto->BagFamily & BAG_FAMILY_MASK_ENGINEERING_SUPP))
                            return false;
                        return true;
                    case ITEM_SUBCLASS_GEM_CONTAINER:
                        if (!(pProto->BagFamily & BAG_FAMILY_MASK_GEMS))
                            return false;
                        return true;
                    case ITEM_SUBCLASS_LEATHERWORKING_CONTAINER:
                        if (!(pProto->BagFamily & BAG_FAMILY_MASK_LEATHERWORKING_SUPP))
                            return false;
                        return true;
                    case ITEM_SUBCLASS_INSCRIPTION_CONTAINER:
                        if (!(pProto->BagFamily & BAG_FAMILY_MASK_INSCRIPTION_SUPP))
                            return false;
                        return true;
                    default:
                        return false;
                }
            }
        }
        case ITEM_CLASS_QUIVER:
        {
            if (pProto->Class == ITEM_CLASS_QUIVER)
            {
                return false;
            }

            switch (pBagProto->SubClass)
            {
                case ITEM_SUBCLASS_QUIVER:
                    if (!(pProto->BagFamily & BAG_FAMILY_MASK_ARROWS))
                        return false;
                    return true;
                case ITEM_SUBCLASS_AMMO_POUCH:
                    if (!(pProto->BagFamily & BAG_FAMILY_MASK_BULLETS))
                        return false;
                    return true;
                default:
                    return false;
            }
        }
    }

    return false;
}

Item::Item()
{
    m_objectType |= TYPEMASK_ITEM;
    m_objectTypeId = TYPEID_ITEM;

    m_updateFlag = UPDATEFLAG_LOWGUID;

    m_valuesCount = ITEM_END;
    m_slot = 0;
    uState = ITEM_NEW;
    uQueuePos = -1;
    m_container = nullptr;
    m_lootGenerated = false;
    mb_in_trade = false;
    m_lastPlayedTimeUpdate = GameTime::GetGameTime().count();

    m_refundRecipient = 0;
    m_paidMoney = 0;
    m_paidExtendedCost = 0;
}

bool Item::Create(ObjectGuid::LowType guidlow, uint32 itemid, Player const* owner)
{
    Object::_Create(guidlow, 0, HighGuid::Item);

    SetEntry(itemid);
    SetObjectScale(1.0f);

    SetGuidValue(ITEM_FIELD_OWNER, owner ? owner->GetGUID() : ObjectGuid::Empty);
    SetGuidValue(ITEM_FIELD_CONTAINED, owner ? owner->GetGUID() : ObjectGuid::Empty);

    ItemTemplate const* itemProto = sObjectMgr->GetItemTemplate(itemid);
    if (!itemProto)
        return false;

    SetUInt32Value(ITEM_FIELD_STACK_COUNT, 1);
    SetUInt32Value(ITEM_FIELD_MAXDURABILITY, itemProto->MaxDurability);
    SetUInt32Value(ITEM_FIELD_DURABILITY, itemProto->MaxDurability);

    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        SetSpellCharges(i, itemProto->Spells[i].SpellCharges);

    SetUInt32Value(ITEM_FIELD_DURATION, itemProto->Duration);
    SetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME, 0);
    sScriptMgr->OnItemCreate(this, itemProto, owner);
    return true;
}

// Returns true if Item is a bag AND it is not empty.
// Returns false if Item is not a bag OR it is an empty bag.
bool Item::IsNotEmptyBag() const
{
    if (Bag const* bag = ToBag())
        return !bag->IsEmpty();
    return false;
}

void Item::UpdateDuration(Player* owner, uint32 diff)
{
    if (!GetUInt32Value(ITEM_FIELD_DURATION))
        return;

    LOG_DEBUG("entities.player.items", "Item::UpdateDuration Item (Entry: {} Duration {} Diff {})", GetEntry(), GetUInt32Value(ITEM_FIELD_DURATION), diff);

    if (GetUInt32Value(ITEM_FIELD_DURATION) <= diff)
    {
        sScriptMgr->OnItemExpire(owner, GetTemplate());
        owner->DestroyItem(GetBagSlot(), GetSlot(), true);
        return;
    }

    SetUInt32Value(ITEM_FIELD_DURATION, GetUInt32Value(ITEM_FIELD_DURATION) - diff);
    SetState(ITEM_CHANGED, owner);                          // save new time in database
}

void Item::SaveToDB(CharacterDatabaseTransaction trans)
{
    bool isInTransaction = static_cast<bool>(trans);
    if (!isInTransaction)
        trans = CharacterDatabase.BeginTransaction();

    ObjectGuid::LowType guid = GetGUID().GetCounter();
    switch (uState)
    {
        case ITEM_NEW:
        case ITEM_CHANGED:
            {
                uint8 index = 0;
                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(uState == ITEM_NEW ? CHAR_REP_ITEM_INSTANCE : CHAR_UPD_ITEM_INSTANCE);
                stmt->SetData(  index, GetEntry());
                stmt->SetData(++index, GetOwnerGUID().GetCounter());
                stmt->SetData(++index, GetGuidValue(ITEM_FIELD_CREATOR).GetCounter());
                stmt->SetData(++index, GetGuidValue(ITEM_FIELD_GIFTCREATOR).GetCounter());
                stmt->SetData(++index, GetCount());
                stmt->SetData(++index, GetUInt32Value(ITEM_FIELD_DURATION));

                std::ostringstream ssSpells;
                for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
                    ssSpells << GetSpellCharges(i) << ' ';
                stmt->SetData(++index, ssSpells.str());

                stmt->SetData(++index, GetUInt32Value(ITEM_FIELD_FLAGS));

                std::ostringstream ssEnchants;
                for (uint8 i = 0; i < MAX_ENCHANTMENT_SLOT; ++i)
                {
                    ssEnchants << GetEnchantmentId(EnchantmentSlot(i)) << ' ';
                    ssEnchants << GetEnchantmentDuration(EnchantmentSlot(i)) << ' ';
                    ssEnchants << GetEnchantmentCharges(EnchantmentSlot(i)) << ' ';
                }
                stmt->SetData(++index, ssEnchants.str());

                stmt->SetData (++index, GetItemRandomPropertyId());
                stmt->SetData(++index, GetUInt32Value(ITEM_FIELD_DURABILITY));
                stmt->SetData(++index, GetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME));
                stmt->SetData(++index, m_text);
                stmt->SetData(++index, guid);

                trans->Append(stmt);

                if ((uState == ITEM_CHANGED) && IsWrapped())
                {
                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GIFT_OWNER);
                    stmt->SetData(0, GetOwnerGUID().GetCounter());
                    stmt->SetData(1, guid);
                    trans->Append(stmt);
                }
                break;
            }
        case ITEM_REMOVED:
            {
                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEM_INSTANCE);
                stmt->SetData(0, guid);
                trans->Append(stmt);

                if (IsWrapped())
                {
                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GIFT);
                    stmt->SetData(0, guid);
                    trans->Append(stmt);
                }

                if (!isInTransaction)
                    CharacterDatabase.CommitTransaction(trans);

                delete this;
                return;
            }
        case ITEM_UNCHANGED:
            break;
    }

    SetState(ITEM_UNCHANGED);

    if (!isInTransaction)
        CharacterDatabase.CommitTransaction(trans);
}

bool Item::LoadFromDB(ObjectGuid::LowType guid, ObjectGuid owner_guid, Field* fields, uint32 entry)
{
    //                                                    0                1      2         3        4      5             6                 7           8           9    10
    //result = CharacterDatabase.Query("SELECT creatorGuid, giftCreatorGuid, count, duration, charges, flags, enchantments, randomPropertyId, durability, playedTime, text FROM item_instance WHERE guid = '{}'", guid);

    // create item before any checks for store correct guid
    // and allow use "FSetState(ITEM_REMOVED); SaveToDB();" for deleting item from DB
    Object::_Create(guid, 0, HighGuid::Item);

    // Set entry, MUST be before proto check
    SetEntry(entry);
    SetObjectScale(1.0f);

    ItemTemplate const* proto = GetTemplate();
    if (!proto)
    {
        LOG_ERROR("entities.item", "Invalid entry {} for item {}. Refusing to load.", GetEntry(), GetGUID().ToString());
        return false;
    }

    // set owner (not if item is only loaded for gbank/auction/mail
    if (owner_guid)
        SetOwnerGUID(owner_guid);

    bool need_save = false;                                 // need explicit save data at load fixes
    SetGuidValue(ITEM_FIELD_CREATOR, ObjectGuid::Create<HighGuid::Player>(fields[0].Get<uint32>()));
    SetGuidValue(ITEM_FIELD_GIFTCREATOR, ObjectGuid::Create<HighGuid::Player>(fields[1].Get<uint32>()));
    SetCount(fields[2].Get<uint32>());

    uint32 duration = fields[3].Get<uint32>();
    SetUInt32Value(ITEM_FIELD_DURATION, duration);
    // update duration if need, and remove if not need
    if ((proto->Duration == 0) != (duration == 0))
    {
        SetUInt32Value(ITEM_FIELD_DURATION, proto->Duration);
        need_save = true;
    }

    std::vector<std::string_view> tokens = Acore::Tokenize(fields[4].Get<std::string_view>(), ' ', false);
    if (tokens.size() == MAX_ITEM_PROTO_SPELLS)
    {
        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            if (Optional<int32> charges = Acore::StringTo<int32>(tokens[i]))
                SetSpellCharges(i, *charges);
            else
                LOG_ERROR("entities.item", "Invalid charge info '{}' for item {}, charge data not loaded.", tokens.at(i), GetGUID().ToString());
        }
    }

    SetUInt32Value(ITEM_FIELD_FLAGS, fields[5].Get<uint32>());
    // Remove bind flag for items vs NO_BIND set
    if (IsSoulBound() && proto->Bonding == NO_BIND && sScriptMgr->CanApplySoulboundFlag(this, proto))
    {
        ApplyModFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_SOULBOUND, false);
        need_save = true;
    }

    std::string enchants = fields[6].Get<std::string>();

    if (!_LoadIntoDataField(fields[6].Get<std::string>(), ITEM_FIELD_ENCHANTMENT_1_1, MAX_ENCHANTMENT_SLOT * MAX_ENCHANTMENT_OFFSET))
    {
        LOG_WARN("entities.item", "Invalid enchantment data '{}' for item {}. Forcing partial load.", fields[6].Get<std::string>(), GetGUID().ToString());
    }

    SetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID, fields[7].Get<int16>());
    // recalculate suffix factor
    if (GetItemRandomPropertyId() < 0)
        UpdateItemSuffixFactor();

    uint32 durability = fields[8].Get<uint16>();
    SetUInt32Value(ITEM_FIELD_DURABILITY, durability);

    // update max durability (and durability) if need
    // xinef: do not overwrite durability for wrapped items!!
    SetUInt32Value(ITEM_FIELD_MAXDURABILITY, proto->MaxDurability);
    if (durability > proto->MaxDurability && !IsWrapped())
    {
        SetUInt32Value(ITEM_FIELD_DURABILITY, proto->MaxDurability);
        need_save = true;
    }

    SetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME, fields[9].Get<uint32>());
    SetText(fields[10].Get<std::string>());

    if (need_save)                                           // normal item changed state set not work at loading
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ITEM_INSTANCE_ON_LOAD);
        stmt->SetData(0, GetUInt32Value(ITEM_FIELD_DURATION));
        stmt->SetData(1, GetUInt32Value(ITEM_FIELD_FLAGS));
        stmt->SetData(2, GetUInt32Value(ITEM_FIELD_DURABILITY));
        stmt->SetData(3, guid);
        CharacterDatabase.Execute(stmt);
    }

    return true;
}

/*static*/
void Item::DeleteFromDB(CharacterDatabaseTransaction trans, ObjectGuid::LowType itemGuid)
{
    sScriptMgr->OnGlobalItemDelFromDB(trans, itemGuid);
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEM_INSTANCE);
    stmt->SetData(0, itemGuid);
    trans->Append(stmt);
}

void Item::DeleteFromDB(CharacterDatabaseTransaction trans)
{
    DeleteFromDB(trans, GetGUID().GetCounter());
}

/*static*/
void Item::DeleteFromInventoryDB(CharacterDatabaseTransaction trans, ObjectGuid::LowType itemGuid)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_INVENTORY_BY_ITEM);
    stmt->SetData(0, itemGuid);
    trans->Append(stmt);
}

void Item::DeleteFromInventoryDB(CharacterDatabaseTransaction trans)
{
    DeleteFromInventoryDB(trans, GetGUID().GetCounter());
}

ItemTemplate const* Item::GetTemplate() const
{
    return sObjectMgr->GetItemTemplate(GetEntry());
}

Player* Item::GetOwner()const
{
    return ObjectAccessor::FindPlayer(GetOwnerGUID());
}

// Legacy / Shortcut
uint32 Item::GetSkill()
{
    return GetTemplate()->GetSkill();
}

uint32 Item::GetSpell()
{
    ItemTemplate const* proto = GetTemplate();

    switch (proto->Class)
    {
        case ITEM_CLASS_WEAPON:
            switch (proto->SubClass)
            {
                case ITEM_SUBCLASS_WEAPON_AXE:
                    return  196;
                case ITEM_SUBCLASS_WEAPON_AXE2:
                    return  197;
                case ITEM_SUBCLASS_WEAPON_BOW:
                    return  264;
                case ITEM_SUBCLASS_WEAPON_GUN:
                    return  266;
                case ITEM_SUBCLASS_WEAPON_MACE:
                    return  198;
                case ITEM_SUBCLASS_WEAPON_MACE2:
                    return  199;
                case ITEM_SUBCLASS_WEAPON_POLEARM:
                    return  200;
                case ITEM_SUBCLASS_WEAPON_SWORD:
                    return  201;
                case ITEM_SUBCLASS_WEAPON_SWORD2:
                    return  202;
                case ITEM_SUBCLASS_WEAPON_STAFF:
                    return  227;
                case ITEM_SUBCLASS_WEAPON_DAGGER:
                    return 1180;
                case ITEM_SUBCLASS_WEAPON_THROWN:
                    return 2567;
                case ITEM_SUBCLASS_WEAPON_SPEAR:
                    return 3386;
                case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                    return 5011;
                case ITEM_SUBCLASS_WEAPON_WAND:
                    return 5009;
                default:
                    return 0;
            }
        case ITEM_CLASS_ARMOR:
            switch (proto->SubClass)
            {
                case ITEM_SUBCLASS_ARMOR_CLOTH:
                    return 9078;
                case ITEM_SUBCLASS_ARMOR_LEATHER:
                    return 9077;
                case ITEM_SUBCLASS_ARMOR_MAIL:
                    return 8737;
                case ITEM_SUBCLASS_ARMOR_PLATE:
                    return  750;
                case ITEM_SUBCLASS_ARMOR_SHIELD:
                    return 9116;
                default:
                    return 0;
            }
    }
    return 0;
}

int32 Item::GenerateItemRandomPropertyId(uint32 item_id)
{
    ItemTemplate const* itemProto = sObjectMgr->GetItemTemplate(item_id);

    if (!itemProto)
        return 0;

    // item must have one from this field values not null if it can have random enchantments
    if ((!itemProto->RandomProperty) && (!itemProto->RandomSuffix))
        return 0;

    // item can have not null only one from field values
    if ((itemProto->RandomProperty) && (itemProto->RandomSuffix))
    {
        LOG_ERROR("sql.sql", "Item template {} have RandomProperty == {} and RandomSuffix == {}, but must have one from field =0", itemProto->ItemId, itemProto->RandomProperty, itemProto->RandomSuffix);
        return 0;
    }

    // RandomProperty case
    if (itemProto->RandomProperty)
    {
        uint32 randomPropId = GetItemEnchantMod(itemProto->RandomProperty);
        ItemRandomPropertiesEntry const* random_id = sItemRandomPropertiesStore.LookupEntry(randomPropId);
        if (!random_id)
        {
            LOG_ERROR("sql.sql", "Enchantment id #{} used but it doesn't have records in 'ItemRandomProperties.dbc'", randomPropId);
            return 0;
        }

        return random_id->ID;
    }
    // RandomSuffix case
    else
    {
        uint32 randomPropId = GetItemEnchantMod(itemProto->RandomSuffix);
        ItemRandomSuffixEntry const* random_id = sItemRandomSuffixStore.LookupEntry(randomPropId);
        if (!random_id)
        {
            LOG_ERROR("sql.sql", "Enchantment id #{} used but it doesn't have records in sItemRandomSuffixStore.", randomPropId);
            return 0;
        }

        return -int32(random_id->ID);
    }
}

void Item::SetItemRandomProperties(int32 randomPropId)
{
    if (!randomPropId)
        return;

    if (randomPropId > 0)
    {
        ItemRandomPropertiesEntry const* item_rand = sItemRandomPropertiesStore.LookupEntry(randomPropId);
        if (item_rand)
        {
            if (GetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID) != int32(item_rand->ID))
            {
                SetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID, item_rand->ID);
                SetState(ITEM_CHANGED, GetOwner());
            }
            for (uint32 i = PROP_ENCHANTMENT_SLOT_0; i < MAX_ENCHANTMENT_SLOT; ++i)
                SetEnchantment(EnchantmentSlot(i), item_rand->Enchantment[i - PROP_ENCHANTMENT_SLOT_0], 0, 0);
        }
    }
    else
    {
        ItemRandomSuffixEntry const* item_rand = sItemRandomSuffixStore.LookupEntry(-randomPropId);
        if (item_rand)
        {
            if (GetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID) != -int32(item_rand->ID) ||
                    !GetItemSuffixFactor())
            {
                SetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID, -int32(item_rand->ID));
                UpdateItemSuffixFactor();
                SetState(ITEM_CHANGED, GetOwner());
            }

            for (uint32 i = PROP_ENCHANTMENT_SLOT_0; i < MAX_ENCHANTMENT_SLOT; ++i)
                SetEnchantment(EnchantmentSlot(i), item_rand->Enchantment[i - PROP_ENCHANTMENT_SLOT_0], 0, 0);
        }
    }
}

void Item::UpdateItemSuffixFactor()
{
    uint32 suffixFactor = GenerateEnchSuffixFactor(GetEntry());
    if (GetItemSuffixFactor() == suffixFactor)
        return;
    SetUInt32Value(ITEM_FIELD_PROPERTY_SEED, suffixFactor);
}

void Item::SetState(ItemUpdateState state, Player* forplayer)
{
    if (uState == ITEM_NEW && state == ITEM_REMOVED)
    {
        // pretend the item never existed
        if (forplayer)
        {
            RemoveFromUpdateQueueOf(forplayer);
            forplayer->DeleteRefundReference(GetGUID());
        }
        delete this;
        return;
    }
    if (state != ITEM_UNCHANGED)
    {
        // new items must stay in new state until saved
        if (uState != ITEM_NEW)
            uState = state;
        if (forplayer)
            AddToUpdateQueueOf(forplayer);
    }
    else
    {
        // unset in queue
        // the item must be removed from the queue manually
        uQueuePos = -1;
        uState = ITEM_UNCHANGED;
    }
}

void Item::AddToUpdateQueueOf(Player* player)
{
    if (IsInUpdateQueue())
        return;

    ASSERT(player);

    if (player->GetGUID() != GetOwnerGUID())
    {
        LOG_DEBUG("entities.player.items", "Item::AddToUpdateQueueOf - Owner's guid ({}) and player's guid ({}) don't match!", GetOwnerGUID().ToString(), player->GetGUID().ToString());
        return;
    }

    if (player->m_itemUpdateQueueBlocked)
        return;

    player->m_itemUpdateQueue.push_back(this);
    uQueuePos = player->m_itemUpdateQueue.size() - 1;
}

void Item::RemoveFromUpdateQueueOf(Player* player)
{
    if (!IsInUpdateQueue())
        return;

    ASSERT(player);

    if (player->GetGUID() != GetOwnerGUID())
    {
        LOG_DEBUG("entities.player.items", "Item::RemoveFromUpdateQueueOf - Owner's guid ({}) and player's guid ({}) don't match!", GetOwnerGUID().ToString(), player->GetGUID().ToString());
        return;
    }

    if (player->m_itemUpdateQueueBlocked)
        return;

    player->m_itemUpdateQueue[uQueuePos] = nullptr;
    uQueuePos = -1;
}

uint8 Item::GetBagSlot() const
{
    return m_container ? m_container->GetSlot() : uint8(INVENTORY_SLOT_BAG_0);
}

bool Item::IsEquipped() const
{
    return !IsInBag() && m_slot < EQUIPMENT_SLOT_END;
}

bool Item::CanBeTraded(bool mail, bool trade) const
{
    if ((!mail || !IsBoundAccountWide()) && (IsSoulBound() && (!IsBOPTradable() || !trade)))
        return false;

    if (IsBag() && (Player::IsBagPos(GetPos()) || !((Bag const*)this)->IsEmpty()))
        return false;

    if (Player* owner = GetOwner())
    {
        if (owner->CanUnequipItem(GetPos(), false) != EQUIP_ERR_OK)
            return false;

        // Xinef: check if item is looted now
        if (owner->GetLootGUID() == GetGUID())
            return false;
    }

    if (IsBoundByTempEnchant()) // pussywizard
        return false;

    if ((!mail || !IsBoundAccountWide()) && IsBoundByEnchant())
        return false;

    return true;
}

bool Item::HasEnchantRequiredSkill(Player const* player) const
{
    // Check all enchants for required skill
    for (uint32 enchant_slot = PERM_ENCHANTMENT_SLOT; enchant_slot < MAX_ENCHANTMENT_SLOT; ++enchant_slot)
        if (uint32 enchant_id = GetEnchantmentId(EnchantmentSlot(enchant_slot)))
            if (SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id))
                if (enchantEntry->requiredSkill && player->GetSkillValue(enchantEntry->requiredSkill) < enchantEntry->requiredSkillValue)
                    return false;

    return true;
}

uint32 Item::GetEnchantRequiredLevel() const
{
    uint32 level = 0;

    // Check all enchants for required level
    for (uint32 enchant_slot = PERM_ENCHANTMENT_SLOT; enchant_slot < MAX_ENCHANTMENT_SLOT; ++enchant_slot)
        if (uint32 enchant_id = GetEnchantmentId(EnchantmentSlot(enchant_slot)))
            if (SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id))
                if (enchantEntry->requiredLevel > level)
                    level = enchantEntry->requiredLevel;

    return level;
}

bool Item::IsBoundByEnchant() const
{
    // Check all enchants for soulbound
    for (uint32 enchant_slot = PERM_ENCHANTMENT_SLOT; enchant_slot < MAX_ENCHANTMENT_SLOT; ++enchant_slot)
        if (uint32 enchant_id = GetEnchantmentId(EnchantmentSlot(enchant_slot)))
            if (SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id))
                if (enchantEntry->slot & ENCHANTMENT_CAN_SOULBOUND)
                    return true;
    return false;
}

///新增避免随机附魔宕机
bool Item::IsEnchanted() const
{
    for (uint32 enchant_slot = PERM_ENCHANTMENT_SLOT; enchant_slot < MAX_ENCHANTMENT_SLOT; ++enchant_slot)
    {
        if (uint32 enchant_id = GetEnchantmentId(EnchantmentSlot(enchant_slot)))
        {
            return true; // 如果找到任何一个附魔ID，表示物品已经被附魔
        }
    }
    return false; // 遍历完所有附魔槽都没有找到附魔ID，表示物品未被附魔
}

bool Item::IsBoundByTempEnchant() const
{
    if (uint32 enchant_id = GetEnchantmentId(TEMP_ENCHANTMENT_SLOT))
        if (SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id))
            if (enchantEntry->slot & ENCHANTMENT_CAN_SOULBOUND)
                return true;
    return false;
}

InventoryResult Item::CanBeMergedPartlyWith(ItemTemplate const* proto) const
{
    // not allow merge looting currently items
    if (m_lootGenerated)
        return EQUIP_ERR_ALREADY_LOOTED;

    // check item type
    if (GetEntry() != proto->ItemId)
        return EQUIP_ERR_ITEM_CANT_STACK;

    // check free space (full stacks can't be target of merge
    if (GetCount() >= proto->GetMaxStackSize())
        return EQUIP_ERR_ITEM_CANT_STACK;

    return EQUIP_ERR_OK;
}

bool Item::IsFitToSpellRequirements(SpellInfo const* spellInfo) const
{
    ItemTemplate const* proto = GetTemplate();

    if (spellInfo->EquippedItemClass != -1)                 // -1 == any item class
    {
        // Special case - accept vellum for armor/weapon requirements
        if ((spellInfo->EquippedItemClass == ITEM_CLASS_ARMOR && proto->IsArmorVellum())
                || (spellInfo->EquippedItemClass == ITEM_CLASS_WEAPON && proto->IsWeaponVellum()))
            if (spellInfo->IsAbilityOfSkillType(SKILL_ENCHANTING)) // only for enchanting spells
                return true;

        if (spellInfo->EquippedItemClass != int32(proto->Class))
            return false;                                   //  wrong item class

        if (spellInfo->EquippedItemSubClassMask != 0)        // 0 == any subclass
        {
            if ((spellInfo->EquippedItemSubClassMask & (1 << proto->SubClass)) == 0)
                return false;                               // subclass not present in mask
        }
    }

    if (spellInfo->EquippedItemInventoryTypeMask != 0)       // 0 == any inventory type
    {
        // Special case - accept weapon type for main and offhand requirements
        if (proto->InventoryType == INVTYPE_WEAPON &&
                (spellInfo->EquippedItemInventoryTypeMask & (1 << INVTYPE_WEAPONMAINHAND) ||
                 spellInfo->EquippedItemInventoryTypeMask & (1 << INVTYPE_WEAPONOFFHAND)))
            return true;
        else if ((spellInfo->EquippedItemInventoryTypeMask & (1 << proto->InventoryType)) == 0)
            return false;                                   // inventory type not present in mask
    }

    return true;
}

void Item::SetEnchantment(EnchantmentSlot slot, uint32 id, uint32 duration, uint32 charges, ObjectGuid caster /*= ObjectGuid::Empty*/)
{
    // Better lost small time at check in comparison lost time at item save to DB.
    if ((GetEnchantmentId(slot) == id) && (GetEnchantmentDuration(slot) == duration) && (GetEnchantmentCharges(slot) == charges))
        return;

    Player* owner = GetOwner();
    if (slot < MAX_INSPECTED_ENCHANTMENT_SLOT)
    {
        if (uint32 oldEnchant = GetEnchantmentId(slot))
            owner->GetSession()->SendEnchantmentLog(GetOwnerGUID(), ObjectGuid::Empty, GetEntry(), oldEnchant);

        if (id)
            owner->GetSession()->SendEnchantmentLog(GetOwnerGUID(), caster, GetEntry(), id);
    }

    SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot * MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_ID_OFFSET, id);
    SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot * MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_DURATION_OFFSET, duration);
    SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot * MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_CHARGES_OFFSET, charges);
    SetState(ITEM_CHANGED, owner);
}
void Item::SetEnchantmentDuration(EnchantmentSlot slot, uint32 duration, Player* owner)
{
    if (GetEnchantmentDuration(slot) == duration)
        return;

    SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot * MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_DURATION_OFFSET, duration);
    SetState(ITEM_CHANGED, owner);
    // Cannot use GetOwner() here, has to be passed as an argument to avoid freeze due to hashtable locking
}

void Item::SetEnchantmentCharges(EnchantmentSlot slot, uint32 charges)
{
    if (GetEnchantmentCharges(slot) == charges)
        return;

    SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot * MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_CHARGES_OFFSET, charges);
    SetState(ITEM_CHANGED, GetOwner());
}

void Item::ClearEnchantment(EnchantmentSlot slot)
{
    if (!GetEnchantmentId(slot))
        return;

    for (uint8 x = 0; x < MAX_SPELL_ITEM_ENCHANTMENT_EFFECTS; ++x)
        SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot * MAX_ENCHANTMENT_OFFSET + x, 0);
    SetState(ITEM_CHANGED, GetOwner());
}

bool Item::GemsFitSockets() const
{
    for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT + MAX_GEM_SOCKETS; ++enchant_slot)
    {
        uint8 SocketColor = GetTemplate()->Socket[enchant_slot - SOCK_ENCHANTMENT_SLOT].Color;

        if (!SocketColor) // no socket slot
            continue;

        uint32 enchant_id = GetEnchantmentId(EnchantmentSlot(enchant_slot));
        if (!enchant_id) // no gems on this socket
            return false;

        SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
        if (!enchantEntry) // invalid gem id on this socket
            return false;

        uint8 GemColor = 0;

        uint32 gemid = enchantEntry->GemID;
        if (gemid)
        {
            ItemTemplate const* gemProto = sObjectMgr->GetItemTemplate(gemid);
            if (gemProto)
            {
                GemPropertiesEntry const* gemProperty = sGemPropertiesStore.LookupEntry(gemProto->GemProperties);
                if (gemProperty)
                    GemColor = gemProperty->color;
            }
        }

        if (!(GemColor & SocketColor)) // bad gem color on this socket
            return false;
    }
    return true;
}

bool Item::HasSocket() const
{
    // There can only be one socket added, and it's always in slot `PRISMATIC_ENCHANTMENT_SLOT`.
    //     Built-in sockets                        Socket from upgrade
    return this->GetTemplate()->Socket[0].Color || this->GetEnchantmentId(EnchantmentSlot(PRISMATIC_ENCHANTMENT_SLOT));
}

uint8 Item::GetGemCountWithID(uint32 GemID) const
{
    uint8 count = 0;
    for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT + MAX_GEM_SOCKETS; ++enchant_slot)
    {
        uint32 enchant_id = GetEnchantmentId(EnchantmentSlot(enchant_slot));
        if (!enchant_id)
            continue;

        SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
        if (!enchantEntry)
            continue;

        if (GemID == enchantEntry->GemID)
            ++count;
    }
    return count;
}

uint8 Item::GetGemCountWithLimitCategory(uint32 limitCategory) const
{
    uint8 count = 0;
    for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT + MAX_GEM_SOCKETS; ++enchant_slot)
    {
        uint32 enchant_id = GetEnchantmentId(EnchantmentSlot(enchant_slot));
        if (!enchant_id)
            continue;

        SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
        if (!enchantEntry)
            continue;

        ItemTemplate const* gemProto = sObjectMgr->GetItemTemplate(enchantEntry->GemID);
        if (!gemProto)
            continue;

        if (gemProto->ItemLimitCategory == limitCategory)
            ++count;
    }
    return count;
}

bool Item::IsLimitedToAnotherMapOrZone(uint32 cur_mapId, uint32 cur_zoneId) const
{
    ItemTemplate const* proto = GetTemplate();
    return proto && ((proto->Map && proto->Map != cur_mapId) || (proto->Area && proto->Area != cur_zoneId));
}

void Item::SendUpdateSockets()
{
    WorldPacket data(SMSG_SOCKET_GEMS_RESULT, 8 + 4 + 4 + 4 + 4);
    data << GetGUID();
    for (uint32 i = SOCK_ENCHANTMENT_SLOT; i <= BONUS_ENCHANTMENT_SLOT; ++i)
        data << uint32(GetEnchantmentId(EnchantmentSlot(i)));

    GetOwner()->GetSession()->SendPacket(&data);
}

// Though the client has the information in the item's data field,
// we have to send SMSG_ITEM_TIME_UPDATE to display the remaining
// time.
void Item::SendTimeUpdate(Player* owner)
{
    uint32 duration = GetUInt32Value(ITEM_FIELD_DURATION);
    if (!duration)
        return;

    WorldPacket data(SMSG_ITEM_TIME_UPDATE, (8 + 4));
    data << GetGUID();
    data << uint32(duration);
    owner->GetSession()->SendPacket(&data);
}

Item* Item::CreateItem(uint32 item, uint32 count, Player const* player, bool clone, uint32 randomPropertyId, bool temp)
{
    if (count < 1)
        return nullptr;                                        //don't create item at zero count

    ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(item);
    if (pProto)
    {
        if (count > pProto->GetMaxStackSize())
            count = pProto->GetMaxStackSize();

        ASSERT_NODEBUGINFO(count != 0 && "pProto->Stackable == 0 but checked at loading already");

        Item* pItem = NewItemOrBag(pProto);
        uint32 guid = temp ? 0xFFFFFFFF : sObjectMgr->GetGenerator<HighGuid::Item>().Generate();
        if (pItem->Create(guid, item, player))
        {
            pItem->SetCount(count);
            if (!clone)
                pItem->SetItemRandomProperties(randomPropertyId ? randomPropertyId : Item::GenerateItemRandomPropertyId(item));
            else if (randomPropertyId)
                pItem->SetItemRandomProperties(randomPropertyId);
            return pItem;
        }
        else
            delete pItem;
    }
    else
        ABORT();
    return nullptr;
}

Item* Item::CloneItem(uint32 count, Player const* player) const
{
    // player CAN be nullptr in which case we must not update random properties because that accesses player's item update queue
    Item* newItem = CreateItem(GetEntry(), count, player, true, player ? GetItemRandomPropertyId() : 0);
    if (!newItem)
        return nullptr;

    newItem->SetUInt32Value(ITEM_FIELD_CREATOR,      GetUInt32Value(ITEM_FIELD_CREATOR));
    newItem->SetUInt32Value(ITEM_FIELD_GIFTCREATOR,  GetUInt32Value(ITEM_FIELD_GIFTCREATOR));
    newItem->SetUInt32Value(ITEM_FIELD_FLAGS,        GetUInt32Value(ITEM_FIELD_FLAGS) & ~(ITEM_FIELD_FLAG_REFUNDABLE | ITEM_FIELD_FLAG_BOP_TRADEABLE));
    newItem->SetUInt32Value(ITEM_FIELD_DURATION,     GetUInt32Value(ITEM_FIELD_DURATION));
    return newItem;
}

bool Item::IsBindedNotWith(Player const* player) const
{
    // not binded item
    if (!IsSoulBound())
        return false;

    // own item
    if (GetOwnerGUID() == player->GetGUID())
        return false;

    if (IsBOPTradable())
        if (allowedGUIDs.find(player->GetGUID()) != allowedGUIDs.end())
            return false;

    // BOA item case
    if (IsBoundAccountWide())
        return false;

    return true;
}

void Item::BuildUpdate(UpdateDataMapType& data_map, UpdatePlayerSet&)
{
    if (Player* owner = GetOwner())
        BuildFieldsUpdate(owner, data_map);
    ClearUpdateMask(false);
}

void Item::AddToObjectUpdate()
{
    if (Player* owner = GetOwner())
        owner->GetMap()->AddUpdateObject(this);
}

void Item::RemoveFromObjectUpdate()
{
    if (Player* owner = GetOwner())
        owner->GetMap()->RemoveUpdateObject(this);
}

void Item::SaveRefundDataToDB()
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEM_REFUND_INSTANCE);
    stmt->SetData(0, GetGUID().GetCounter());
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_ITEM_REFUND_INSTANCE);
    stmt->SetData(0, GetGUID().GetCounter());
    stmt->SetData(1, GetRefundRecipient());
    stmt->SetData(2, GetPaidMoney());
    stmt->SetData(3, uint16(GetPaidExtendedCost()));
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);
}

void Item::DeleteRefundDataFromDB(CharacterDatabaseTransaction* trans)
{
    if (trans)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEM_REFUND_INSTANCE);
        stmt->SetData(0, GetGUID().GetCounter());
        (*trans)->Append(stmt);
    }
}

void Item::SetNotRefundable(Player* owner, bool changestate /*=true*/, CharacterDatabaseTransaction* trans /*=nullptr*/)
{
    if (!IsRefundable())
        return;

    RemoveFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_REFUNDABLE);
    // Following is not applicable in the trading procedure
    if (changestate)
        SetState(ITEM_CHANGED, owner);

    SetRefundRecipient(0);
    SetPaidMoney(0);
    SetPaidExtendedCost(0);
    DeleteRefundDataFromDB(trans);

    owner->DeleteRefundReference(GetGUID());
}

void Item::UpdatePlayedTime(Player* owner)
{
    /*  Here we update our played time
        We simply add a number to the current played time,
        based on the time elapsed since the last update hereof.
    */
    // Get current played time
    uint32 current_playtime = GetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME);
    // Calculate time elapsed since last played time update
    time_t curtime = GameTime::GetGameTime().count();
    uint32 elapsed = uint32(curtime - m_lastPlayedTimeUpdate);
    uint32 new_playtime = current_playtime + elapsed;
    // Check if the refund timer has expired yet
    if (new_playtime <= 2 * HOUR)
    {
        // No? Proceed.
        // Update the data field
        SetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME, new_playtime);
        // Flag as changed to get saved to DB
        SetState(ITEM_CHANGED, owner);
        // Speaks for itself
        m_lastPlayedTimeUpdate = curtime;
        return;
    }
    // Yes
    SetNotRefundable(owner);
}

uint32 Item::GetPlayedTime()
{
    time_t curtime = GameTime::GetGameTime().count();
    uint32 elapsed = uint32(curtime - m_lastPlayedTimeUpdate);
    return GetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME) + elapsed;
}

bool Item::IsRefundExpired()
{
    return (GetPlayedTime() > 2 * HOUR);
}

void Item::SetSoulboundTradeable(AllowedLooterSet& allowedLooters)
{
    SetFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_BOP_TRADEABLE);
    allowedGUIDs = allowedLooters;
}

void Item::ClearSoulboundTradeable(Player* currentOwner)
{
    RemoveFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_BOP_TRADEABLE);
    if (allowedGUIDs.empty())
        return;

    allowedGUIDs.clear();
    SetState(ITEM_CHANGED, currentOwner);
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEM_BOP_TRADE);
    stmt->SetData(0, GetGUID().GetCounter());
    CharacterDatabase.Execute(stmt);
}

bool Item::CheckSoulboundTradeExpire()
{
    // called from owner's update - GetOwner() MUST be valid
    if (GetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME) + 2 * HOUR < GetOwner()->GetTotalPlayedTime())
    {
        ClearSoulboundTradeable(GetOwner());
        return true; // remove from tradeable list
    }

    return false;
}

std::string Item::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << Object::GetDebugInfo() << "\n"
        << std::boolalpha
        << "Owner: " << GetOwnerGUID().ToString() << " Count: " << GetCount()
        << " BagSlot: " << std::to_string(GetBagSlot()) << " Slot: " << std::to_string(GetSlot()) << " Equipped: " << IsEquipped();
    return sstr.str();
}
