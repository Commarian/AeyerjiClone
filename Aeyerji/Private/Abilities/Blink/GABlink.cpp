#include "Abilities/Blink/GABlink.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"          // optional - visualise trace
#include "NativeGameplayTags.h"
#include "Attributes/AttributeSet_Ranges.h"
#include "Attributes/AeyerjiAttributeSet.h"

/* ───────────────────────────────────────────────────────────────
 *  Define the tags once here; no extern/DEFINE macro gymnastics.
 *  This removes the unresolved‐symbol (LNK2001) errors you saw.
 * ─────────────────────────────────────────────────────────────── */
namespace BlinkTags
{
	// These must exist in DefaultGameplayTags.ini (or your Primary Data Table)
	const FGameplayTag AbilityTag  = FGameplayTag::RequestGameplayTag(TEXT("Ability.Blink"));
	const FGameplayTag CooldownTag = FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Blink"));

	/* GameplayCues (VFX/SFX) */
	const FGameplayTag GC_BlinkOut = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Blink.Out"));
	const FGameplayTag GC_BlinkIn  = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Blink.In"));
}
/* ------------------------------------------------------------ */

UGABlink::UGABlink()
{
	/* Asset (ability) tag – must pass a container to SetAssetTags in UE 5.6 :contentReference[oaicite:5]{index=5} */
	{
		FGameplayTagContainer AssetTags;
		AssetTags.AddTag(BlinkTags::AbilityTag);
		SetAssetTags(AssetTags);
	}

	/* Local helpers */
	CooldownTags.AddTag(BlinkTags::CooldownTag);
	BlinkOutCue = BlinkTags::GC_BlinkOut;
	BlinkInCue  = BlinkTags::GC_BlinkIn;

	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

float UGABlink::GetMaxBlinkRange(const UAbilitySystemComponent* ASC) const
{
	float Range = MaxBlinkDistance;

	if (BlinkConfig)
	{
		const FRichCurve* Curve = BlinkConfig->Tunables.RangeByLevel.GetRichCurveConst();
		if (Curve && Curve->GetNumKeys() > 0)
		{
			const int32 Level = (ASC && ASC->HasAttributeSetForAttribute(UAeyerjiAttributeSet::GetLevelAttribute()))
				? FMath::RoundToInt(ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetLevelAttribute()))
				: 1;
			const float CurveRange = Curve->Eval(static_cast<float>(Level), BlinkConfig->Tunables.MaxRange);
			Range = FMath::Max(Range, CurveRange);
		}
		else
		{
			Range = FMath::Max(Range, BlinkConfig->Tunables.MaxRange);
		}

		Range *= FMath::Max(0.0f, BlinkConfig->Tunables.RangeScalar);
	}

	if (ASC)
	{
		if (const UAttributeSet_Ranges* RangeSet = ASC->GetSet<UAttributeSet_Ranges>())
		{
			Range = FMath::Max(Range, RangeSet->GetBlinkRange());
		}
	}

	return FMath::Max(0.f, Range);
}

void UGABlink::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                               const FGameplayAbilityActorInfo* ActorInfo,
                               const FGameplayAbilityActivationInfo ActivationInfo,
                               const FGameplayEventData* /*TriggerEventData*/)
{
	/* CommitAbility ⇒ pays cost + applies cooldown GE */
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEnd*/true, /*bCancelled*/false);
		return;
	}

	if (UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
	{
		ASC->ExecuteGameplayCue(BlinkOutCue);           /* Out VFX/SFX :contentReference[oaicite:6]{index=6} */
	}

	ACharacter* Avatar = Cast<ACharacter>(ActorInfo->AvatarActor.Get());
	if (!Avatar)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	const float MaxRange = GetMaxBlinkRange(ActorInfo->AbilitySystemComponent.Get());
	const FVector Start = Avatar->GetActorLocation();
	const FVector End   = Start + Avatar->GetActorForwardVector() * MaxRange;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BlinkTrace), false, Avatar);
	

	if (UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
	{
		ASC->ExecuteGameplayCue(BlinkInCue);            /* In VFX/SFX */
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
