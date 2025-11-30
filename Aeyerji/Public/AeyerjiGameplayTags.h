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
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_PrimaryAttack);           // Cooldown.PrimaryAttack
    AEYERJI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SBC_CooldownDuration);             // SetByCaller.Cooldown.Duration
}



