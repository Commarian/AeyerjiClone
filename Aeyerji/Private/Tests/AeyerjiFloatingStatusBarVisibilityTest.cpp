#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "GUI/AeyerjiFloatingStatusBarComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiFloatingStatusBarVisibilityTest,
	"Aeyerji.GUI.FloatingStatusBar.DeathVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiFloatingStatusBarVisibilityTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	UAeyerjiFloatingStatusBarComponent* StatusBar =
		NewObject<UAeyerjiFloatingStatusBarComponent>(GetTransientPackage());
	TestNotNull(TEXT("A transient floating status-bar component can be created."), StatusBar);
	if (!StatusBar)
	{
		return false;
	}

	TestTrue(
		TEXT("A new status-bar source permits presentation."),
		StatusBar->IsStatusBarPresentationVisible());
	StatusBar->SetStatusBarPresentationVisible(false);
	TestFalse(
		TEXT("Death presentation can hide a retained pooled status-bar source."),
		StatusBar->IsStatusBarPresentationVisible());
	StatusBar->SetStatusBarPresentationVisible(true);
	TestTrue(
		TEXT("Pool checkout can restore the retained status-bar source."),
		StatusBar->IsStatusBarPresentationVisible());

	return true;
}

#endif
