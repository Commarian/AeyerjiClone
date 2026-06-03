#include "Abilities/Blink/GABlink.h"
#include "AbilitySystemComponent.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"          // optional - visualise trace
#include "NativeGameplayTags.h"
#include "Abilities/AeyerjiAbilityTuning.h"
#include "Attributes/AttributeSet_Ranges.h"
#include "Attributes/AeyerjiAttributeSet.h"

/* ───────────────────────────────────────────────────────────────
 *  Define the tags once here; no extern/DEFINE macro gymnastics.
 *  This removes the unresolved‐symbol (LNK2001) errors you saw.
 * ─────────────────────────────────────────────────────────────── */
namespace BlinkTags
{
	// Resolve tags lazily so packaged startup does not depend on file-scope initialization.
	const FGameplayTag& AbilityTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Ability.Blink"));
		return Tag;
	}

	const FGameplayTag& CooldownTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Blink"));
		return Tag;
	}

	/* GameplayCues (VFX/SFX) */
	const FGameplayTag& GC_BlinkOut()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Blink.Out"));
		return Tag;
	}

	const FGameplayTag& GC_BlinkIn()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Blink.In"));
		return Tag;
	}
}
/* ------------------------------------------------------------ */

UGABlink::UGABlink()
{
	/* Asset (ability) tag – must pass a container to SetAssetTags in UE 5.6 :contentReference[oaicite:5]{index=5} */
	{
		FGameplayTagContainer AssetTags;
		AssetTags.AddTag(BlinkTags::AbilityTag());
		SetAssetTags(AssetTags);
	}

	/* Local helpers */
	CooldownTags.AddTag(BlinkTags::CooldownTag());
	BlinkOutCue = BlinkTags::GC_BlinkOut();
	BlinkInCue  = BlinkTags::GC_BlinkIn();

	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

float UGABlink::GetMaxBlinkRange(const UAbilitySystemComponent* ASC) const
{
	float Range = MaxBlinkDistance;

	if (const FAeyerjiAbilityTableRow* Row = GetAbilityTuningRow(ASC))
	{
		Range = FMath::Max(Range, FMath::Max(Row->MaxRange, Row->PreviewRange));
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
