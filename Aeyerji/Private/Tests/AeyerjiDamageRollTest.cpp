#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Abilities/AbilityTeamUtils.h"
#include "Attributes/AeyerjiStatTuning.h"
#include "AeyerjiGameplayTags.h"
#include "GAS/AeyerjiDamageRules.h"
#include "GAS/AeyerjiGameplayEffectContext.h"
#include "GAS/ExecCalc_DamagePhysical.h"
#include "GAS/GE_DamagePhysical.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiDamageRollRangeTest,
	"Aeyerji.Combat.DamageRoll.RangeAndCritical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiDamageRollRangeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	constexpr float AverageDamage = 80.f;
	constexpr float Variance = 0.125f;
	constexpr float CritChance = 1.f;
	constexpr float CriticalMultiplier = 2.f;

	const FAeyerjiDamageRollResult MinimumRoll = UExecCalc_DamagePhysical::ResolveDamageRoll(
		AverageDamage,
		Variance,
		0.f,
		CriticalMultiplier,
		true,
		false,
		0.f,
		0.f);
	TestTrue(TEXT("Minimum variance roll is 70."), FMath::IsNearlyEqual(MinimumRoll.DamageBeforeMitigation, 70.f));
	TestFalse(TEXT("Critical is blocked when the rule is absent."), MinimumRoll.bWasCritical);

	const FAeyerjiDamageRollResult AverageRoll = UExecCalc_DamagePhysical::ResolveDamageRoll(
		AverageDamage,
		Variance,
		0.f,
		CriticalMultiplier,
		true,
		false,
		0.5f,
		0.f);
	TestTrue(TEXT("Middle variance roll remains the average."), FMath::IsNearlyEqual(AverageRoll.DamageBeforeMitigation, 80.f));

	const FAeyerjiDamageRollResult MaximumRoll = UExecCalc_DamagePhysical::ResolveDamageRoll(
		AverageDamage,
		Variance,
		0.f,
		CriticalMultiplier,
		true,
		false,
		1.f,
		0.f);
	TestTrue(TEXT("Maximum variance roll is 90."), FMath::IsNearlyEqual(MaximumRoll.DamageBeforeMitigation, 90.f));

	const FAeyerjiDamageRollResult MinimumCritical = UExecCalc_DamagePhysical::ResolveDamageRoll(
		AverageDamage,
		Variance,
		CritChance,
		CriticalMultiplier,
		true,
		true,
		0.f,
		0.f);
	TestTrue(TEXT("Minimum critical roll doubles after variance."), FMath::IsNearlyEqual(MinimumCritical.DamageBeforeMitigation, 140.f));
	TestTrue(TEXT("Critical is reported when the rule and roll allow it."), MinimumCritical.bWasCritical);

	const FAeyerjiDamageRollResult MaximumCritical = UExecCalc_DamagePhysical::ResolveDamageRoll(
		AverageDamage,
		Variance,
		CritChance,
		CriticalMultiplier,
		true,
		true,
		1.f,
		0.f);
	TestTrue(TEXT("Maximum critical roll doubles after variance."), FMath::IsNearlyEqual(MaximumCritical.DamageBeforeMitigation, 180.f));

	const FAeyerjiDamageRollResult NoVariance = UExecCalc_DamagePhysical::ResolveDamageRoll(
		AverageDamage,
		Variance,
		0.f,
		CriticalMultiplier,
		false,
		false,
		1.f,
		0.f);
	TestTrue(TEXT("No variance rule keeps exact average damage."), FMath::IsNearlyEqual(NoVariance.DamageBeforeMitigation, AverageDamage));

	const FAeyerjiDamageRollResult NoCritRule = UExecCalc_DamagePhysical::ResolveDamageRoll(
		AverageDamage,
		Variance,
		CritChance,
		CriticalMultiplier,
		false,
		false,
		0.f,
		0.f);
	TestFalse(TEXT("No crit rule prevents critical hits."), NoCritRule.bWasCritical);

	TestFalse(TEXT("Dodge chance is ignored when the damage source did not opt in."),
		UExecCalc_DamagePhysical::ResolveDodge(false, 1.f, 0.f));
	TestTrue(TEXT("Dodge resolves when the source opted in and the target roll succeeds."),
		UExecCalc_DamagePhysical::ResolveDodge(true, 0.25f, 0.10f));
	TestFalse(TEXT("Dodge fails when the roll is above the target chance."),
		UExecCalc_DamagePhysical::ResolveDodge(true, 0.25f, 0.50f));

	const FAeyerjiCombatLimitsTuning DefaultCombatLimits;
	TestTrue(TEXT("Dodge cap defaults to 75 percent."),
		FMath::IsNearlyEqual(DefaultCombatLimits.GetSafeMaxDodgeChance(), 0.75f));

	FAeyerjiCombatLimitsTuning InvalidCombatLimits;
	InvalidCombatLimits.MaxDodgeChance = 1.f;
	TestTrue(TEXT("Dodge cap clamps editor values below guaranteed avoidance."),
		FMath::IsNearlyEqual(InvalidCombatLimits.GetSafeMaxDodgeChance(), 0.95f));

	TestTrue(TEXT("Persistent and per-hit armor penetration add under the cap."),
		FMath::Abs(UExecCalc_DamagePhysical::ResolveArmorPenetration(0.20f, 0.15f, 0.75f) - 0.35f) < 0.001f);
	TestTrue(TEXT("Armor penetration obeys the global cap."),
		FMath::Abs(UExecCalc_DamagePhysical::ResolveArmorPenetration(0.60f, 0.40f, 0.75f) - 0.75f) < 0.001f);

	const float SoftCapReduction = UExecCalc_DamagePhysical::ResolveArmorDamageReduction(
		500.f, 1500.f, 500.f, 0.0001f, 0.75f);
	const float JustPastSoftCapReduction = UExecCalc_DamagePhysical::ResolveArmorDamageReduction(
		500.001f, 1500.f, 500.f, 0.0001f, 0.75f);
	TestTrue(TEXT("Armor tail starts at the configured curve value instead of a hard-coded fifty percent."),
		FMath::IsNearlyEqual(SoftCapReduction, 0.25f));
	TestTrue(TEXT("Armor mitigation remains continuous across a customized soft cap."),
		FMath::IsNearlyEqual(SoftCapReduction, JustPastSoftCapReduction, 0.0001f));
	TestTrue(TEXT("Armor tail cap cannot force mitigation below the soft-cap value."),
		FMath::IsNearlyEqual(
			UExecCalc_DamagePhysical::ResolveArmorDamageReduction(1000.f, 500.f, 500.f, 1.f, 0.1f),
			0.5f));

	TestTrue(TEXT("Life steal uses actual post-mitigation damage."),
		FMath::IsNearlyEqual(UExecCalc_DamagePhysical::ResolveLifeSteal(40.f, 0.25f, 100.f, true), 10.f));
	TestTrue(TEXT("Life steal cannot exceed missing health."),
		FMath::IsNearlyEqual(UExecCalc_DamagePhysical::ResolveLifeSteal(100.f, 0.25f, 5.f, true), 5.f));
	TestTrue(TEXT("Life steal is disabled when the source rule is absent."),
		FMath::IsNearlyZero(UExecCalc_DamagePhysical::ResolveLifeSteal(100.f, 0.25f, 100.f, false)));

	AActor* UnteamedA = NewObject<AActor>(GetTransientPackage());
	AActor* UnteamedB = NewObject<AActor>(GetTransientPackage());
	TestFalse(TEXT("Two unknown team affiliations are not treated as friendly."),
		AbilityTeamUtils::AreOnSameTeam(UnteamedA, UnteamedB));
	TestTrue(TEXT("An actor is always considered friendly with itself."),
		AbilityTeamUtils::AreOnSameTeam(UnteamedA, UnteamedA));

	FGameplayEffectContextHandle DisabledContext(new FAeyerjiGameplayEffectContext());
	FGameplayEffectSpec DisabledSpec(GetDefault<UGE_DamagePhysical>(), DisabledContext, 1.f);
	FAeyerjiDamageRuleConfig DisabledRules;
	DisabledRules.ApplyToSpec(DisabledSpec);
	FGameplayTagContainer DisabledAssetTags;
	DisabledSpec.GetAllAssetTags(DisabledAssetTags);
	TestFalse(TEXT("A source with crit disabled never receives the CanCrit rule tag."),
		DisabledAssetTags.HasTagExact(AeyerjiTags::DamageRule_CanCrit));

	FGameplayEffectContextHandle EnabledContext(new FAeyerjiGameplayEffectContext());
	FGameplayEffectSpec EnabledSpec(GetDefault<UGE_DamagePhysical>(), EnabledContext, 1.f);
	FAeyerjiDamageRuleConfig EnabledRules;
	EnabledRules.bCanCrit = true;
	EnabledRules.bCanBeDodged = true;
	EnabledRules.bCanLifeSteal = true;
	EnabledRules.ApplyToSpec(EnabledSpec);
	FGameplayTagContainer EnabledAssetTags;
	EnabledSpec.GetAllAssetTags(EnabledAssetTags);
	TestTrue(TEXT("Enabled crit is represented only on the outgoing source spec."),
		EnabledAssetTags.HasTagExact(AeyerjiTags::DamageRule_CanCrit));
	TestTrue(TEXT("Enabled dodge is represented only on the outgoing source spec."),
		EnabledAssetTags.HasTagExact(AeyerjiTags::DamageRule_CanBeDodged));
	TestTrue(TEXT("Enabled life steal is represented only on the outgoing source spec."),
		EnabledAssetTags.HasTagExact(AeyerjiTags::DamageRule_CanLifeSteal));

	return true;
}

#endif
