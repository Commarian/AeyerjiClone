#include "GUI/W_AeyerjiFrontendShell.h"

#include "Algo/AllOf.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Frontend/AeyerjiFrontendSubsystem.h"
#include "Frontend/AeyerjiSessionSubsystem.h"
#include "GUI/AeyerjiStringLibrary.h"

namespace
{
	constexpr float MaxFrontendXPDisplay = 1000000000000.f;

	int64 FrontendXPInteger(const float Value)
	{
		return FMath::RoundToInt64(FMath::Clamp(
			FMath::IsFinite(Value) ? static_cast<double>(Value) : 0.0,
			0.0,
			static_cast<double>(MaxFrontendXPDisplay)));
	}

	FText GetFrontendOperationText(const EAeyerjiFrontendOperationState OperationState)
	{
		switch (OperationState)
		{
		case EAeyerjiFrontendOperationState::CreatingSession:
			return AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_CreatingParty"));
		case EAeyerjiFrontendOperationState::SearchingSessions:
			return AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_SearchingParties"));
		case EAeyerjiFrontendOperationState::JoiningSession:
			return AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_JoiningParty"));
		case EAeyerjiFrontendOperationState::LeavingSession:
			return AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_LeavingParty"));
		case EAeyerjiFrontendOperationState::Launching:
			return AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_Launching"));
		default:
			return FText::GetEmpty();
		}
	}

	FText GetLobbyProfileStateText(const EAeyerjiLobbyProfileState ProfileState)
	{
		return ProfileState == EAeyerjiLobbyProfileState::Verified
			? AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_ProfileVerified"))
			: AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_ProfileResolving"));
	}
}

void UW_AeyerjiFrontendShell::NativeConstruct()
{
	Super::NativeConstruct();
	BindNativeButtonHandlers();
	if (UAeyerjiFrontendSubsystem* Frontend = GetFrontendSubsystem())
	{
		Frontend->OnFrontendSnapshotChanged.AddUObject(this, &ThisClass::HandleFrontendSnapshot);
		Frontend->OnLobbySnapshotChanged.AddUObject(this, &ThisClass::HandleLobbySnapshot);
		Frontend->OnSessionResultsChanged.AddUObject(this, &ThisClass::HandleSessionResults);
		Frontend->OnFeedback.AddUObject(this, &ThisClass::HandleFeedback);
		Frontend->RefreshCurrentState();
	}
}

void UW_AeyerjiFrontendShell::NativeDestruct()
{
	if (UAeyerjiFrontendSubsystem* Frontend = GetFrontendSubsystem())
	{
		Frontend->OnFrontendSnapshotChanged.RemoveAll(this);
		Frontend->OnLobbySnapshotChanged.RemoveAll(this);
		Frontend->OnSessionResultsChanged.RemoveAll(this);
		Frontend->OnFeedback.RemoveAll(this);
	}
	Super::NativeDestruct();
}

UAeyerjiFrontendSubsystem* UW_AeyerjiFrontendShell::GetFrontendSubsystem() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAeyerjiFrontendSubsystem>() : nullptr;
}

bool UW_AeyerjiFrontendShell::HasOnlineParty() const
{
	UGameInstance* GI = GetGameInstance();
	if (const UAeyerjiSessionSubsystem* Sessions = GI ? GI->GetSubsystem<UAeyerjiSessionSubsystem>() : nullptr)
	{
		return Sessions->HasOnlineParty();
	}
	return false;
}

UWidget* UW_AeyerjiFrontendShell::FindDesignerWidget(const FName WidgetName) const
{
	return GetWidgetFromName(WidgetName);
}

UButton* UW_AeyerjiFrontendShell::FindDesignerButton(const FName WidgetName) const
{
	return Cast<UButton>(FindDesignerWidget(WidgetName));
}

UTextBlock* UW_AeyerjiFrontendShell::FindDesignerText(const FName WidgetName) const
{
	return Cast<UTextBlock>(FindDesignerWidget(WidgetName));
}

void UW_AeyerjiFrontendShell::BindNativeButtonHandlers()
{
	if (UButton* Button = FindDesignerButton(TEXT("Button_Play"))) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandlePlayClicked);
	if (UButton* Button = FindDesignerButton(TEXT("Button_HostPublicParty"))) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleHostPublicPartyClicked);
	if (UButton* Button = FindDesignerButton(TEXT("Button_PartyBrowser"))) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandlePartyBrowserClicked);
	if (UButton* Button = FindDesignerButton(TEXT("Button_Refresh"))) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleRefreshPartiesClicked);
	if (UButton* Button = FindDesignerButton(TEXT("Button_BrowserBack"))) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleBrowserBackClicked);
	if (UButton* Button = FindDesignerButton(TEXT("Button_Ready"))) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleReadyClicked);
	if (UButton* Button = FindDesignerButton(TEXT("Button_Campaign"))) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleStandardRiftClicked);
	if (UButton* Button = FindDesignerButton(TEXT("CampaignButton"))) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleStandardRiftClicked);
	if (UButton* Button = FindDesignerButton(TEXT("Button_Excursion"))) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleExcursionClicked);
	if (UButton* Button = FindDesignerButton(TEXT("ExcursionButton"))) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleExcursionClicked);
	if (UButton* Button = FindDesignerButton(TEXT("Button_TierPrevious"))) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleTierPreviousClicked);
	if (UButton* Button = FindDesignerButton(TEXT("Button_TierNext"))) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleTierNextClicked);
	if (UButton* Button = FindDesignerButton(TEXT("Button_Launch"))) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleLaunchClicked);
	if (UButton* Button = FindDesignerButton(TEXT("Button_Leave"))) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleLeaveClicked);
	if (UButton* Button = FindDesignerButton(TEXT("Button_Invite"))) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleInviteClicked);
}

void UW_AeyerjiFrontendShell::ApplyNativeFrontendSnapshot(const FAeyerjiFrontendSnapshot& Snapshot)
{
	SetWidgetText(TEXT("Text_Level"), FText::Format(
		AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_LevelFormat")), FText::AsNumber(Snapshot.CharacterLevel)));
	SetWidgetText(TEXT("Text_XP"), FText::Format(
		AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_XPFormat")),
		FText::AsNumber(FrontendXPInteger(Snapshot.CurrentXP)),
		FText::AsNumber(FrontendXPInteger(Snapshot.XPRequiredForNextLevel))));
	SetWidgetText(TEXT("Text_Gold"), FText::Format(
		AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_GoldFormat")), FText::AsNumber(Snapshot.Gold)));

	if (UProgressBar* ProgressBar = Cast<UProgressBar>(FindDesignerWidget(TEXT("Progress_XP"))))
	{
		const float SafeCurrentXP = FMath::Clamp(
			FMath::IsFinite(Snapshot.CurrentXP) ? Snapshot.CurrentXP : 0.f, 0.f, MaxFrontendXPDisplay);
		const float SafeRequiredXP = FMath::Clamp(
			FMath::IsFinite(Snapshot.XPRequiredForNextLevel) ? Snapshot.XPRequiredForNextLevel : 0.f,
			0.f,
			MaxFrontendXPDisplay);
		const float Percent = SafeRequiredXP > 0.f
			? FMath::Clamp(SafeCurrentXP / SafeRequiredXP, 0.f, 1.f)
			: 0.f;
		ProgressBar->SetPercent(Percent);
	}

	const bool bProfileReady = Snapshot.ProfileState == EAeyerjiFrontendProfileState::Ready;
	if (PageSwitcher)
	{
		PageSwitcher->SetIsEnabled(bProfileReady);
	}
	SetWidgetText(TEXT("Text_ProfileStatus"), bProfileReady
		? FText::GetEmpty()
		: AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_ProfileResolving")));
	SetWidgetVisibility(TEXT("Text_ProfileStatus"), bProfileReady ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);

	const FText OperationText = GetFrontendOperationText(Snapshot.OperationState);
	SetWidgetText(TEXT("Text_Operation"), OperationText);
	SetWidgetVisibility(TEXT("Text_Operation"), OperationText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
}

void UW_AeyerjiFrontendShell::ApplyNativeLobbySnapshot(const FAeyerjiLobbySnapshot& Snapshot)
{
	for (int32 SlotIndex = 1; SlotIndex <= 4; ++SlotIndex)
	{
		SetWidgetVisibility(FName(*FString::Printf(TEXT("MemberSlot_%d"), SlotIndex)), ESlateVisibility::Collapsed);
	}
	for (int32 MemberIndex = 0; MemberIndex < Snapshot.Members.Num() && MemberIndex < 4; ++MemberIndex)
	{
		ApplyLobbyMemberToSlot(MemberIndex + 1, Snapshot.Members[MemberIndex]);
	}

	const FAeyerjiLobbyMemberView* LocalMember = FindLocalLobbyMember(Snapshot);
	const bool bWaiting = Snapshot.Phase == EAeyerjiLobbyPhase::Waiting;
	const bool bLocalProfileVerified = LocalMember && LocalMember->ProfileState == EAeyerjiLobbyProfileState::Verified;
	const bool bLocalLeader = IsLocalLobbyLeader(Snapshot);
	const bool bCanReady = bWaiting && bLocalProfileVerified;
	const bool bCanConfigure = bWaiting && bLocalLeader;
	const bool bTierVisible = Snapshot.ActivityType == EAeyerjiRiftActivityType::Excursion;
	const bool bCanChangeTier = bCanConfigure && bTierVisible && Snapshot.CommonExcursionTierCap >= 1;
	const bool bAllMembersLaunchReady = Snapshot.Members.Num() > 0 && Algo::AllOf(Snapshot.Members,
		[](const FAeyerjiLobbyMemberView& Member)
		{
			return Member.ProfileState == EAeyerjiLobbyProfileState::Verified && Member.bReady;
		});

	SetWidgetEnabled(TEXT("Button_Ready"), bCanReady);
	SetWidgetText(TEXT("Text_Ready"), LocalMember && LocalMember->bReady
		? AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_Unready"))
		: AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_Ready")));
	SetWidgetEnabled(TEXT("Button_Campaign"), bCanConfigure);
	SetWidgetEnabled(TEXT("CampaignButton"), bCanConfigure);
	SetWidgetEnabled(TEXT("Button_Excursion"), bCanConfigure);
	SetWidgetEnabled(TEXT("ExcursionButton"), bCanConfigure);
	SetWidgetEnabled(TEXT("Button_TierPrevious"), bCanChangeTier);
	SetWidgetEnabled(TEXT("Button_TierNext"), bCanChangeTier);
	SetWidgetEnabled(TEXT("Button_Launch"), bCanConfigure && bAllMembersLaunchReady);
	SetWidgetText(TEXT("Text_SelectedTier"), FText::AsNumber(Snapshot.SelectedExcursionTier));
	SetWidgetVisibility(TEXT("Button_TierPrevious"), bTierVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	SetWidgetVisibility(TEXT("Text_SelectedTier"), bTierVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	SetWidgetVisibility(TEXT("Button_TierNext"), bTierVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UW_AeyerjiFrontendShell::ApplyLobbyMemberToSlot(const int32 SlotIndex, const FAeyerjiLobbyMemberView& Member)
{
	const FString Suffix = FString::FromInt(SlotIndex);
	SetWidgetVisibility(FName(*FString::Printf(TEXT("MemberSlot_%s"), *Suffix)), ESlateVisibility::Visible);
	SetWidgetText(FName(*FString::Printf(TEXT("Text_Name_%s"), *Suffix)), FText::FromString(Member.DisplayName));
	SetWidgetText(FName(*FString::Printf(TEXT("Text_Level_%s"), *Suffix)), FText::AsNumber(Member.CharacterLevel));
	SetWidgetText(FName(*FString::Printf(TEXT("Text_HighestTier_%s"), *Suffix)), FText::AsNumber(Member.HighestUnlockedExcursionTier));
	SetWidgetText(FName(*FString::Printf(TEXT("Text_ProfileState_%s"), *Suffix)), GetLobbyProfileStateText(Member.ProfileState));
	SetWidgetText(FName(*FString::Printf(TEXT("Text_ReadyState_%s"), *Suffix)), Member.bReady
		? AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_Ready"))
		: AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_Unready")));
	SetWidgetText(FName(*FString::Printf(TEXT("Text_LeaderBadge_%s"), *Suffix)), Member.bLeader
		? AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_PartyLeader"))
		: FText::GetEmpty());
	SetWidgetVisibility(FName(*FString::Printf(TEXT("Text_LeaderBadge_%s"), *Suffix)), Member.bLeader
		? ESlateVisibility::Visible
		: ESlateVisibility::Collapsed);
}

void UW_AeyerjiFrontendShell::ShowPage(UWidget* const Page)
{
	if (PageSwitcher && Page)
	{
		PageSwitcher->SetActiveWidget(Page);
	}
}

void UW_AeyerjiFrontendShell::ShowLanding()
{
	ShowPage(Page_Landing);
}

void UW_AeyerjiFrontendShell::ShowPartyBrowser()
{
	ShowPage(Page_PartyBrowser);
}

void UW_AeyerjiFrontendShell::ShowPartyLobby()
{
	ShowPage(Page_PartyLobby);
}

void UW_AeyerjiFrontendShell::SetWidgetEnabled(const FName WidgetName, const bool bEnabled) const
{
	if (UWidget* Widget = FindDesignerWidget(WidgetName))
	{
		Widget->SetIsEnabled(bEnabled);
	}
}

void UW_AeyerjiFrontendShell::SetWidgetVisibility(const FName WidgetName, const ESlateVisibility WidgetVisibility) const
{
	if (UWidget* Widget = FindDesignerWidget(WidgetName))
	{
		Widget->SetVisibility(WidgetVisibility);
	}
}

void UW_AeyerjiFrontendShell::SetWidgetText(const FName WidgetName, const FText& Text) const
{
	if (UTextBlock* TextBlock = FindDesignerText(WidgetName))
	{
		TextBlock->SetText(Text);
	}
}

const FAeyerjiLobbyMemberView* UW_AeyerjiFrontendShell::FindLocalLobbyMember(const FAeyerjiLobbySnapshot& Snapshot) const
{
	const APlayerState* LocalPlayerState = GetOwningPlayerState();
	if (!LocalPlayerState)
	{
		return nullptr;
	}
	return Snapshot.Members.FindByPredicate([LocalPlayerId = LocalPlayerState->GetPlayerId()](const FAeyerjiLobbyMemberView& Member)
	{
		return Member.PlayerId == LocalPlayerId;
	});
}

bool UW_AeyerjiFrontendShell::IsLocalLobbyLeader(const FAeyerjiLobbySnapshot& Snapshot) const
{
	const FAeyerjiLobbyMemberView* LocalMember = FindLocalLobbyMember(Snapshot);
	return LocalMember && LocalMember->bLeader;
}

void UW_AeyerjiFrontendShell::HandlePlayClicked()
{
	ShowPartyLobby();
}

void UW_AeyerjiFrontendShell::HandleHostPublicPartyClicked()
{
	HostPublicParty(FString());
}

void UW_AeyerjiFrontendShell::HandlePartyBrowserClicked()
{
	ShowPartyBrowser();
	SearchPublicParties();
}

void UW_AeyerjiFrontendShell::HandleRefreshPartiesClicked()
{
	SearchPublicParties();
}

void UW_AeyerjiFrontendShell::HandleBrowserBackClicked()
{
	ShowLanding();
}

void UW_AeyerjiFrontendShell::HandleReadyClicked()
{
	if (const FAeyerjiLobbyMemberView* LocalMember = FindLocalLobbyMember(LatestLobbySnapshot))
	{
		SetPartyReady(!LocalMember->bReady);
	}
}

void UW_AeyerjiFrontendShell::HandleStandardRiftClicked()
{
	SelectPartyActivity(EAeyerjiRiftActivityType::StandardRift);
}

void UW_AeyerjiFrontendShell::HandleExcursionClicked()
{
	SelectPartyActivity(EAeyerjiRiftActivityType::Excursion);
}

void UW_AeyerjiFrontendShell::HandleTierPreviousClicked()
{
	if (LatestLobbySnapshot.ActivityType != EAeyerjiRiftActivityType::Excursion || LatestLobbySnapshot.CommonExcursionTierCap < 1)
	{
		return;
	}
	const int32 ProposedTier = FMath::Clamp(LatestLobbySnapshot.SelectedExcursionTier - 1, 1, LatestLobbySnapshot.CommonExcursionTierCap);
	SelectPartyExcursionTier(ProposedTier);
}

void UW_AeyerjiFrontendShell::HandleTierNextClicked()
{
	if (LatestLobbySnapshot.ActivityType != EAeyerjiRiftActivityType::Excursion || LatestLobbySnapshot.CommonExcursionTierCap < 1)
	{
		return;
	}
	const int32 ProposedTier = FMath::Clamp(LatestLobbySnapshot.SelectedExcursionTier + 1, 1, LatestLobbySnapshot.CommonExcursionTierCap);
	SelectPartyExcursionTier(ProposedTier);
}

void UW_AeyerjiFrontendShell::HandleLaunchClicked()
{
	LaunchPartyActivity();
}

void UW_AeyerjiFrontendShell::HandleLeaveClicked()
{
	LeaveCurrentParty();
}

void UW_AeyerjiFrontendShell::HandleInviteClicked()
{
	OpenPartyInviteOverlay();
}

void UW_AeyerjiFrontendShell::RefreshFrontend()
{
	if (UAeyerjiFrontendSubsystem* Frontend = GetFrontendSubsystem()) Frontend->RefreshCurrentState();
}

bool UW_AeyerjiFrontendShell::HostPublicParty(const FString& PartyName)
{
	UAeyerjiFrontendSubsystem* Frontend = GetFrontendSubsystem();
	const bool bRequestAccepted = Frontend && Frontend->HostPublicParty(PartyName);
	if (bRequestAccepted)
	{
		// The session request is asynchronous, but staging can be presented while creation is in progress.
		ShowPartyLobby();
	}
	return bRequestAccepted;
}

bool UW_AeyerjiFrontendShell::SearchPublicParties()
{
	UAeyerjiFrontendSubsystem* Frontend = GetFrontendSubsystem();
	return Frontend && Frontend->SearchPublicParties();
}

bool UW_AeyerjiFrontendShell::JoinPublicParty(const int32 ResultId)
{
	UAeyerjiFrontendSubsystem* Frontend = GetFrontendSubsystem();
	return Frontend && Frontend->JoinPublicParty(ResultId);
}

bool UW_AeyerjiFrontendShell::LeaveCurrentParty()
{
	UAeyerjiFrontendSubsystem* Frontend = GetFrontendSubsystem();
	return Frontend && Frontend->LeaveCurrentParty();
}

bool UW_AeyerjiFrontendShell::OpenPartyInviteOverlay()
{
	UAeyerjiFrontendSubsystem* Frontend = GetFrontendSubsystem();
	return Frontend && Frontend->OpenPartyInviteOverlay();
}

bool UW_AeyerjiFrontendShell::SetPartyReady(const bool bReady)
{
	UAeyerjiFrontendSubsystem* Frontend = GetFrontendSubsystem();
	return Frontend && Frontend->SetReady(bReady);
}

bool UW_AeyerjiFrontendShell::SelectPartyActivity(const EAeyerjiRiftActivityType ActivityType)
{
	UAeyerjiFrontendSubsystem* Frontend = GetFrontendSubsystem();
	return Frontend && Frontend->SelectActivity(ActivityType);
}

bool UW_AeyerjiFrontendShell::SelectPartyExcursionTier(const int32 Tier)
{
	UAeyerjiFrontendSubsystem* Frontend = GetFrontendSubsystem();
	return Frontend && Frontend->SelectExcursionTier(Tier);
}

bool UW_AeyerjiFrontendShell::LaunchPartyActivity()
{
	UAeyerjiFrontendSubsystem* Frontend = GetFrontendSubsystem();
	return Frontend && Frontend->LaunchSelectedActivity();
}

void UW_AeyerjiFrontendShell::HandleFrontendSnapshot(const FAeyerjiFrontendSnapshot& Snapshot)
{
	ApplyNativeFrontendSnapshot(Snapshot);
	ApplyFrontendSnapshot(Snapshot);
}

void UW_AeyerjiFrontendShell::HandleLobbySnapshot(const FAeyerjiLobbySnapshot& Snapshot)
{
	LatestLobbySnapshot = Snapshot;
	if (Snapshot.Phase == EAeyerjiLobbyPhase::InGameplay)
	{
		// The shell is local presentation state and must not survive seamless travel into a run.
		// This also covers menus created directly by Blueprint before the controller adopted them.
		SetVisibility(ESlateVisibility::Collapsed);
		RemoveFromParent();
		return;
	}

	ApplyNativeLobbySnapshot(Snapshot);
	ApplyLobbySnapshot(Snapshot);
	// Hosting reloads the menu as a listen server; select staging again on the newly constructed widget.
	if (HasOnlineParty())
	{
		ShowPartyLobby();
	}
	if (Snapshot.Phase != EAeyerjiLobbyPhase::Launching)
	{
		LastPresentedLaunchTime = -1.f;
	}
	else if (Snapshot.LaunchAtServerTimeSeconds > 0.f && !FMath::IsNearlyEqual(Snapshot.LaunchAtServerTimeSeconds, LastPresentedLaunchTime))
	{
		LastPresentedLaunchTime = Snapshot.LaunchAtServerTimeSeconds;
		PresentLaunchCountdown(Snapshot.LaunchAtServerTimeSeconds);
	}
}

void UW_AeyerjiFrontendShell::HandleSessionResults(const TArray<FAeyerjiSessionSearchResultView>& Results)
{
	ApplySessionResults(Results);
}

void UW_AeyerjiFrontendShell::HandleFeedback(const EAeyerjiFrontendFailure Failure, const FText& Message)
{
	ShowFrontendFeedback(Failure, Message);
}
