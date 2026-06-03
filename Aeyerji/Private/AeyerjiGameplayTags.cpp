// AeyerjiGameplayTags.cpp
#include "AeyerjiGameplayTags.h"

// Register definitions
namespace AeyerjiTags
{
    UE_DEFINE_GAMEPLAY_TAG(State_Dead, "State.Dead");
    UE_DEFINE_GAMEPLAY_TAG(State_Dead_Cleansed, "State.Dead.Cleansed");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Primary, "Ability.Primary");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Primary_Melee_Basic,  "Ability.Primary.Melee.Basic");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Primary_Ranged_Basic, "Ability.Primary.Ranged.Basic");
    UE_DEFINE_GAMEPLAY_TAG(DamageType_Physical, "Damage.Type.Physical");
    UE_DEFINE_GAMEPLAY_TAG(AilmentType_Poisonous, "AilmentType.Poisonous");
    UE_DEFINE_GAMEPLAY_TAG(AilmentType_Corrupting, "AilmentType.Corrupting");
    UE_DEFINE_GAMEPLAY_TAG(AilmentType_Traumatizing, "AilmentType.Traumatizing");
    UE_DEFINE_GAMEPLAY_TAG(State_Ability_PrimaryMelee_WindUp,    "State.Ability.PrimaryMelee.WindUp");
    UE_DEFINE_GAMEPLAY_TAG(State_Ability_PrimaryMelee_HitWindow, "State.Ability.PrimaryMelee.HitWindow");
    UE_DEFINE_GAMEPLAY_TAG(State_Ability_PrimaryMelee_Recovery,  "State.Ability.PrimaryMelee.Recovery");
    UE_DEFINE_GAMEPLAY_TAG(State_Ability_PrimaryMelee_Cancelled, "State.Ability.PrimaryMelee.Cancelled");
    UE_DEFINE_GAMEPLAY_TAG(State_Ability_PrimaryMelee_BlockMovement, "State.Ability.PrimaryMelee.BlockMovement");

    // SetByCaller tags used by the derived stats GE
    UE_DEFINE_GAMEPLAY_TAG(SBC_PrimaryDerived_HPMax,           "SetByCaller.PrimaryDerived.HPMax");
    UE_DEFINE_GAMEPLAY_TAG(SBC_PrimaryDerived_Armor,           "SetByCaller.PrimaryDerived.Armor");
    UE_DEFINE_GAMEPLAY_TAG(SBC_PrimaryDerived_AttackSpeed,     "SetByCaller.PrimaryDerived.AttackSpeed");
    UE_DEFINE_GAMEPLAY_TAG(SBC_PrimaryDerived_DodgeChance,     "SetByCaller.PrimaryDerived.DodgeChance");
    UE_DEFINE_GAMEPLAY_TAG(SBC_PrimaryDerived_SpellPower,      "SetByCaller.PrimaryDerived.SpellPower");
    UE_DEFINE_GAMEPLAY_TAG(SBC_PrimaryDerived_ManaMax,         "SetByCaller.PrimaryDerived.ManaMax");
    UE_DEFINE_GAMEPLAY_TAG(SBC_PrimaryDerived_ManaRegen,       "SetByCaller.PrimaryDerived.ManaRegen");
    UE_DEFINE_GAMEPLAY_TAG(SBC_PrimaryDerived_HPRegen,         "SetByCaller.PrimaryDerived.HPRegen");
    UE_DEFINE_GAMEPLAY_TAG(SBC_PrimaryDerived_AilmentDPS,      "SetByCaller.PrimaryDerived.AilmentDPS");
    UE_DEFINE_GAMEPLAY_TAG(SBC_PrimaryDerived_AilmentDuration, "SetByCaller.PrimaryDerived.AilmentDuration");

    // Cooldown + SetByCaller duration
    UE_DEFINE_GAMEPLAY_TAG(Event_Combat_Melee_TraceWindow, "Event.Combat.Melee.TraceWindow");
    UE_DEFINE_GAMEPLAY_TAG(Event_PrimaryAttack_Completed,  "Event.PrimaryAttack.Completed");
    UE_DEFINE_GAMEPLAY_TAG(Event_External_Target,          "Event.External.Target");
    UE_DEFINE_GAMEPLAY_TAG(Cooldown_PrimaryAttack,         "Cooldown.PrimaryAttack");
    UE_DEFINE_GAMEPLAY_TAG(SBC_Cost_Mana,                  "SetByCaller.Cost.Mana");
    UE_DEFINE_GAMEPLAY_TAG(SBC_CooldownSeconds,            "SetByCaller.Cooldown.Seconds");
    UE_DEFINE_GAMEPLAY_TAG(SBC_Damage_Instant,             "SetByCaller.Damage.Instant");
    UE_DEFINE_GAMEPLAY_TAG(SBC_CooldownDuration,           "SetByCaller.Cooldown.Duration");
    UE_DEFINE_GAMEPLAY_TAG(World_Teleporter_Unlocked,      "World.Teleporter.Unlocked");
    UE_DEFINE_GAMEPLAY_TAG(World_Quest_Flag,               "World.Quest.Flag");
    UE_DEFINE_GAMEPLAY_TAG(Run_State,                      "Run.State");
    UE_DEFINE_GAMEPLAY_TAG(Run_Level,                      "Run.Level");
    UE_DEFINE_GAMEPLAY_TAG(Run_Zone,                       "Run.Zone");
    UE_DEFINE_GAMEPLAY_TAG(Run_Result,                     "Run.Result");
    UE_DEFINE_GAMEPLAY_TAG(Run_Event_Started,              "Run.Event.Started");
    UE_DEFINE_GAMEPLAY_TAG(Run_Event_BossDefeated,         "Run.Event.BossDefeated");
    UE_DEFINE_GAMEPLAY_TAG(Run_Event_ObjectiveComplete,    "Run.Event.ObjectiveComplete");
    UE_DEFINE_GAMEPLAY_TAG(Run_Event_Completed,            "Run.Event.Completed");
    UE_DEFINE_GAMEPLAY_TAG(Run_Event_Failed,               "Run.Event.Failed");
    UE_DEFINE_GAMEPLAY_TAG(Run_Event_Abandoned,            "Run.Event.Abandoned");
    UE_DEFINE_GAMEPLAY_TAG(Run_Event_SpawnMoreEnemies_Done, "Run.Event.SpawnMoreEnemies.Done");
    UE_DEFINE_GAMEPLAY_TAG(Run_Difficulty_EnemyPressure,   "Run.Difficulty.EnemyPressure");
    UE_DEFINE_GAMEPLAY_TAG(World_Boss_Map1_Defeated,       "World.Boss.Map1.Defeated");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Source_NormalEnemy,        "Loot.Source.NormalEnemy");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Source_Elite,              "Loot.Source.Elite");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Source_MiniBoss,           "Loot.Source.MiniBoss");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Source_Boss,               "Loot.Source.Boss");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Source_Chest,              "Loot.Source.Chest");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Source_Event,              "Loot.Source.Event");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Pity_GenericLegendary,     "Loot.Pity.GenericLegendary");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Pity_BossUnique,           "Loot.Pity.BossUnique");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Pity_SetPiece,             "Loot.Pity.SetPiece");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Pity_ProgressionKey,       "Loot.Pity.ProgressionKey");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Pity_RareMaterial,         "Loot.Pity.RareMaterial");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Pity_FirstClear,           "Loot.Pity.FirstClear");
    
    UE_DEFINE_GAMEPLAY_TAG(SBC_Stun_Duration,          "SetByCaller.Stun.Duration");
    UE_DEFINE_GAMEPLAY_TAG(State_CrowdControl_Stunned, "State.CrowdControl.Stunned");
}
