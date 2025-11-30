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
    UE_DEFINE_GAMEPLAY_TAG(Cooldown_PrimaryAttack,         "Cooldown.PrimaryAttack");
    UE_DEFINE_GAMEPLAY_TAG(SBC_CooldownDuration,           "SetByCaller.Cooldown.Duration");
}
