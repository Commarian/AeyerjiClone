#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Frontend/AeyerjiFrontendTypes.h"
#include "W_AeyerjiFrontendShell.generated.h"

class UAeyerjiFrontendSubsystem;
class UButton;
class UProgressBar;
class UTextBlock;
class UWidget;
class UWidgetSwitcher;

/** Native event-driven contract that the existing W_MainMenu Blueprint should be reparented to later. */
UCLASS(Abstract, Blueprintable)
class AEYERJI_API UW_AeyerjiFrontendShell : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend")
	void RefreshFrontend();

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Session")
	bool HostPublicParty(const FString& PartyName);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Session")
	bool SearchPublicParties();

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Session")
	bool JoinPublicParty(int32 ResultId);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Session")
	bool LeaveCurrentParty();

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Lobby")
	bool SetPartyReady(bool bReady);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Lobby")
	bool SelectPartyActivity(EAeyerjiRiftActivityType ActivityType);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Lobby")
	bool SelectPartyExcursionTier(int32 Tier);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Lobby")
	bool LaunchPartyActivity();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Set persistent profile header values and operation-state presentation. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Frontend|Presentation")
	void ApplyFrontendSnapshot(const FAeyerjiFrontendSnapshot& Snapshot);

	/** Rebuild shared selection, ready controls, and the four-member roster from this snapshot. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Frontend|Presentation")
	void ApplyLobbySnapshot(const FAeyerjiLobbySnapshot& Snapshot);

	/** Rebuild the browser list from Blueprint-safe results; IDs expire on the next search. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Frontend|Presentation")
	void ApplySessionResults(const TArray<FAeyerjiSessionSearchResultView>& Results);

	/** Present already-localized operation or validation feedback without embedding raw UI text in C++. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Frontend|Presentation")
	void ShowFrontendFeedback(EAeyerjiFrontendFailure Failure, const FText& Message);

	/** Start the portal/countdown presentation against synchronized GameState server time. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Frontend|Presentation")
	void PresentLaunchCountdown(float LaunchAtServerTimeSeconds);

private:
	/** Optional Designer binding for the frontend page container. Keep the named pages inside this switcher. */
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional, AllowPrivateAccess="true"), Category="Aeyerji|Frontend|Presentation")
	TObjectPtr<UWidgetSwitcher> PageSwitcher;

	/** Optional Designer binding for the initial page shown when no retained online party exists. */
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional, AllowPrivateAccess="true"), Category="Aeyerji|Frontend|Presentation")
	TObjectPtr<UWidget> Page_Landing;

	/** Optional Designer binding for the public-party discovery page. */
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional, AllowPrivateAccess="true"), Category="Aeyerji|Frontend|Presentation")
	TObjectPtr<UWidget> Page_PartyBrowser;

	/** Optional Designer binding for the party staging page selected after hosting or re-entering a retained party. */
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional, AllowPrivateAccess="true"), Category="Aeyerji|Frontend|Presentation")
	TObjectPtr<UWidget> Page_PartyLobby;

	UAeyerjiFrontendSubsystem* GetFrontendSubsystem() const;
	bool HasOnlineParty() const;
	UWidget* FindDesignerWidget(FName WidgetName) const;
	UButton* FindDesignerButton(FName WidgetName) const;
	UTextBlock* FindDesignerText(FName WidgetName) const;
	void BindNativeButtonHandlers();
	void ApplyNativeFrontendSnapshot(const FAeyerjiFrontendSnapshot& Snapshot);
	void ApplyNativeLobbySnapshot(const FAeyerjiLobbySnapshot& Snapshot);
	void ApplyLobbyMemberToSlot(int32 SlotIndex, const FAeyerjiLobbyMemberView& Member);
	void ShowPage(UWidget* Page);
	void ShowLanding();
	void ShowPartyBrowser();
	void ShowPartyLobby();
	void SetWidgetEnabled(FName WidgetName, bool bEnabled) const;
	void SetWidgetVisibility(FName WidgetName, ESlateVisibility WidgetVisibility) const;
	void SetWidgetText(FName WidgetName, const FText& Text) const;
	const FAeyerjiLobbyMemberView* FindLocalLobbyMember(const FAeyerjiLobbySnapshot& Snapshot) const;
	bool IsLocalLobbyLeader(const FAeyerjiLobbySnapshot& Snapshot) const;

	UFUNCTION()
	void HandlePlayClicked();

	UFUNCTION()
	void HandleHostPublicPartyClicked();

	UFUNCTION()
	void HandlePartyBrowserClicked();

	UFUNCTION()
	void HandleRefreshPartiesClicked();

	UFUNCTION()
	void HandleBrowserBackClicked();

	UFUNCTION()
	void HandleReadyClicked();

	UFUNCTION()
	void HandleStandardRiftClicked();

	UFUNCTION()
	void HandleExcursionClicked();

	UFUNCTION()
	void HandleTierPreviousClicked();

	UFUNCTION()
	void HandleTierNextClicked();

	UFUNCTION()
	void HandleLaunchClicked();

	UFUNCTION()
	void HandleLeaveClicked();

	/** Latest replicated lobby view used only to formulate UI requests; server validation remains authoritative. */
	FAeyerjiLobbySnapshot LatestLobbySnapshot;

	/** Avoids replaying a portal animation whenever an unchanged launching snapshot is republished. */
	float LastPresentedLaunchTime = -1.f;

	void HandleFrontendSnapshot(const FAeyerjiFrontendSnapshot& Snapshot);
	void HandleLobbySnapshot(const FAeyerjiLobbySnapshot& Snapshot);
	void HandleSessionResults(const TArray<FAeyerjiSessionSearchResultView>& Results);
	void HandleFeedback(EAeyerjiFrontendFailure Failure, const FText& Message);
};
