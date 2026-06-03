#pragma once
#include "NativeGameplayTags.h"

namespace AeyerjiTags
{
	// Declare tags (exported so other modules can see them)
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead_Cleansed);
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Primary);
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Primary_Ranged_Basic);
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Primary_Melee_Basic);

	// Damage types
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DamageType_Physical); // Damage.Type.Physical

	// Ailment source types used to drive ailment application logic.
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AilmentType_Poisonous);    // AilmentType.Poisonous
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AilmentType_Corrupting);   // AilmentType.Corrupting
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AilmentType_Traumatizing); // AilmentType.Traumatizing
		
	// Add more as needed: Secondary attack, Status effects, etc.

    // Primary melee ability phases
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Ability_PrimaryMelee_WindUp);     // State.Ability.PrimaryMelee.WindUp
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Ability_PrimaryMelee_HitWindow);  // State.Ability.PrimaryMelee.HitWindow
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Ability_PrimaryMelee_Recovery);   // State.Ability.PrimaryMelee.Recovery
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Ability_PrimaryMelee_Cancelled);  // State.Ability.PrimaryMelee.Cancelled
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Ability_PrimaryMelee_BlockMovement); // State.Ability.PrimaryMelee.BlockMovement

    // SetByCaller tags for primary->derived stat GE
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_PrimaryDerived_HPMax);        // SetByCaller.PrimaryDerived.HPMax
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_PrimaryDerived_Armor);        // SetByCaller.PrimaryDerived.Armor
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_PrimaryDerived_AttackSpeed);  // SetByCaller.PrimaryDerived.AttackSpeed
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_PrimaryDerived_DodgeChance);  // SetByCaller.PrimaryDerived.DodgeChance
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_PrimaryDerived_SpellPower);   // SetByCaller.PrimaryDerived.SpellPower
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_PrimaryDerived_ManaMax);      // SetByCaller.PrimaryDerived.ManaMax
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_PrimaryDerived_ManaRegen);    // SetByCaller.PrimaryDerived.ManaRegen
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_PrimaryDerived_HPRegen);      // SetByCaller.PrimaryDerived.HPRegen
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_PrimaryDerived_AilmentDPS);   // SetByCaller.PrimaryDerived.AilmentDPS
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_PrimaryDerived_AilmentDuration); // SetByCaller.PrimaryDerived.AilmentDuration

    // Generic cooldown tag + SetByCaller duration for abilities that compute at runtime
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_Melee_TraceWindow);  // Event.Combat.Melee.TraceWindow
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_PrimaryAttack_Completed);  // Event.PrimaryAttack.Completed
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_External_Target);           // Event.External.Target
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_PrimaryAttack);           // Cooldown.PrimaryAttack
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_Cost_Mana);                   // SetByCaller.Cost.Mana
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_CooldownSeconds);             // SetByCaller.Cooldown.Seconds
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_Damage_Instant);              // SetByCaller.Damage.Instant
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_CooldownDuration);             // Deprecated: SetByCaller.Cooldown.Duration

	// Persistent shared world-state compatibility tags.
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(World_Teleporter_Unlocked);         // World.Teleporter.Unlocked
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(World_Quest_Flag);                  // World.Quest.Flag

	// Run director and world-state tags.
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Run_State);                         // Run.State
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Run_Level);                         // Run.Level
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Run_Zone);                          // Run.Zone
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Run_Result);                        // Run.Result
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Run_Event_Started);                 // Run.Event.Started
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Run_Event_BossDefeated);            // Run.Event.BossDefeated
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Run_Event_ObjectiveComplete);       // Run.Event.ObjectiveComplete
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Run_Event_Completed);               // Run.Event.Completed
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Run_Event_Failed);                  // Run.Event.Failed
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Run_Event_Abandoned);               // Run.Event.Abandoned
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Run_Event_SpawnMoreEnemies_Done);   // Run.Event.SpawnMoreEnemies.Done
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Run_Difficulty_EnemyPressure);      // Run.Difficulty.EnemyPressure
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(World_Boss_Map1_Defeated);          // World.Boss.Map1.Defeated

	// Loot source and pity tags used by C++ loot rules and StateTree nodes.
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Loot_Source_NormalEnemy);           // Loot.Source.NormalEnemy
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Loot_Source_Elite);                 // Loot.Source.Elite
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Loot_Source_MiniBoss);              // Loot.Source.MiniBoss
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Loot_Source_Boss);                  // Loot.Source.Boss
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Loot_Source_Chest);                 // Loot.Source.Chest
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Loot_Source_Event);                 // Loot.Source.Event
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Loot_Pity_GenericLegendary);        // Loot.Pity.GenericLegendary
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Loot_Pity_BossUnique);              // Loot.Pity.BossUnique
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Loot_Pity_SetPiece);                // Loot.Pity.SetPiece
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Loot_Pity_ProgressionKey);          // Loot.Pity.ProgressionKey
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Loot_Pity_RareMaterial);            // Loot.Pity.RareMaterial
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Loot_Pity_FirstClear);              // Loot.Pity.FirstClear

	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_Stun_Duration);             // SetByCaller.Stun.Duration
	AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_CrowdControl_Stunned);    // State.CrowdControl.Stunned
}



