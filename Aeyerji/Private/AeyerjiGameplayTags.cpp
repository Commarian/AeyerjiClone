// AeyerjiGameplayTags.cpp
#include "AeyerjiGameplayTags.h"

// Register definitions
namespace AeyerjiTags
{
    UE_DEFINE_GAMEPLAY_TAG(State_Dead, "State.Dead");
    UE_DEFINE_GAMEPLAY_TAG(State_Dead_Cleansed, "State.Dead.Cleansed");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Death, "Ability.Death");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Primary, "Ability.Primary");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Primary_Melee_Basic,  "Ability.Primary.Melee.Basic");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Primary_Ranged_Basic, "Ability.Primary.Ranged.Basic");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Potion_Heal, "Ability.Potion.Heal");
    UE_DEFINE_GAMEPLAY_TAG(DamageType_Physical, "Damage.Type.Physical");
    UE_DEFINE_GAMEPLAY_TAG(DamageRule_UseVariance, "Damage.Rule.UseVariance");
    UE_DEFINE_GAMEPLAY_TAG(DamageRule_CanCrit, "Damage.Rule.CanCrit");
    UE_DEFINE_GAMEPLAY_TAG(DamageRule_CanBeDodged, "Damage.Rule.CanBeDodged");
    UE_DEFINE_GAMEPLAY_TAG(DamageRule_CanLifeSteal, "Damage.Rule.CanLifeSteal");
    UE_DEFINE_GAMEPLAY_TAG(DamageRule_CanTriggerOnHit, "Damage.Rule.CanTriggerOnHit");
    UE_DEFINE_GAMEPLAY_TAG(DamageRule_CanStagger, "Damage.Rule.CanStagger");
    UE_DEFINE_GAMEPLAY_TAG(AilmentType_Poisonous, "AilmentType.Poisonous");
    UE_DEFINE_GAMEPLAY_TAG(AilmentType_Corrupting, "AilmentType.Corrupting");
    UE_DEFINE_GAMEPLAY_TAG(AilmentType_Traumatizing, "AilmentType.Traumatizing");
    UE_DEFINE_GAMEPLAY_TAG(State_Ability_PrimaryMelee_WindUp,    "State.Ability.PrimaryMelee.WindUp");
    UE_DEFINE_GAMEPLAY_TAG(State_Ability_PrimaryMelee_HitWindow, "State.Ability.PrimaryMelee.HitWindow");
    UE_DEFINE_GAMEPLAY_TAG(State_Ability_PrimaryMelee_Recovery,  "State.Ability.PrimaryMelee.Recovery");
    UE_DEFINE_GAMEPLAY_TAG(State_Ability_PrimaryMelee_Cancelled, "State.Ability.PrimaryMelee.Cancelled");
    UE_DEFINE_GAMEPLAY_TAG(State_Ability_PrimaryMelee_BlockMovement, "State.Ability.PrimaryMelee.BlockMovement");
    UE_DEFINE_GAMEPLAY_TAG(State_Ability_Casting, "State.Ability.Casting");

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
    UE_DEFINE_GAMEPLAY_TAG(Cooldown_Potion,                "Cooldown.Potion");
    UE_DEFINE_GAMEPLAY_TAG(SBC_Cost_Mana,                  "SetByCaller.Cost.Mana");
    UE_DEFINE_GAMEPLAY_TAG(SBC_CooldownSeconds,            "SetByCaller.Cooldown.Seconds");
    UE_DEFINE_GAMEPLAY_TAG(SBC_Heal_Instant,               "SetByCaller.Heal.Instant");
    UE_DEFINE_GAMEPLAY_TAG(SBC_Damage_Instant,             "SetByCaller.Damage.Instant");
    UE_DEFINE_GAMEPLAY_TAG(SBC_Damage_Variance,            "SetByCaller.Damage.Variance");
    UE_DEFINE_GAMEPLAY_TAG(SBC_Damage_CriticalMultiplier,   "SetByCaller.Damage.CriticalMultiplier");
    UE_DEFINE_GAMEPLAY_TAG(SBC_Damage_StaggerMultiplier,    "SetByCaller.Damage.StaggerMultiplier");
    UE_DEFINE_GAMEPLAY_TAG(SBC_ArmorShred,                  "SetByCaller.ArmorShred");
    UE_DEFINE_GAMEPLAY_TAG(SBC_ArmorPenetration,            "SetByCaller.ArmorPenetration");
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
    UE_DEFINE_GAMEPLAY_TAG(Loot_Source_RiftBase,           "Loot.Source.Rift.Base");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Source_RiftTimed,          "Loot.Source.Rift.Timed");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Source_RiftFlawless,       "Loot.Source.Rift.Flawless");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Pity_GenericLegendary,     "Loot.Pity.GenericLegendary");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Pity_BossUnique,           "Loot.Pity.BossUnique");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Pity_SetPiece,             "Loot.Pity.SetPiece");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Pity_ProgressionKey,       "Loot.Pity.ProgressionKey");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Pity_RareMaterial,         "Loot.Pity.RareMaterial");
    UE_DEFINE_GAMEPLAY_TAG(Loot_Pity_FirstClear,           "Loot.Pity.FirstClear");
    
    UE_DEFINE_GAMEPLAY_TAG(SBC_Stun_Duration,          "SetByCaller.Stun.Duration");
    UE_DEFINE_GAMEPLAY_TAG(State_CrowdControl_Stunned, "State.CrowdControl.Stunned");
    UE_DEFINE_GAMEPLAY_TAG(SBC_Stagger_Duration,          "SetByCaller.Stagger.Duration");
    UE_DEFINE_GAMEPLAY_TAG(State_CrowdControl_Staggered,   "State.CrowdControl.Staggered");
    UE_DEFINE_GAMEPLAY_TAG(Event_AI_CrowdControl_Stunned,   "Event.AI.CrowdControl.Stunned");
    UE_DEFINE_GAMEPLAY_TAG(Event_AI_CrowdControl_Staggered, "Event.AI.CrowdControl.Staggered");
    UE_DEFINE_GAMEPLAY_TAG(Event_AI_CrowdControl_Cleared,   "Event.AI.CrowdControl.Cleared");
    UE_DEFINE_GAMEPLAY_TAG(Event_Combat_DamageDealt,       "Event.Combat.DamageDealt");
    UE_DEFINE_GAMEPLAY_TAG(Event_Combat_DamageReceived,    "Event.Combat.DamageReceived");
    UE_DEFINE_GAMEPLAY_TAG(Event_Combat_OnHit,              "Event.Combat.OnHit");
    UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_Hit_Physical, "GameplayCue.Combat.Hit.Physical");
    UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_Hit_Critical, "GameplayCue.Combat.Hit.Critical");
    UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_Hit_Dodged,   "GameplayCue.Combat.Hit.Dodged");
    UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_Hit_Staggered,"GameplayCue.Combat.Hit.Staggered");
    UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_Hit_Killing,  "GameplayCue.Combat.Hit.Killing");
}
